# Formatea (o verifica con -Check) todo el código C++ con el clang-format del
# Visual Studio instalado, para que local y CI usen la misma herramienta.
param([switch]$Check)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$clangFormat = (Get-Command clang-format -ErrorAction SilentlyContinue).Source
if (-not $clangFormat) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -property installationPath
    $clangFormat = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-format.exe'
}
if (-not (Test-Path $clangFormat)) { throw "No se encontró clang-format" }
& $clangFormat --version

$files = Get-ChildItem -Path (Join-Path $root 'src'), (Join-Path $root 'tests') -Recurse -Include *.cpp, *.hpp, *.h
if ($Check) {
    & $clangFormat --dry-run --Werror $files.FullName
    if ($LASTEXITCODE -ne 0) { throw "Hay archivos sin formatear: ejecuta scripts/format.ps1" }
    Write-Host "Formato correcto ($($files.Count) archivos)"
} else {
    & $clangFormat -i $files.FullName
    Write-Host "Formateados $($files.Count) archivos"
}
