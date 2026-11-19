# IOP T3 (odd/tasks/iop-salida.md, S2-S3): caza por guion del aviso del IOP al salir de carrera con QUIT
# ("WARNING: Closing file ... will probably be fatal" / "IOP FATAL ERROR: callback received when status is ...").
# Variante de scripts\explorar.ps1 que lo deja todo en logs\iop-t3 (logs, capturas, pilas, peticion de captura y
# tarjetas de prueba copiadas de logs\vigilante-t5\tarjetas: nunca toca juego\mc0 ni las partidas del usuario).
# Mismas opciones del jugador que la caza del vigilante T5 (logs\vigilante-t5\correr.ps1) y PS2X_IOP_TRAZA_CD=1,
# pero sin piloto automatico (F1_PILOTO=0; ver el guion) salvo con -Piloto (como la salida con aviso de vig-t5-quit-b).
# Hace N ciclos carrera -> pausa -> QUIT -> carrera; en el ciclo n pulsa START a los Tiempos[n] s de game#n
# (variando el momento: justo tras la parrilla (game+25 = X en "Grid setup"; antes no hay pausa), salida, media vuelta...). Las entradas Menu#k son iguales para todo k (5 abajo,
# cruz, cruz), y hay un START de reserva a game#n+T+12 por si la pausa se cierra sola (L9 del vigilante): si la
# primera funciono, cae en la carga de QUIT -> menu.
# No arranca nada si F1-06.exe o ps2EntryRunner.exe estan abiertos.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\iop-t3-correr.ps1 -Nombre r1
#        [-Tiempos '30,60,27,...'] [-Limite 2400] [-SinSucesos 240]   (Tiempos >= 26: antes de X no hay pausa)
param(
    [Parameter(Mandatory = $true)][string]$Nombre,
    [string]$Tiempos = '30,60,27,90,28,45,26,120,35,75,29,50',
    [int]$Limite = 2400,
    [int]$SinSucesos = 240,
    [switch]$Piloto
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
if (Get-Process -Name 'F1-06', 'ps2EntryRunner' -ErrorAction SilentlyContinue) { "F1-06.exe o el runner abierto: no arranco $Nombre"; exit 1 }
$dir = Join-Path $raiz 'logs\iop-t3'
New-Item -ItemType Directory -Path $dir -Force | Out-Null

# ---- Guion ----
$t = @($Tiempos -split ',' | ForEach-Object { [double]$_ })
$g = New-Object System.Collections.Generic.List[string]
$g.AddRange([string[]]@('pad+3:cruz:0.5', 'pad+8:cruz:0.5', 'Continue#3+1.5:start:0.5', 'Attract+5:start:0.5',
    'LOADfromDir+4:cruz:0.5', 'SerializeSucceeded+4:cruz:0.5', 'NoInGameAtBoot+4:cruz:0.5', 'SinglePlayer+4:cruz:0.5',
    'InstantRace+6:cruz:0.5'))
for ($n = 1; $n -le $t.Count; $n++) {
    $tp = $t[$n - 1]
    # X en "Grid setup" (game+25) y, sin piloto automatico, acelerar hasta 2 s antes de la pausa (despues las
    # pulsaciones caerian en los menus). El piloto (F1_PILOTO=1) mantiene la cruz pulsada tambien con la pausa
    # abierta y la cruz del guion no selecciona QUIT (r3: pausa parada en QUIT).
    $g.Add("game#$n+25:cruz:0.5")
    if ($tp -ge 30 -and -not $Piloto) { $g.Add("game#$n+26:cruz:$($tp - 28)") }
    $g.Add("game#$n+$($tp):start:0.1")
    $g.Add("game#$n+$($tp + 12):start:0.1")
    if ($n -gt 1) {
        $g.Add("InstantRace1P#$($n - 1)+3:cruz:0.5"); $g.Add("SinglePlayer#$($n + 0)+4:cruz:0.5")
        $g.Add("InstantRace#$($n + 0)+6:cruz:0.5")
    }
}
# Menu#k identicas (hasta 3 por ciclo).
for ($k = 1; $k -le 3 * $t.Count; $k++) {
    foreach ($d in 2, 2.6, 3.2, 3.8, 4.4) { $g.Add("Menu#$k+$($d):abajo:0.15") }
    $g.Add("Menu#$k+5.5:cruz:0.3"); $g.Add("Menu#$k+8.5:cruz:0.3")
}
$guion = $g -join ','
$guion | Out-File -Encoding utf8 (Join-Path $dir "$Nombre-guion.txt")

# ---- Entorno ----
$env:F1_GUION = $guion
Remove-Item env:F1_PULSAR -ErrorAction SilentlyContinue
$env:PS2X_BACKGROUND_WINDOW = '1'
$env:PS2X_SIN_MANDOS = '1'
$tarjetas = Join-Path $dir 'tarjetas'
if (-not (Test-Path (Join-Path $tarjetas 'mc0'))) {
    New-Item -ItemType Directory -Path $tarjetas -Force | Out-Null
    Copy-Item -Path (Join-Path $raiz 'logs\vigilante-t5\tarjetas\*') -Destination $tarjetas -Recurse -Force
}
$env:PS2X_TARJETAS = $tarjetas
$v = @($(if ($Piloto) { 'F1_PILOTO=1' } else { 'F1_PILOTO=0' }), 'PS2X_GS=gpu', 'PS2X_GS_SSAA=4', 'PS2X_GS_ALTA_RES=1', 'PS2X_VSYNC=1', 'PS2X_FILTRO=fsr',
    'PS2X_CARGAS_RAPIDAS=1', 'F1_AB=1', 'F1_AB_DESDE=1000000', 'PS2X_IOP_TRAZA_CD=1',
    "PS2X_CONTROLES=$dir\controles-inexistente.ini")
foreach ($par in $v) { if ($par -match '^([^=]+)=(.*)$') { Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2] } }

Add-Type -AssemblyName System.Drawing
$peticion = Join-Path $dir ("captura-{0}.peticion" -f $PID)
Remove-Item $peticion -ErrorAction SilentlyContinue
$env:PS2X_CAPTURA_PETICION = $peticion
function Capturar([System.Diagnostics.Process]$proc, [string]$destino) {
    Remove-Item $destino -ErrorAction SilentlyContinue
    [System.IO.File]::WriteAllText($peticion, $destino)
    $limiteCaptura = (Get-Date).AddSeconds(5)
    while ((Test-Path $peticion) -and (Get-Date) -lt $limiteCaptura -and -not $proc.HasExited) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $peticion) -and (Test-Path $destino)) { return "Captura: $destino" }
    Remove-Item $peticion -ErrorAction SilentlyContinue
    "Sin captura: $destino"
}

$base = Join-Path $dir $Nombre
$out = "$base.out.log"; $err = "$base.err.log"
"$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') inicio $Nombre tiempos=$Tiempos ($($v -join ';'))"
$proc = Start-Process -FilePath (Join-Path $raiz 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe') -WorkingDirectory $raiz `
    -ArgumentList 'juego\SCES_539.50' -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err
$inicio = Get-Date
function Segundos() { ((Get-Date) - $inicio).TotalSeconds }
$sucesos = New-Object System.Collections.Generic.List[string]
$vistos = @{}
$leidos = 0
$ultimoSuceso = 0.0
function Leer() {
    if (-not (Test-Path $out)) { return }
    try {
        $fs = New-Object System.IO.FileStream($out, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $sr = New-Object System.IO.StreamReader($fs); $texto = $sr.ReadToEnd(); $sr.Close()
    } catch { return }
    $ms = [regex]::Matches($texto, '\[f1-06:lua\] t=([0-9.]+) graph\.outcome\("([^"]*)"\)')
    for ($i = $script:leidos; $i -lt $ms.Count; $i++) {
        $ev = $ms[$i].Groups[2].Value
        $n = 1; while ($script:vistos.ContainsKey("$ev#$n")) { $n++ }
        $script:vistos["$ev#$n"] = Segundos
        $script:sucesos.Add(("t={0} {1}#{2} (script {3:N0} s)" -f $ms[$i].Groups[1].Value, $ev, $n, (Segundos)))
        $script:ultimoSuceso = Segundos
    }
    $script:leidos = $ms.Count
}
$resumen = New-Object System.Collections.Generic.List[string]
$ultimoN = $t.Count
$capturados = @{}
try {
    while (-not $proc.HasExited -and (Segundos) -lt $Limite) {
        Leer
        # Una captura 5 s despues de cada vuelta al menu (InstantRace1P#n): comprueba que no se queda colgado.
        foreach ($clave in @($vistos.Keys)) {
            if ($clave -like 'InstantRace1P#*' -and -not $capturados.ContainsKey($clave) -and (Segundos) -ge $vistos[$clave] + 5) {
                $capturados[$clave] = 1
                $resumen.Add("$clave+5: " + (Capturar $proc ("{0}-{1}.png" -f $base, ($clave -replace '#', '_'))))
            }
        }
        if ($vistos.ContainsKey("InstantRace1P#$ultimoN") -and (Segundos) -ge $vistos["InstantRace1P#$ultimoN"] + 15) {
            $resumen.Add("Fin: InstantRace1P#$ultimoN+15"); break
        }
        if ((Segundos) -gt 60 -and (Segundos) - $ultimoSuceso -gt $SinSucesos) {
            $pila = "$base-pila.txt"
            & "$raiz\build\pila.exe" $proc.Id | Out-File -Encoding utf8 $pila
            $resumen.Add(("Sin sucesos nuevos desde hace {0} s (a los {1:N0} s): pilas en {2}" -f $SinSucesos, (Segundos), $pila))
            $resumen.Add((Capturar $proc "$base-sin-sucesos.png"))
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if ($proc.HasExited) { $resumen.Add("El juego se cerro solo (codigo $($proc.ExitCode)) a los {0:N0} s" -f (Segundos)) }
    elseif ((Segundos) -ge $Limite) { $resumen.Add(("Limite de {0} s" -f $Limite)); $resumen.Add((Capturar $proc "$base-limite.png")) }
}
finally {
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Remove-Item $peticion -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 1
Leer
$avisos = 0
try { $avisos = @(Select-String -Path $err -Pattern 'Closing file' -SimpleMatch).Count } catch {}
$todo = @('Sucesos:') + $sucesos + @('', 'Capturas y final:') + $resumen + @('', "Avisos 'Closing file': $avisos")
$todo | Out-File -Encoding utf8 "$base.txt"
$todo | Select-Object -Last 12
"$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') fin $Nombre"
