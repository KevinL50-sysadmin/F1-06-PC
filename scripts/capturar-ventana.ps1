# Arranca el juego recompilado, espera, captura SOLO su ventana (PrintWindow) y lo cierra.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capturar-ventana.ps1 [-Segundos 50] [-Salida logs\captura.png]
#        [-Pulsar '105:cruz'] [-Extra '110,125']
# -Extra: capturas adicionales (segundos desde el arranque, antes de -Segundos). Se guardan junto a -Salida con el
# segundo en el nombre, p. ej. logs\captura-110s.png. La de -Segundos es la ultima y cierra el juego.
# Por defecto la ventana va en segundo plano (PS2X_BACKGROUND_WINDOW=1: fuera de la pantalla, sin foco, no tapa
# nada). -Visible la muestra normal.
# -Ventana: captura la ventana tal como se ve (con el filtro de la imagen y el contador de FPS) en vez del fotograma
# del juego: el juego guarda la ventana (PS2X_CAPTURA_VENTANA=1). -PrintWindow: la captura siempre con PrintWindow
# (para comparar con -Ventana).
param(
    [int]$Segundos = 50,
    [string]$Salida = 'logs\captura.png',
    [string]$Pulsar = '',  # pulsaciones programadas para el juego, p. ej. '105:cruz,115:abajo:0.1' (ver F1_PULSAR)
    [string]$Extra = '',
    [switch]$Visible,
    [switch]$Ventana,
    [switch]$PrintWindow
)
if ($Ventana) { $env:PS2X_CAPTURA_VENTANA = '1' }
if ($Pulsar) { $env:F1_PULSAR = $Pulsar }
if (-not $Visible) { $env:PS2X_BACKGROUND_WINDOW = '1'; $env:PS2X_SIN_MANDOS = '1' } # sin tocar los mandos del usuario
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Ventana {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
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
    if (-not $PrintWindow) {
        [System.IO.File]::WriteAllText($peticion, $destino)
        $limite = (Get-Date).AddSeconds(5)
        while ((Test-Path $peticion) -and (Get-Date) -lt $limite -and -not $proc.HasExited) { Start-Sleep -Milliseconds 100 }
        if (-not (Test-Path $peticion) -and (Test-Path $destino)) { return "Captura (del juego): $destino" }
    }
    Remove-Item $peticion -ErrorAction SilentlyContinue
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw 'El juego no tiene ventana (se ha cerrado?)' }
    $r = New-Object Ventana+RECT
    [void][Ventana]::GetClientRect($hwnd, [ref]$r)
    $ancho = [Math]::Max(1, $r.Right - $r.Left); $alto = [Math]::Max(1, $r.Bottom - $r.Top)
    $bmp = New-Object System.Drawing.Bitmap $ancho, $alto
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    # 1 = PW_CLIENTONLY, 2 = PW_RENDERFULLCONTENT (necesario para ventanas OpenGL/DirectX)
    $ok = [Ventana]::PrintWindow($hwnd, $hdc, 3)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($destino, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $v = New-Object Ventana+RECT
    [void][Ventana]::GetWindowRect($hwnd, [ref]$v)
    "Captura ($ancho x $alto, PrintWindow=$ok, ventana en $($v.Left),$($v.Top)): $destino"
}

$log = Join-Path $raiz ('logs\captura-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
# En segundo plano, la consola del juego (es una aplicacion de consola) tambien se oculta.
$estilo = if ($Visible) { 'Normal' } else { 'Hidden' }
$proc = Start-Process -FilePath (Join-Path $raiz 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe') `
    -ArgumentList 'juego\SCES_539.50' -WorkingDirectory $raiz -PassThru -WindowStyle $estilo `
    -RedirectStandardOutput "$log.out.log" -RedirectStandardError "$log.err.log"
$inicio = Get-Date
try {
    $destino = Join-Path $raiz $Salida
    $base = [System.IO.Path]::Combine([System.IO.Path]::GetDirectoryName($destino), [System.IO.Path]::GetFileNameWithoutExtension($destino))
    $extras = @()
    if ($Extra) { $extras = $Extra.Split(',') | ForEach-Object { [double]$_ } | Where-Object { $_ -lt $Segundos } | Sort-Object }
    foreach ($s in $extras) {
        $espera = $s - ((Get-Date) - $inicio).TotalSeconds
        if ($espera -gt 0) { Start-Sleep -Milliseconds ([int]($espera * 1000)) }
        Capturar $proc ("{0}-{1}s.png" -f $base, $s)
    }
    $espera = $Segundos - ((Get-Date) - $inicio).TotalSeconds
    if ($espera -gt 0) { Start-Sleep -Milliseconds ([int]($espera * 1000)) }
    Capturar $proc $destino
    "Log: $log.out.log / $log.err.log"
}
finally {
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Remove-Item $peticion -ErrorAction SilentlyContinue
}
