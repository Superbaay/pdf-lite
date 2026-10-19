# Prepara vcpkg dentro del repo (carpeta ignorada por git) en el commit fijado
# por "builtin-baseline" de vcpkg.json. Ejecutar una vez tras clonar.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcpkgDir = Join-Path $root 'vcpkg'
$baseline = (Get-Content (Join-Path $root 'vcpkg.json') -Raw | ConvertFrom-Json).'builtin-baseline'

if (-not (Test-Path (Join-Path $vcpkgDir '.git'))) {
    git clone https://github.com/microsoft/vcpkg.git $vcpkgDir
}
git -C $vcpkgDir fetch --quiet origin
git -C $vcpkgDir checkout --quiet $baseline
& (Join-Path $vcpkgDir 'bootstrap-vcpkg.bat') -disableMetrics
Write-Host "vcpkg listo en $vcpkgDir (baseline $baseline)"
