# Spike fase2-gs-gpu T1: reproduce los volcados del GS de PCSX2 (build\pgs-dumps\*.gs) con paraLLEl-GS
# (build\pgs\tools\parallel-gs-spike.exe, herramienta local en third_party\parallel-gs\tools\gs_spike_replayer.cpp).
# Para cada volcado y cada factor de supermuestreo guarda PNG en logs\pgs\ y una linea RESULT (ms por vblank:
# pared, CPU enviando al GS desde memoria, suma de marcas de tiempo de la GPU) en build\pgs-resultados.txt.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\pgs-reproducir.ps1
#   [-Volcados grid-setup,race-start-lights] [-Factores 1,2,4,8] [-Vsyncs '2'] [-Pasadas 3] [-AltaRes]
#   -AltaRes: salida a la resolucion interna x factor (reescalado); sin ella el supermuestreo se reduce a nativa.
param(
    [string[]]$Volcados = @('menu-quick-race', 'grid-setup', 'race-start-lights', 'race-start-go'),
    [string[]]$Factores = @(1, 2, 4, 8),
    [string]$Vsyncs = '',
    [int]$Pasadas = 3,
    [switch]$AltaRes
)
$ErrorActionPreference = 'Stop'
$proyecto = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $proyecto 'build\pgs\tools\parallel-gs-spike.exe'
$salida = Join-Path $proyecto 'logs\pgs'
$resultados = Join-Path $proyecto 'build\pgs-resultados.txt'
New-Item -ItemType Directory -Force -Path $salida | Out-Null
$Volcados = @($Volcados | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Factores = @($Factores | ForEach-Object { "$_" -split "," } | Where-Object { $_ })
$porDefecto = @{ 'menu-quick-race' = '2'; 'grid-setup' = '2'; 'menu-player-details' = '2';
    'race-start-lights' = '10,200,280'; 'race-start-go' = '20,80,140' }

foreach ($v in $Volcados) {
    $gs = Join-Path $proyecto "build\pgs-dumps\$v.gs"
    $lista = if ($Vsyncs) { $Vsyncs } elseif ($porDefecto.ContainsKey($v)) { $porDefecto[$v] } else { '2' }
    foreach ($f in $Factores) {
        $sufijo = if ($AltaRes) { "x$f-hr" } else { "x$f" }
        $argumentos = @($gs, '--ssaa', "$f", '--iterations', "$Pasadas", '--png', (Join-Path $salida "$v-$sufijo"),
            '--png-vsyncs', $lista)
        if ($AltaRes) { $argumentos += '--high-res-scanout' }
        # La herramienta escribe su registro por stderr: no es un error.
        $ErrorActionPreference = 'Continue'
        $log = & $exe @argumentos 2>&1 | ForEach-Object { "$_" }
        $ErrorActionPreference = 'Stop'
        $res = $log | Where-Object { $_ -match 'RESULT|PNG|ERROR' }
        $res | ForEach-Object { Write-Output "$v ${sufijo}: $_" }
        $res | Where-Object { $_ -match 'RESULT' } | ForEach-Object { Add-Content -LiteralPath $resultados -Value "$v ${sufijo}: $_" }
    }
}
