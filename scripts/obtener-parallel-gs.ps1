# Fase 2 (GS por GPU), T3: obtiene paraLLEl-GS fijado al commit cc6184a en third_party\parallel-gs (solo si falta:
# un clon que ya existe no se borra ni se toca) con los submodulos que necesita su modo STANDALONE (Granite, volk y
# las cabeceras de Vulkan), y compila nuestro puente pgs_puente.dll en build\pgs-puente con las herramientas
# portables (gpu\pgs_puente\compilar.cmd).
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\obtener-parallel-gs.ps1 [-SinCompilar]
param(
    [switch]$SinCompilar
)
$ErrorActionPreference = 'Stop'
$proyecto = Split-Path -Parent $PSScriptRoot
$clon = Join-Path $proyecto 'third_party\parallel-gs'
$url = 'https://github.com/Arntzen-Software/parallel-gs'
$commit = 'cc6184a'

function Ejecutar-Git {
    param([string]$Directorio, [string[]]$Argumentos)
    # git escribe su progreso por stderr: no es un error.
    $ErrorActionPreference = 'Continue'
    & git -C $Directorio @Argumentos 2>&1 | ForEach-Object { Write-Output "  $_" }
    $codigo = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($codigo -ne 0) { throw "git $($Argumentos -join ' ') fallo (codigo $codigo)" }
}

if (Test-Path -LiteralPath (Join-Path $clon 'gs\gs_interface.hpp')) {
    Write-Output "[parallel-gs] Ya existe $clon; no se descarga de nuevo."
    $actual = (& git -C $clon rev-parse --short=7 HEAD 2>$null)
    if ($actual -and $actual -ne $commit) {
        Write-Output "[parallel-gs] Aviso: el clon esta en $actual, no en $commit (no se cambia)."
    }
}
elseif (Test-Path -LiteralPath $clon) {
    throw "[parallel-gs] $clon existe pero no parece un clon de paraLLEl-GS; revisalo a mano (no se borra nada)."
}
else {
    Write-Output "[parallel-gs] Clonando $url en $clon..."
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $clon) | Out-Null
    Ejecutar-Git -Directorio $proyecto -Argumentos @('clone', '--no-checkout', $url, $clon)
    Ejecutar-Git -Directorio $clon -Argumentos @('checkout', $commit)
    # Los submodulos del modo STANDALONE (como checkout_submodules.sh del propio repositorio).
    Ejecutar-Git -Directorio $clon -Argumentos @('submodule', 'update', '--init', 'Granite')
    $granite = Join-Path $clon 'Granite'
    Ejecutar-Git -Directorio $granite -Argumentos @('submodule', 'update', '--init', 'third_party/volk',
        'third_party/khronos/vulkan-headers')
}

if ($SinCompilar) { exit 0 }

Write-Output '[parallel-gs] Compilando build\pgs-puente\pgs_puente.dll...'
$cmd = Join-Path $proyecto 'gpu\pgs_puente\compilar.cmd'
& cmd.exe /c "`"$cmd`""
if ($LASTEXITCODE -ne 0) { throw "[parallel-gs] La compilacion de pgs_puente.dll fallo (codigo $LASTEXITCODE)." }
$dll = Join-Path $proyecto 'build\pgs-puente\pgs_puente.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw "[parallel-gs] No se encuentra $dll tras compilar." }
Write-Output "[parallel-gs] Listo: $dll"
