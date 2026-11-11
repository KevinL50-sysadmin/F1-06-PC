# Mide el rendimiento en carrera: arranca una carrera rapida (Honda/Button en Silverstone) con el guion de
# pulsaciones (F1_GUION), espera a que el log muestre graph.outcome("game") y, contando desde ahi, perfila el hilo
# del EE con build\perfil.exe y captura la ventana. Los tiempos del arranque varian mucho entre ejecuciones, por eso
# todo se cuenta desde "game" y no desde el arranque.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\medir-carrera.ps1 -Nombre base
#        [-PerfilTras 90] [-Muestras 3000] [-Intervalo 4] [-FinTras 150] [-PerfilCarga] [-Guion '...']
#        [-Variables 'F1_X=1;F1_Y=2'] [-Capturas '60,120']
# -PerfilCarga: perfila tambien la carga de la carrera (20 s despues de "InstantRace").
# -Capturas: capturas intermedias a esos segundos despues de "game" (logs\medida-<Nombre>-<s>s.png).
# Salida: logs\medida-<Nombre>.png (captura final), logs\perfil-<Nombre>.txt (y perfil-<Nombre>-carga.txt),
# logs\medida-<Nombre>.txt (resumen de [f1-06:vif1fps]) y los logs del juego logs\medida-<Nombre>.out.log/.err.log.
# La ventana del juego va en segundo plano (PS2X_BACKGROUND_WINDOW=1) y su consola oculta.
param(
    [string]$Nombre = 'base',
    [int]$PerfilTras = 90,
    [int]$Muestras = 3000,
    [int]$Intervalo = 4,
    [int]$FinTras = 150,
    [switch]$PerfilCarga,
    [int]$Limite = 900,
    [string]$Guion = 'pad+3:cruz:0.5,pad+8:cruz:0.5,Continue#3+1.5:start:0.5,Attract+5:start:0.5,LOADfromDir+4:cruz:0.5,SerializeSucceeded+4:cruz:0.5,NoInGameAtBoot+4:cruz:0.5,SinglePlayer+4:cruz:0.5,InstantRace+6:cruz:0.5,game+25:cruz:0.5,game+35:cruz:0.5,game+45:cruz:0.5,game+55:cruz:0.5,game+65:cruz:900',
    [string]$Pulsar = '',
    [string]$Variables = '',
    [string]$Capturas = '',
    # Hilo que perfila build\perfil.exe (su nombre; '' = el que mas CPU gaste al empezar, que en una escena lenta
    # puede no ser el del EE).
    [string]$Hilo = 'GameThread',
    # Ejecutable del juego, relativo a la raiz (para comparar dos compilaciones distintas).
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe'
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$env:F1_GUION = $Guion
if ($Pulsar) { $env:F1_PULSAR = $Pulsar } else { Remove-Item env:F1_PULSAR -ErrorAction SilentlyContinue }
$env:PS2X_BACKGROUND_WINDOW = '1'
$env:PS2X_SIN_MANDOS = '1' # no tocar los mandos ni el volante del usuario (=0 con -Variables para probarlos)
foreach ($par in ($Variables -split ';')) {
    if ($par -match '^\s*([^=]+)=(.*)$') { Set-Item -Path ("env:" + $Matches[1].Trim()) -Value $Matches[2] }
}

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class VentanaMedida {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
}
'@
# Las capturas las guarda el propio juego (PS2X_CAPTURA_PETICION: se le escribe la ruta del PNG en un archivo y lo borra
# al terminar), que funciona tambien con la sesion de Windows bloqueada; si no contesta en 5 s, PrintWindow.
$peticion = Join-Path $raiz ("logs\captura-{0}.peticion" -f $PID)
Remove-Item $peticion -ErrorAction SilentlyContinue
$env:PS2X_CAPTURA_PETICION = $peticion
function Capturar([System.Diagnostics.Process]$proc, [string]$destino) {
    Remove-Item $destino -ErrorAction SilentlyContinue
    [System.IO.File]::WriteAllText($peticion, $destino)
    $limite = (Get-Date).AddSeconds(5)
    while ((Test-Path $peticion) -and (Get-Date) -lt $limite -and -not $proc.HasExited) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $peticion) -and (Test-Path $destino)) { return "Captura: $destino" }
    Remove-Item $peticion -ErrorAction SilentlyContinue
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { return "Sin ventana para capturar: $destino" }
    $r = New-Object VentanaMedida+RECT
    [void][VentanaMedida]::GetClientRect($hwnd, [ref]$r)
    $ancho = [Math]::Max(1, $r.Right - $r.Left); $alto = [Math]::Max(1, $r.Bottom - $r.Top)
    $bmp = New-Object System.Drawing.Bitmap $ancho, $alto
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][VentanaMedida]::PrintWindow($hwnd, $hdc, 3)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($destino, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "Captura (PrintWindow): $destino"
}

$base = Join-Path $raiz "logs\medida-$Nombre"
$out = "$base.out.log"; $err = "$base.err.log"
$proc = Start-Process -FilePath (Join-Path $raiz $Exe) `
    -ArgumentList 'juego\SCES_539.50' -WorkingDirectory $raiz -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $out -RedirectStandardError $err
$inicio = Get-Date
function Segundos() { ((Get-Date) - $inicio).TotalSeconds }
function Buscar([string]$patron) {
    if (-not (Test-Path $out)) { return $false }
    try { return [bool](Select-String -Path $out -Pattern $patron -SimpleMatch -Quiet) } catch { return $false }
}

$resumen = @()
try {
    if ($PerfilCarga) {
        while (-not (Buscar 'graph.outcome("InstantRace")') -and (Segundos) -lt $Limite -and -not $proc.HasExited) { Start-Sleep -Seconds 2 }
        Start-Sleep -Seconds 20
        if (-not $proc.HasExited) {
            & "$raiz\build\perfil.exe" $proc.Id 1500 $Intervalo | Out-File -Encoding utf8 "logs\perfil-$Nombre-carga.txt"
            $resumen += ("Perfil de la carga a los {0:N0} s" -f (Segundos))
        }
    }
    while (-not (Buscar 'graph.outcome("game")') -and (Segundos) -lt $Limite -and -not $proc.HasExited) { Start-Sleep -Seconds 2 }
    if (-not $proc.HasExited -and -not (Buscar 'graph.outcome("game")')) {
        # Sin carrera al llegar al limite: probablemente colgado. Dos pilas de todos los hilos, separadas 5 s, para ver
        # donde esta cada uno (la salida estandar va con bufer y pierde el final).
        $pila = Join-Path $raiz "logs\pila-$Nombre.txt"
        & "$raiz\build\pila.exe" $proc.Id | Out-File -Encoding utf8 $pila
        Start-Sleep -Seconds 5
        "`n===== 5 s despues =====`n" | Out-File -Encoding utf8 -Append $pila
        & "$raiz\build\pila.exe" $proc.Id | Out-File -Encoding utf8 -Append $pila
        $resumen += "Sin carrera a los $Limite s: pilas en $pila"
        $resumen += (Capturar $proc "$base-colgado.png")
    }
    $tGame = Segundos
    $resumen += ("game visto en el log a los {0:N0} s" -f $tGame)
    $pendientes = @($Capturas -split ',' | Where-Object { $_.Trim() } | ForEach-Object { [int]$_ } | Sort-Object)
    function CapturasPendientes() {
        while ($script:pendientes.Count -gt 0 -and (Segundos) -ge $tGame + $script:pendientes[0] -and -not $proc.HasExited) {
            $script:resumen += (Capturar $proc ("$base-{0}s.png" -f $script:pendientes[0]))
            $script:pendientes = @($script:pendientes | Select-Object -Skip 1)
        }
    }
    while ((Segundos) -lt $tGame + $PerfilTras -and -not $proc.HasExited) { CapturasPendientes; Start-Sleep -Seconds 1 }
    if (-not $proc.HasExited -and $Muestras -gt 0) {
        $resumen += ("Perfil en carrera desde los {0:N0} s" -f (Segundos))
        $hiloPerfil = if ($Hilo) { $Hilo } else { '0' }
        & "$raiz\build\perfil.exe" $proc.Id $Muestras $Intervalo $hiloPerfil | Out-File -Encoding utf8 "logs\perfil-$Nombre.txt"
    }
    while ((Segundos) -lt $tGame + $FinTras -and -not $proc.HasExited) { CapturasPendientes; Start-Sleep -Seconds 1 }
    if (-not $proc.HasExited) { $resumen += (Capturar $proc "$base.png") }
}
finally {
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Remove-Item $peticion -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 1
$texto = Get-Content $out -Raw
foreach ($m in [regex]::Matches($texto, '\[f1-06:(?:vif1fps|lua)\] t=[0-9.]+ (?:paquetes/s=[0-9.]+(?: modo=[0-9]+)?|graph\.outcome\("(?:InstantRace|Finished|game)"\))')) {
    $resumen += $m.Value
}
$resumen | Out-File -Encoding utf8 "$base.txt"
$resumen | Select-Object -Last 20
