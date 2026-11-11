# Junta varias capturas en un mosaico (cada una a la mitad de tamaño, 3 por fila) con su nombre encima.
# Uso: powershell -File mosaico.ps1 -Salida mosaico.png -Imagenes a.png,b.png,...
param([string]$Salida, [string[]]$Imagenes)
# Con powershell -File la lista llega como una sola cadena "a.png,b.png": se parte aqui.
$Imagenes = @($Imagenes | ForEach-Object { $_ -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
Add-Type -AssemblyName System.Drawing
$columnas = 3
$ancho = 320; $alto = 224; $titulo = 16
$filas = [Math]::Ceiling($Imagenes.Count / $columnas)
$bmp = New-Object System.Drawing.Bitmap ($ancho * $columnas), (($alto + $titulo) * $filas)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::Black)
$fuente = New-Object System.Drawing.Font 'Arial', 9
for ($i = 0; $i -lt $Imagenes.Count; $i++) {
    $x = ($i % $columnas) * $ancho
    $y = [Math]::Floor($i / $columnas) * ($alto + $titulo)
    $img = [System.Drawing.Image]::FromFile($Imagenes[$i])
    $g.DrawImage($img, $x, $y + $titulo, $ancho, $alto)
    $img.Dispose()
    $g.DrawString([System.IO.Path]::GetFileNameWithoutExtension($Imagenes[$i]), $fuente, [System.Drawing.Brushes]::Yellow, $x + 2, $y + 1)
}
$g.Dispose()
$bmp.Save($Salida, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"Mosaico: $Salida"
