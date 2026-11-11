# Hace el icono de Windows del juego (build\icono\F1-06.ico) con el logo original de F1 que el juego dibuja arriba a
# la derecha de los menus. El logo es del juego: no se sube al repositorio; este script lo saca de los datos del
# usuario (juego\, modo desarrollo) cada vez que haga falta.
# Origen exacto: la textura del logo de los menus (Frontend/Textures/F1_logo.pct, dentro de los .WAD), tal como esta
# en la VRAM del GS mientras se ve el menu RACE: 128x64, PSMT8 (256 colores) con su paleta CT32 y transparencia
# (tbw=2). Sus direcciones cambian de una partida a otra, asi que se busca por su forma en el historial del GS
# (F1_VOLCAR_TEX=dibujo:2:19:128:64 de src\f1_overrides.cpp, con F1_VOLCAR_GS). Para llegar al menu, el mismo guion de
# scripts\explorar.ps1 que en las pruebas (carga el perfil de la tarjeta de pruebas, copia de juego\mc0).
# El juego guarda el alfa de 0 a 128; f1_overrides lo vuelca doblado (0-255) en gs_tex<tbp>-<s>-alfa.png.
# Icono: el logo recortado a su contenido, centrado en un cuadrado redondeado azul oscuro (el de la cabecera de los
# menus: el F1 es blanco y no se veria sobre el fondo claro del Explorador; -SinFondo lo deja transparente), en 16,
# 24, 32, 48, 64, 128 y 256 px (bicubico de alta calidad; la de 256 en PNG dentro del .ico, las demas en BMP de 32
# bits). Ademas, build\icono\F1-06.png (vista previa de 256 px) y la textura volcada (textura-color.png y
# textura-alfa.png).
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hacer-icono.ps1 [-Textura 'logs\gs_tex14724-141']
#        [-SinFondo] [-Exe 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe']
# -Textura: usa un volcado ya hecho (sin -alfa.png ni .png) en vez de arrancar el juego (unos 3 minutos).
param(
    [string]$Textura = '',
    [switch]$SinFondo,
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe'
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
Add-Type -AssemblyName System.Drawing
$salida = Join-Path $raiz 'build\icono'
if (-not (Test-Path $salida)) { New-Item -ItemType Directory -Path $salida | Out-Null }
$tamanos = 16, 24, 32, 48, 64, 128, 256

# 1. La textura del logo: color (alfa a 255) y alfa (en gris).
if (-not $Textura) {
    if (-not (Test-Path (Join-Path $raiz 'juego\SCES_539.50'))) { throw 'Falta juego\SCES_539.50 (los datos del juego)' }
    # Volcados a varios segundos desde el arranque: el menu RACE sale a los ~130 s con este guion y se queda.
    $segundos = 140, 141, 150, 151, 160
    Get-ChildItem (Join-Path $raiz 'logs') -Filter 'gs_tex*' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^gs_tex\d+-(\d+)' -and [int]$Matches[1] -in $segundos } | Remove-Item -Force
    $guion = 'pad+3:cruz:0.5,pad+8:cruz:0.5,Continue#3+1.5:start:0.5,LOADfromDir+4:cruz:0.5,' +
        'SerializeSucceeded+4:cruz:0.5,NoInGameAtBoot+4:cruz:0.5'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'explorar.ps1') -Nombre icono `
        -Guion $guion -Fin 'SinglePlayer+35' -Limite 240 -Exe $Exe `
        -Variables ("F1_VOLCAR_GS={0};F1_VOLCAR_TEX=dibujo:2:19:128:64" -f ($segundos -join ',')) | Out-Null
    # El ultimo volcado con un logo de verdad (bastantes pixeles opacos).
    foreach ($s in ($segundos | Sort-Object -Descending)) {
        $candidato = Get-ChildItem (Join-Path $raiz 'logs') -Filter "gs_tex*-$s-alfa.png" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $candidato) { continue }
        $prueba = [System.Drawing.Bitmap]::FromFile($candidato.FullName)
        $opacos = 0
        for ($y = 0; $y -lt $prueba.Height; $y++) { for ($x = 0; $x -lt $prueba.Width; $x++) { if ($prueba.GetPixel($x, $y).R -ge 128) { $opacos++ } } }
        $prueba.Dispose()
        if ($opacos -ge 500) { $Textura = $candidato.FullName -replace '-alfa\.png$', ''; break }
    }
    if (-not $Textura) { throw 'No se encontro el logo en los volcados (ver logs\explorar-icono.txt y .err.log)' }
}
$rutaColor = [System.IO.Path]::GetFullPath("$Textura.png")
$rutaAlfa = [System.IO.Path]::GetFullPath("$Textura-alfa.png")
foreach ($r in $rutaColor, $rutaAlfa) { if (-not (Test-Path $r)) { throw "Falta $r" } }
Write-Host "Textura: $rutaColor"

# 2. Una imagen RGBA con el alfa del volcado, recortada a lo que no es transparente.
$color = [System.Drawing.Bitmap]::FromFile($rutaColor)
$alfa = [System.Drawing.Bitmap]::FromFile($rutaAlfa)
$ancho = $color.Width; $alto = $color.Height
$logo = New-Object System.Drawing.Bitmap $ancho, $alto, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$minX = $ancho; $minY = $alto; $maxX = -1; $maxY = -1
for ($y = 0; $y -lt $alto; $y++) {
    for ($x = 0; $x -lt $ancho; $x++) {
        $a = $alfa.GetPixel($x, $y).R
        $c = $color.GetPixel($x, $y)
        $logo.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($a, $c.R, $c.G, $c.B))
        if ($a -ge 16) {
            if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
        }
    }
}
$color.Save((Join-Path $salida 'textura-color.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$alfa.Save((Join-Path $salida 'textura-alfa.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$color.Dispose(); $alfa.Dispose()
if ($maxX -lt 0) { throw 'La textura es transparente entera' }
$recorte = New-Object System.Drawing.Rectangle $minX, $minY, ($maxX - $minX + 1), ($maxY - $minY + 1)
Write-Host ("Logo: {0}x{1} px en ({2},{3}) de la textura de {4}x{5}" -f $recorte.Width, $recorte.Height, $minX, $minY, $ancho, $alto)

# 3. Cada tamano: fondo redondeado (salvo -SinFondo) y el logo centrado, sin deformar.
function Dibujar([int]$lado) {
    $bmp = New-Object System.Drawing.Bitmap $lado, $lado, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $margen = 0.0
    if (-not $SinFondo) {
        $radio = [Math]::Max(2.0, $lado * 0.18)
        $camino = New-Object System.Drawing.Drawing2D.GraphicsPath
        $d = $radio * 2; $l = $lado - 0.5
        $camino.AddArc(0, 0, $d, $d, 180, 90); $camino.AddArc($l - $d, 0, $d, $d, 270, 90)
        $camino.AddArc($l - $d, $l - $d, $d, $d, 0, 90); $camino.AddArc(0, $l - $d, $d, $d, 90, 90)
        $camino.CloseFigure()
        $pincel = New-Object System.Drawing.Drawing2D.LinearGradientBrush((New-Object System.Drawing.PointF 0, 0),
            (New-Object System.Drawing.PointF 0, $lado), [System.Drawing.Color]::FromArgb(255, 22, 52, 96),
            [System.Drawing.Color]::FromArgb(255, 6, 18, 40))
        $g.FillPath($pincel, $camino)
        $pincel.Dispose(); $camino.Dispose()
        $margen = $lado * 0.08
    }
    $hueco = $lado - 2 * $margen
    $escala = [Math]::Min($hueco / $recorte.Width, $hueco / $recorte.Height)
    $w = $recorte.Width * $escala; $h = $recorte.Height * $escala
    $atributos = New-Object System.Drawing.Imaging.ImageAttributes
    $atributos.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY) # sin bordes oscuros al ampliar
    $g.DrawImage($logo, (New-Object System.Drawing.Rectangle ([int][Math]::Round(($lado - $w) / 2)),
        ([int][Math]::Round(($lado - $h) / 2)), ([int][Math]::Round($w)), ([int][Math]::Round($h))),
        $recorte.X, $recorte.Y, $recorte.Width, $recorte.Height, [System.Drawing.GraphicsUnit]::Pixel, $atributos)
    $atributos.Dispose(); $g.Dispose()
    $bmp
}

# 4. El .ico: cabecera, una entrada por tamano y las imagenes (BMP de 32 bits abajo-arriba con su mascara AND vacia;
# la de 256 en PNG).
$imagenes = New-Object System.Collections.Generic.List[byte[]]
foreach ($lado in $tamanos) {
    $bmp = Dibujar $lado
    if ($lado -eq 256) {
        $bmp.Save((Join-Path $salida 'F1-06.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $imagenes.Add($ms.ToArray()); $ms.Dispose()
    } else {
        $datos = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $lado, $lado),
            [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $fila = $lado * 4
        $pixeles = New-Object byte[] ($datos.Stride * $lado)
        [System.Runtime.InteropServices.Marshal]::Copy($datos.Scan0, $pixeles, 0, $pixeles.Length)
        $bmp.UnlockBits($datos)
        $filaMascara = [int]([Math]::Ceiling($lado / 32.0) * 4)
        $ms = New-Object System.IO.MemoryStream
        $bw = New-Object System.IO.BinaryWriter $ms
        $bw.Write([uint32]40); $bw.Write([int32]$lado); $bw.Write([int32]($lado * 2)); $bw.Write([uint16]1)
        $bw.Write([uint16]32); $bw.Write([uint32]0); $bw.Write([uint32]($fila * $lado + $filaMascara * $lado))
        $bw.Write([int32]0); $bw.Write([int32]0); $bw.Write([uint32]0); $bw.Write([uint32]0)
        for ($y = $lado - 1; $y -ge 0; $y--) { $bw.Write($pixeles, $y * $datos.Stride, $fila) }
        $bw.Write((New-Object byte[] ($filaMascara * $lado)))
        $bw.Flush(); $imagenes.Add($ms.ToArray()); $bw.Dispose()
    }
    $bmp.Dispose()
}
$logo.Dispose()
$rutaIco = Join-Path $salida 'F1-06.ico'
$fs = [System.IO.File]::Create($rutaIco)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$tamanos.Count)
$desplazamiento = 6 + 16 * $tamanos.Count
for ($i = 0; $i -lt $tamanos.Count; $i++) {
    $lado = $tamanos[$i]
    $bw.Write([byte]($lado % 256)); $bw.Write([byte]($lado % 256)); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]$imagenes[$i].Length); $bw.Write([uint32]$desplazamiento)
    $desplazamiento += $imagenes[$i].Length
}
foreach ($img in $imagenes) { $bw.Write($img) }
$bw.Dispose()
Write-Host ("Listo: {0} ({1:N0} bytes; {2} px) y {3}" -f $rutaIco, (Get-Item $rutaIco).Length, ($tamanos -join ', '),
    (Join-Path $salida 'F1-06.png'))
