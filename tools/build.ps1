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

Push-Location $projectRoot
try {
    if ($Target -in @('all', 'firmware')) {
        Invoke-Checked cmake @('--preset', 'firmware-debug')
        Invoke-Checked cmake @('--build', '--preset', 'firmware-debug')
    }

    if ($Target -in @('all', 'host-tests')) {
        . (Join-Path $PSScriptRoot 'import-msvc-environment.ps1')
        Invoke-Checked cmake @('--preset', 'host-debug')
        Invoke-Checked cmake @('--build', '--preset', 'host-debug')
        Invoke-Checked ctest @('--preset', 'host-debug')
    }
}
finally {
    Pop-Location
}
