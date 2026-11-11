# Recorta un rectangulo de una imagen y lo amplia sin suavizar (vecino mas proximo), para ver pixeles sueltos.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\recortar.ps1 -Entrada a.png -Salida b.png -X 0 -Y 0 -Ancho 100 -Alto 50 [-Escala 4]
param([string]$Entrada, [string]$Salida, [int]$X, [int]$Y, [int]$Ancho, [int]$Alto, [int]$Escala = 4)
Add-Type -AssemblyName System.Drawing
$origen = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Entrada))
Write-Output "Tamano: $($origen.Width)x$($origen.Height)"
$destino = New-Object System.Drawing.Bitmap ($Ancho * $Escala), ($Alto * $Escala)
$g = [System.Drawing.Graphics]::FromImage($destino)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($origen, (New-Object System.Drawing.Rectangle 0, 0, ($Ancho * $Escala), ($Alto * $Escala)),
    (New-Object System.Drawing.Rectangle $X, $Y, $Ancho, $Alto), [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$destino.Save($Salida, [System.Drawing.Imaging.ImageFormat]::Png)
$destino.Dispose(); $origen.Dispose()
