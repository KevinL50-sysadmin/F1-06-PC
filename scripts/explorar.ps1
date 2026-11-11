# Explora los menus del juego con un guion de pulsaciones (F1_GUION) y saca capturas relativas a los sucesos del juego
# (graph.outcome del front-end Lua). Sirve para probar modos, circuitos y la tarjeta de memoria sin estar delante.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\explorar.ps1 -Nombre X -Guion '...'
#        [-Capturas 'SinglePlayer+3,game+60,game#2+10'] [-Fin 'game+90'] [-Limite 400] [-Variables 'F1_X=1;...']
#        [-CadaSegundos 0] [-Tarjetas 'logs\tarjetas-prueba'] [-Exe '<ruta del .exe>'] [-Argumentos 'juego\SCES_539.50']
#        [-Directorio '<carpeta de trabajo>']
# -Exe: relativo a la raiz del proyecto o absoluto (p. ej. una copia del paquete portable en una carpeta de prueba).
# -Argumentos: linea de argumentos del .exe (por defecto juego\SCES_539.50, el modo carpeta). Una ISO con espacios va
#   entre comillas dobles: -Argumentos '"C:\Juegos\Mi juego.iso"'. -Argumentos '' arranca sin argumentos.
# -Ventana: la ventana del juego minimizada en segundo plano (por defecto, oculta).
# -Directorio: carpeta de trabajo del juego (relativa a la raiz del proyecto o absoluta); por defecto la raiz.
# -Tarjetas: carpeta con las tarjetas de pruebas (PS2X_TARJETAS; el juego usa <carpeta>\mc0 y <carpeta>\mc1). Si no
#   existe <carpeta>\mc0 se copia de juego\mc0 (la partida del usuario solo se lee). -Tarjetas '' deja PS2X_TARJETAS
#   sin poner: el juego usa sus propias tarjetas (partidas\ del paquete portable o del modo instalado; junto al ELF
#   en modo desarrollo).
# -Capturas: evento[#n]+segundos (la n-esima vez que sale graph.outcome("evento"), por defecto la primera), o
#   t+segundos (desde el arranque). Salen en logs\explorar-<Nombre>-<i>-<evento>.png.
# -Fin: cuando cerrar el juego (evento[#n]+segundos); si no llega, se cierra a los -Limite s del arranque.
# -CadaSegundos N: ademas, una captura cada N s (logs\explorar-<Nombre>-cada-<s>.png).
# Salida: logs\explorar-<Nombre>.txt con la secuencia de sucesos (t= del juego), las capturas y si se colgo; los logs
# del juego en logs\explorar-<Nombre>.out.log/.err.log. Sin sucesos nuevos durante 120 s (y sin -Fin cumplido) se
# toman pilas de los hilos (logs\pila-explorar-<Nombre>.txt) y se cierra.
param(
    [Parameter(Mandatory = $true)][string]$Nombre,
    [string]$Guion = '',
    [string]$Capturas = '',
    [string]$Fin = '',
    [int]$Limite = 400,
    [string]$Variables = '',
    [int]$CadaSegundos = 0,
    [int]$SinSucesos = 120,
    [string]$Tarjetas = 'logs\tarjetas-prueba',
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe',
    [string]$Argumentos = 'juego\SCES_539.50',
    [switch]$Ventana,
    [string]$Directorio = ''
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
if ($Guion) { $env:F1_GUION = $Guion } else { Remove-Item env:F1_GUION -ErrorAction SilentlyContinue }
Remove-Item env:F1_PULSAR -ErrorAction SilentlyContinue
$env:PS2X_BACKGROUND_WINDOW = '1'
$env:PS2X_SIN_MANDOS = '1' # no tocar los mandos ni el volante del usuario (=0 con -Variables para probarlos)
if ($Tarjetas) {
    $carpeta = [System.IO.Path]::GetFullPath((Join-Path $raiz $Tarjetas))
    foreach ($mc in 'mc0', 'mc1') {
        $destino = Join-Path $carpeta $mc
        if (-not (Test-Path $destino)) {
            New-Item -ItemType Directory -Path $destino -Force | Out-Null
            $origen = Join-Path $raiz "juego\$mc"
            if (Test-Path $origen) { Copy-Item -Path (Join-Path $origen '*') -Destination $destino -Recurse -Force }
        }
    }
    $env:PS2X_TARJETAS = $carpeta
} else {
    Remove-Item env:PS2X_TARJETAS -ErrorAction SilentlyContinue
}
foreach ($par in ($Variables -split ';')) {
    if ($par -match '^\s*([^=]+)=(.*)$') { Set-Item -Path ("env:" + $Matches[1].Trim()) -Value $Matches[2] }
}

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class VentanaExplorar {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
}
'@
$peticion = Join-Path $raiz ("logs\captura-{0}.peticion" -f $PID)
Remove-Item $peticion -ErrorAction SilentlyContinue
$env:PS2X_CAPTURA_PETICION = $peticion
function Capturar([System.Diagnostics.Process]$proc, [string]$destino) {
    Remove-Item $destino -ErrorAction SilentlyContinue
    [System.IO.File]::WriteAllText($peticion, $destino)
    $limiteCaptura = (Get-Date).AddSeconds(5)
    while ((Test-Path $peticion) -and (Get-Date) -lt $limiteCaptura -and -not $proc.HasExited) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $peticion) -and (Test-Path $destino)) { return "Captura: $destino" }
    Remove-Item $peticion -ErrorAction SilentlyContinue
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { return "Sin ventana para capturar: $destino" }
    $r = New-Object VentanaExplorar+RECT
    [void][VentanaExplorar]::GetClientRect($hwnd, [ref]$r)
    $ancho = [Math]::Max(1, $r.Right - $r.Left); $alto = [Math]::Max(1, $r.Bottom - $r.Top)
    $bmp = New-Object System.Drawing.Bitmap $ancho, $alto
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][VentanaExplorar]::PrintWindow($hwnd, $hdc, 3)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($destino, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "Captura (PrintWindow): $destino"
}

# evento[#n]+segundos -> objeto; 't' es el arranque.
function Especificacion([string]$texto) {
    if ($texto -notmatch '^\s*([^#+]+)(?:#(\d+))?\+([0-9.]+)\s*$') { throw "Especificacion no valida: $texto" }
    [pscustomobject]@{ Texto = $texto.Trim(); Evento = $Matches[1]; N = $(if ($Matches[2]) { [int]$Matches[2] } else { 1 }); Retraso = [double]$Matches[3]; Hecha = $false }
}
$especificaciones = @($Capturas -split ',' | Where-Object { $_.Trim() } | ForEach-Object { Especificacion $_ })
$especFin = if ($Fin) { Especificacion $Fin } else { $null }

$base = Join-Path $raiz "logs\explorar-$Nombre"
$out = "$base.out.log"; $err = "$base.err.log"
$trabajo = if (-not $Directorio) { $raiz } elseif ([System.IO.Path]::IsPathRooted($Directorio)) { $Directorio } `
    else { [System.IO.Path]::GetFullPath((Join-Path $raiz $Directorio)) }
$rutaExe = if ([System.IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $raiz $Exe }
# -Ventana: the game's window exists (minimized, in the background) instead of hidden.
$estilo = if ($Ventana) { 'Minimized' } else { 'Hidden' }
$opciones = @{ FilePath = $rutaExe; WorkingDirectory = $trabajo; PassThru = $true; WindowStyle = $estilo
    RedirectStandardOutput = $out; RedirectStandardError = $err }
if ($Argumentos) { $opciones.ArgumentList = $Argumentos }
$proc = Start-Process @opciones
$inicio = Get-Date
function Segundos() { ((Get-Date) - $inicio).TotalSeconds }
# Momento (segundos del script) en que se vio cada suceso por n-esima vez.
$vistos = @{}
$sucesos = New-Object System.Collections.Generic.List[string]
$leidos = 0
$ultimoSuceso = 0.0
function Leer() {
    if (-not (Test-Path $out)) { return }
    # El juego tiene el archivo abierto para escribir: hay que abrirlo con lectura y escritura compartidas.
    try {
        $fs = New-Object System.IO.FileStream($out, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $sr = New-Object System.IO.StreamReader($fs)
        $texto = $sr.ReadToEnd()
        $sr.Close()
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
function Momento($e) {
    if ($e.Evento -eq 't') { return $e.Retraso }
    $clave = "{0}#{1}" -f $e.Evento, $e.N
    if ($vistos.ContainsKey($clave)) { return $vistos[$clave] + $e.Retraso }
    return $null
}

$resumen = New-Object System.Collections.Generic.List[string]
$siguienteCada = $CadaSegundos
$indice = 0
try {
    while (-not $proc.HasExited -and (Segundos) -lt $Limite) {
        Leer
        foreach ($e in $especificaciones) {
            if ($e.Hecha) { continue }
            $m = Momento $e
            if ($m -ne $null -and (Segundos) -ge $m) {
                $e.Hecha = $true
                $indice++
                $nombreArchivo = "{0}-{1:D2}-{2}.png" -f $base, $indice, ($e.Texto -replace '[^A-Za-z0-9#+.]', '_' -replace '[#+]', '_')
                $resumen.Add("$($e.Texto): " + (Capturar $proc $nombreArchivo))
            }
        }
        if ($CadaSegundos -gt 0 -and (Segundos) -ge $siguienteCada) {
            $resumen.Add((Capturar $proc ("{0}-cada-{1:D4}.png" -f $base, [int]$siguienteCada)))
            $siguienteCada += $CadaSegundos
        }
        if ($especFin) {
            $m = Momento $especFin
            if ($m -ne $null -and (Segundos) -ge $m) { $resumen.Add("Fin: $($especFin.Texto)"); break }
        }
        if ((Segundos) -gt 60 -and (Segundos) - $ultimoSuceso -gt $SinSucesos) {
            $pila = Join-Path $raiz "logs\pila-explorar-$Nombre.txt"
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
$todo = @('Sucesos:') + $sucesos + @('', 'Capturas y final:') + $resumen
$todo | Out-File -Encoding utf8 "$base.txt"
$todo | Select-Object -Last 40
