[CmdletBinding()]
param(
    [ValidateSet('all', 'firmware', 'host-tests')]
    [string]$Target = 'all'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

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

function Get-MsvcEnvironmentScript {
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Build Tools were not found; host tests require an MSVC C compiler'
    }

    $installationPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
        throw 'A Visual Studio installation with the C/C++ build tools was not found'
    }

    $vcvars = Join-Path $installationPath.Trim() 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) {
        throw "MSVC environment script was not found at $vcvars"
    }

    return $vcvars
}

Push-Location $projectRoot
try {
    if ($Target -in @('all', 'firmware')) {
        Invoke-Checked cmake @('--preset', 'firmware-debug')
        Invoke-Checked cmake @('--build', '--preset', 'firmware-debug')
    }

    if ($Target -in @('all', 'host-tests')) {
        $vcvars = Get-MsvcEnvironmentScript
        $hostCommand = "call `"$vcvars`" >nul && " +
                       'cmake --preset host-debug && ' +
                       'cmake --build --preset host-debug && ' +
                       'ctest --preset host-debug'
        & $env:ComSpec /d /s /c $hostCommand
        if ($LASTEXITCODE -ne 0) {
            throw "Host build or tests failed with exit code $LASTEXITCODE"
        }
    }
}
finally {
    Pop-Location
}
