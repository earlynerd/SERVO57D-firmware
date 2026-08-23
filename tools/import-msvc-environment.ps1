[CmdletBinding()]
param()

# Dot-source this file to prepare the current PowerShell process:
#   . .\tools\import-msvc-environment.ps1

$programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
$vswhere = Join-Path `
    $programFilesX86 `
    'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Build Tools were not found; host tests require an MSVC C compiler'
}

$installationPaths = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
$vswhereExitCode = $LASTEXITCODE
$installationPath = $installationPaths | Select-Object -First 1
if ($vswhereExitCode -ne 0 -or
    [string]::IsNullOrWhiteSpace($installationPath)) {
    throw 'A Visual Studio installation with the C/C++ build tools was not found'
}

$vcvars = Join-Path `
    $installationPath.Trim() `
    'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "MSVC environment script was not found at $vcvars"
}

$environmentCommand = "call `"$vcvars`" >nul && set"
$environmentLines = & $env:ComSpec /d /s /c $environmentCommand
if ($LASTEXITCODE -ne 0) {
    throw "MSVC environment initialization failed with exit code $LASTEXITCODE"
}

$importedNames = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($line in $environmentLines) {
    $separator = $line.IndexOf('=')
    if ($separator -le 0) {
        continue
    }

    $name = $line.Substring(0, $separator)
    if (-not $importedNames.Add($name)) {
        # Windows names are case-insensitive. Some parent environments expose
        # both PATH and Path; vcvars emits its augmented PATH first.
        continue
    }
    $value = $line.Substring($separator + 1)
    [Environment]::SetEnvironmentVariable($name, $value, 'Process')
}

if ([string]::IsNullOrWhiteSpace($env:INCLUDE) -or
    [string]::IsNullOrWhiteSpace($env:LIB) -or
    $null -eq (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'MSVC initialized without a complete compiler, INCLUDE, and LIB environment'
}

Write-Verbose "Imported the MSVC x64 environment from $vcvars"
