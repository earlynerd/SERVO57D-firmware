[CmdletBinding()]
param(
    [ValidateRange(50, 4000)]
    [int]$SpeedKHz = 200,

    [ValidatePattern('^[0-9]+$')]
    [string]$ProbeSerial,

    [string]$JLinkPath,

    [switch]$SkipBuild,

    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$firmwarePreset = 'firmware-debug'
$firmwareDirectory = Join-Path $projectRoot 'build\firmware-debug\firmware'
$elfPath = Join-Path $firmwareDirectory 'mks57d.elf'
$binPath = Join-Path $firmwareDirectory 'mks57d.bin'
$deviceName = 'N32L406CB'
$flashStart = [uint32]0x08000000
$flashEndExclusive = [uint32]0x08020000
$expectedStackTop = [uint32]0x20004000
$rdpLevel2Mask = [Convert]::ToUInt32('80000000', 16)

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [string]$Command,

        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

function Invoke-JLinkChecked {
    param(
        [Parameter(Mandatory)]
        [string]$Commander,

        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    $output = @(& $Commander @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_.ToString() }
    if ($exitCode -ne 0) {
        throw "J-Link Commander failed with exit code $exitCode"
    }
    return $output
}

function Resolve-JLinkCommander {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        $resolved = (Resolve-Path -LiteralPath $RequestedPath).Path
        if ([IO.Path]::GetFileName($resolved) -ine 'JLink.exe') {
            throw "The requested J-Link Commander is not JLink.exe: $resolved"
        }
        return $resolved
    }

    $candidates = @()
    $programFiles = [Environment]::GetFolderPath('ProgramFiles')
    if (-not [string]::IsNullOrWhiteSpace($programFiles)) {
        $candidates += Join-Path $programFiles 'SEGGER\JLink\JLink.exe'
    }

    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $candidates += Join-Path $programFilesX86 'SEGGER\JLink\JLink.exe'
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'SEGGER J-Link Commander was not found. Pass -JLinkPath with the full path to SEGGER JLink.exe.'
}

function Test-FirmwareImage {
    param([string]$ImagePath)

    $image = [IO.File]::ReadAllBytes($ImagePath)
    if ($image.Length -lt 8) {
        throw "Firmware image is too short to contain a vector table: $ImagePath"
    }
    if ($image.Length -gt ($flashEndExclusive - $flashStart)) {
        throw "Firmware image is larger than the N32L406CB 128 KB flash"
    }

    $initialStackPointer = [BitConverter]::ToUInt32($image, 0)
    $resetVector = [BitConverter]::ToUInt32($image, 4)
    $resetAddress = [uint32]($resetVector -band 0xFFFFFFFE)

    if ($initialStackPointer -ne $expectedStackTop) {
        throw ('Initial stack pointer is 0x{0:X8}; expected 0x{1:X8}' -f
               $initialStackPointer, $expectedStackTop)
    }
    if (($resetVector -band 1) -eq 0) {
        throw ('Reset vector 0x{0:X8} does not select Thumb state' -f $resetVector)
    }
    if ($resetAddress -lt $flashStart -or $resetAddress -ge $flashEndExclusive) {
        throw ('Reset vector 0x{0:X8} is outside N32L406CB flash' -f $resetVector)
    }

    [pscustomobject]@{
        Length = $image.Length
        InitialStackPointer = $initialStackPointer
        ResetVector = $resetVector
    }
}

Push-Location $projectRoot
try {
    if (-not $SkipBuild) {
        Invoke-Checked cmake @('--preset', $firmwarePreset)
        Invoke-Checked cmake @('--build', '--preset', $firmwarePreset)
    }

    if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) {
        throw "Firmware ELF was not found: $elfPath"
    }
    if (-not (Test-Path -LiteralPath $binPath -PathType Leaf)) {
        throw "Firmware binary was not found: $binPath"
    }

    $imageInfo = Test-FirmwareImage $binPath
    $elfHash = (Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash
    $jlink = Resolve-JLinkCommander $JLinkPath

    Write-Host 'First-board J-Link flash preflight passed:'
    Write-Host "  Device:      $deviceName"
    Write-Host "  Interface:   SWD at $SpeedKHz kHz"
    Write-Host "  ELF:         $elfPath"
    Write-Host "  ELF SHA-256: $elfHash"
    Write-Host ('  Image bytes: {0}' -f $imageInfo.Length)
    Write-Host ('  Initial SP:  0x{0:X8}' -f $imageInfo.InitialStackPointer)
    Write-Host ('  Reset vector: 0x{0:X8}' -f $imageInfo.ResetVector)
    Write-Host "  Commander:   $jlink"

    if (-not $Yes) {
        Write-Host ''
        Write-Host 'Dry run only. No target was accessed.'
        Write-Host 'With a current-limited supply appropriate for the intended run, rerun with -Yes to program, verify, reset, and start the firmware.'
        exit 0
    }

    Write-Host ''
    Write-Warning 'Programming will replace the target flash contents. The motor must be disconnected and the controller must use a current-limited supply.'

    $commandFile = [IO.Path]::GetTempFileName()
    try {
        $jlinkArguments = @(
            '-Device', $deviceName,
            '-If', 'SWD',
            '-Speed', $SpeedKHz.ToString(),
            '-AutoConnect', '1',
            '-ExitOnError', '1',
            '-NoGui', '1',
            '-CommandFile', $commandFile
        )
        if (-not [string]::IsNullOrWhiteSpace($ProbeSerial)) {
            $jlinkArguments = @('-USB', $ProbeSerial) + $jlinkArguments
        }

        $preflightCommands = @(
            'ExitOnError 1'
            'Mem32 0x4002201C, 1'
            'Exit'
        )
        [IO.File]::WriteAllLines(
            $commandFile,
            $preflightCommands,
            [Text.UTF8Encoding]::new($false)
        )

        $preflightOutput = Invoke-JLinkChecked $jlink $jlinkArguments
        $preflightText = ($preflightOutput | ForEach-Object { $_.ToString() }) -join "`n"
        $optionMatch = [regex]::Match(
            $preflightText,
            '(?im)4002201C\s*=\s*([0-9A-F]{8})'
        )
        if (-not $optionMatch.Success) {
            throw 'Could not parse the N32 FLASH_OB value from J-Link output; refusing to program'
        }

        $flashOptionBytes = [Convert]::ToUInt32($optionMatch.Groups[1].Value, 16)
        if (($flashOptionBytes -band $rdpLevel2Mask) -ne 0) {
            throw ('Target reports RDP L2 in FLASH_OB 0x{0:X8}; refusing to program' -f $flashOptionBytes)
        }
        if (($flashOptionBytes -band [uint32]0x00000002) -ne 0) {
            throw ('Target reports RDP L1 in FLASH_OB 0x{0:X8}; release protection separately before programming' -f $flashOptionBytes)
        }
        if (($flashOptionBytes -band [uint32]0x00000001) -ne 0) {
            throw ('Target reports an option-byte complement error in FLASH_OB 0x{0:X8}; refusing to program' -f $flashOptionBytes)
        }
        Write-Host ('RDP L0 confirmed: FLASH_OB = 0x{0:X8}' -f $flashOptionBytes)

        $programCommands = @(
            'ExitOnError 1'
            'Reset'
            ('LoadFile "{0}"' -f $elfPath)
            ('VerifyBin "{0}" 0x08000000' -f $binPath)
            'Reset'
            'Go'
            'Exit'
        )
        [IO.File]::WriteAllLines(
            $commandFile,
            $programCommands,
            [Text.UTF8Encoding]::new($false)
        )
        Invoke-JLinkChecked $jlink $jlinkArguments | Out-Null
    }
    finally {
        if ($commandFile -and (Test-Path -LiteralPath $commandFile -PathType Leaf)) {
            Remove-Item -LiteralPath $commandFile
        }
    }

    Write-Host ''
    Write-Host 'Firmware programmed, independently verified, reset, and started.'
}
finally {
    Pop-Location
}
