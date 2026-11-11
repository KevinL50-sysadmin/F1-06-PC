# Hace las imagenes del instalador (Inno Setup) a partir de la portada de F1 06 de PS2 (la de Fernando Alonso con el
# Renault), retocada como si fuera la caja de la version de PC: la franja de "PlayStation 2" pasa a ser una banda
# "PC DVD-ROM" y se borran la etiqueta PAL, el PEGI y el logo de Sony (se rellenan con lo que tienen alrededor).
# La portada no es nuestra: no se sube al repositorio. El script la descarga de LaunchBox Games Database (eleccion del
# usuario, 2026-10-28) en build\instalador-imagen\origen y deja todo lo generado en build\instalador-imagen:
#   portada-pc.png                      la portada retocada (500x715, como la original)
#   asistente-<ancho>x<alto>.bmp        la imagen grande del asistente (WizardImageFile), en los 7 tamanos de escala
#   asistente-pequena-<ancho>x<alto>.bmp la pequena (WizardSmallImageFile), con el icono del juego si existe
#                                       (build\icono\F1-06.png, de scripts\hacer-icono.ps1)
#   control.png                         la portada con las zonas que se rellenan marcadas (para revisar)
# empaquetar.ps1 -Instalador las usa si existen.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hacer-imagen-instalador.ps1 [-Portada 'ruta.jpg']
param([string]$Portada = '')
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
$salida = Join-Path $raiz 'build\instalador-imagen'
$origen = Join-Path $salida 'origen'
New-Item -ItemType Directory -Force -Path $origen | Out-Null
$urlPortada = 'https://images.launchbox-app.com/51c8179b-af12-4b54-bd73-1fffd597805a.jpg'
$urlLogo = 'https://images.launchbox-app.com/1c4e9509-6d47-4e83-93f2-ae1e4de4ccb6.png'
if (-not $Portada) {
    $Portada = Join-Path $origen 'portada-ps2-alonso.jpg'
    if (-not (Test-Path $Portada)) { Invoke-WebRequest -UseBasicParsing -Uri $urlPortada -OutFile $Portada }
}
$logo = Join-Path $origen 'logo-f1-06.png'
if (-not (Test-Path $logo)) { Invoke-WebRequest -UseBasicParsing -Uri $urlLogo -OutFile $logo }
$icono = Join-Path $raiz 'build\icono\F1-06.png'

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Runtime.InteropServices;

public static class ImagenInstalador
{
    static float[] Leer(Bitmap b)
    {
        int w = b.Width, h = b.Height;
        var d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        var p = new byte[w * h * 4];
        for (int y = 0; y < h; y++) Marshal.Copy(d.Scan0 + y * d.Stride, p, y * w * 4, w * 4);
        b.UnlockBits(d);
        var f = new float[w * h * 3];
        for (int i = 0; i < w * h; i++) { f[i * 3] = p[i * 4]; f[i * 3 + 1] = p[i * 4 + 1]; f[i * 3 + 2] = p[i * 4 + 2]; }
        return f;
    }

    static void Escribir(Bitmap b, float[] f)
    {
        int w = b.Width, h = b.Height;
        var p = new byte[w * h * 4];
        for (int i = 0; i < w * h; i++)
        {
            for (int c = 0; c < 3; c++) p[i * 4 + c] = (byte)Math.Max(0, Math.Min(255, (int)Math.Round(f[i * 3 + c])));
            p[i * 4 + 3] = 255;
        }
        var d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        for (int y = 0; y < h; y++) Marshal.Copy(p, y * w * 4, d.Scan0 + y * d.Stride, w * 4);
        b.UnlockBits(d);
    }

    // Rellena cada rectangulo con lo que tiene alrededor: difusion (cada pixel del hueco pasa a ser la media de sus
    // vecinos hasta que se estabiliza, partiendo del borde) y un grano suave copiado de la zona de encima para que no
    // quede liso.
    public static void Rellenar(Bitmap b, Rectangle[] huecos)
    {
        int w = b.Width, h = b.Height;
        var f = Leer(b);
        var dentro = new bool[w * h];
        foreach (var r in huecos)
            for (int y = Math.Max(0, r.Top); y < Math.Min(h, r.Bottom); y++)
                for (int x = Math.Max(0, r.Left); x < Math.Min(w, r.Right); x++) dentro[y * w + x] = true;
        // Arranque: cada pixel del hueco con la media de su fila en el borde (acelera la convergencia).
        for (int i = 0; i < w * h; i++) if (dentro[i]) { f[i * 3] = f[i * 3 + 1] = f[i * 3 + 2] = 128; }
        var g = (float[])f.Clone();
        for (int it = 0; it < 4000; it++)
        {
            for (int y = 1; y < h - 1; y++)
                for (int x = 1; x < w - 1; x++)
                {
                    int i = y * w + x;
                    if (!dentro[i]) continue;
                    for (int c = 0; c < 3; c++)
                        g[i * 3 + c] = 0.25f * (f[(i - 1) * 3 + c] + f[(i + 1) * 3 + c] + f[(i - w) * 3 + c] + f[(i + w) * 3 + c]);
                }
            var t = f; f = g; g = t;
            for (int i = 0; i < w * h; i++) if (!dentro[i]) { g[i * 3] = f[i * 3]; g[i * 3 + 1] = f[i * 3 + 1]; g[i * 3 + 2] = f[i * 3 + 2]; }
        }
        // Grano: la diferencia entre la zona de encima del hueco y su version suavizada, atenuada.
        var rnd = new Random(1234);
        foreach (var r in huecos)
            for (int y = Math.Max(1, r.Top); y < Math.Min(h - 1, r.Bottom); y++)
                for (int x = Math.Max(1, r.Left); x < Math.Min(w - 1, r.Right); x++)
                {
                    int i = y * w + x;
                    float n = (float)(rnd.NextDouble() - 0.5) * 6f;
                    for (int c = 0; c < 3; c++) f[i * 3 + c] += n;
                }
        Escribir(b, f);
    }

    static GraphicsPath Redondeado(RectangleF r, float radio)
    {
        var p = new GraphicsPath();
        float d = radio * 2;
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    // La banda de arriba de las cajas de PC de la epoca: negra, con el distintivo "PC" (rectangulo blanco redondeado
    // con PC en negro) a la izquierda y "DVD-ROM" a la derecha. alto = alto de la banda.
    public static void Banda(Graphics g, float ancho, float alto)
    {
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        using (var negro = new SolidBrush(Color.Black)) g.FillRectangle(negro, 0, 0, ancho, alto);
        using (var linea = new LinearGradientBrush(new PointF(0, 0), new PointF(ancho, 0), Color.FromArgb(70, 70, 70), Color.FromArgb(200, 200, 200)))
            g.FillRectangle(linea, 0, alto - alto * 0.04f, ancho, alto * 0.04f);
        float m = alto * 0.18f;
        var caja = new RectangleF(m, m, alto * 1.25f, alto - 2 * m - alto * 0.04f);
        using (var blanco = new SolidBrush(Color.White)) using (var p = Redondeado(caja, caja.Height * 0.16f)) g.FillPath(blanco, p);
        var centro = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        using (var f = new Font("Arial Black", caja.Height * 0.62f, FontStyle.Bold, GraphicsUnit.Pixel))
        using (var tinta = new SolidBrush(Color.Black))
            g.DrawString("PC", f, tinta, new RectangleF(caja.X, caja.Y + caja.Height * 0.03f, caja.Width, caja.Height), centro);
        var derecha = new StringFormat { Alignment = StringAlignment.Far, LineAlignment = StringAlignment.Center };
        using (var f = new Font("Arial", alto * 0.36f, FontStyle.Bold, GraphicsUnit.Pixel))
        using (var plata = new LinearGradientBrush(new RectangleF(0, 0, ancho, alto), Color.FromArgb(235, 235, 235), Color.FromArgb(150, 150, 150), LinearGradientMode.Vertical))
            g.DrawString("DVD-ROM", f, plata, new RectangleF(0, 0, ancho - m * 1.2f, alto - alto * 0.04f), derecha);
    }

    public static Bitmap Escalar(Image origen, RectangleF fuente, int ancho, int alto)
    {
        var b = new Bitmap(ancho, alto, PixelFormat.Format24bppRgb);
        using (var g = Graphics.FromImage(b))
        {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.CompositingQuality = CompositingQuality.HighQuality;
            using (var ia = new ImageAttributes()) { ia.SetWrapMode(WrapMode.TileFlipXY);
                g.DrawImage(origen, new Rectangle(0, 0, ancho, alto), fuente.X, fuente.Y, fuente.Width, fuente.Height, GraphicsUnit.Pixel, ia); }
        }
        return b;
    }
}
'@

$cover = [System.Drawing.Bitmap]::new($Portada)
if ($cover.Width -ne 500 -or $cover.Height -ne 715) { throw "La portada esperada es de 500x715 ($($cover.Width)x$($cover.Height))" }
$pc = New-Object System.Drawing.Bitmap 500, 715, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($pc); $g.DrawImage($cover, 0, 0, 500, 715); $g.Dispose()
$cover.Dispose()

# Zonas a borrar (medidas sobre la portada de 500x715): la etiqueta PAL bajo la franja, el PEGI y el logo de Sony.
$alturaBanda = 101
$huecos = @(
    [System.Drawing.Rectangle]::new(398, 101, 78, 22),   # PAL
    [System.Drawing.Rectangle]::new(12, 576, 76, 96),    # PEGI 3+
    [System.Drawing.Rectangle]::new(402, 582, 78, 90)    # SONY / Computer Entertainment
)
$control = $pc.Clone()
$gc = [System.Drawing.Graphics]::FromImage($control)
$rojo = New-Object System.Drawing.Pen ([System.Drawing.Color]::Red), 2
foreach ($r in $huecos) { $gc.DrawRectangle($rojo, $r) }
$gc.DrawLine($rojo, 0, $alturaBanda, 500, $alturaBanda)
$gc.Dispose(); $control.Save((Join-Path $salida 'control.png'), [System.Drawing.Imaging.ImageFormat]::Png); $control.Dispose()

[ImagenInstalador]::Rellenar($pc, $huecos)
$g = [System.Drawing.Graphics]::FromImage($pc)
[ImagenInstalador]::Banda($g, 500, $alturaBanda)
$g.Dispose()
$pc.Save((Join-Path $salida 'portada-pc.png'), [System.Drawing.Imaging.ImageFormat]::Png)

# Imagen grande del asistente: vertical (164x314 al 100 %). Se hace a 410x797 (250 %) y se reduce para el resto.
# Arriba la banda PC, debajo el logo limpio (el de la portada no cabe a lo ancho) sobre un degradado oscuro, y el
# coche de la portada (recorte vertical centrado en el casco de Alonso, desde debajo del logo de la portada para que
# no salga dos veces).
$tamanos = @(@(164, 314), @(192, 386), @(246, 459), @(273, 556), @(328, 604), @(355, 700), @(410, 797))
$W = 410; $H = 797
$maestra = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($maestra)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$bandaW = [int]($H * 0.105)
$inicioFondo = 250 # debajo del logo de la portada
$fuenteAlto = 715 - $inicioFondo
$fuenteAncho = $fuenteAlto * $W / ($H - $bandaW)
$centroX = 300
$x0 = [math]::Max(0, [math]::Min(500 - $fuenteAncho, $centroX - $fuenteAncho / 2))
$g.DrawImage($pc, (New-Object System.Drawing.RectangleF 0, $bandaW, $W, ($H - $bandaW)),
    (New-Object System.Drawing.RectangleF $x0, $inicioFondo, $fuenteAncho, $fuenteAlto), [System.Drawing.GraphicsUnit]::Pixel)
$oscuro = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, $bandaW),
    (New-Object System.Drawing.PointF 0, ($bandaW + $H * 0.36)), ([System.Drawing.Color]::FromArgb(200, 0, 0, 0)), ([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$oscuro.WrapMode = [System.Drawing.Drawing2D.WrapMode]::TileFlipXY # sin la linea del borde al repetirse
$g.FillRectangle($oscuro, 0, $bandaW, $W, $H * 0.36)
$imLogo = [System.Drawing.Image]::FromFile($logo)
$anchoLogo = $W * 0.88; $altoLogo = $anchoLogo * $imLogo.Height / $imLogo.Width
$g.DrawImage($imLogo, [single](($W - $anchoLogo) / 2), [single]($bandaW + $H * 0.05), [single]$anchoLogo, [single]$altoLogo)
$imLogo.Dispose()
[ImagenInstalador]::Banda($g, $W, $bandaW)
$g.Dispose()
$maestra.Save((Join-Path $salida 'asistente-maestra.png'), [System.Drawing.Imaging.ImageFormat]::Png)
foreach ($t in $tamanos) {
    $b = [ImagenInstalador]::Escalar($maestra, (New-Object System.Drawing.RectangleF 0, 0, $W, $H), $t[0], $t[1])
    $b.Save((Join-Path $salida ("asistente-{0}x{1}.bmp" -f $t[0], $t[1])), [System.Drawing.Imaging.ImageFormat]::Bmp); $b.Dispose()
}
$maestra.Dispose()

# Imagen pequena (55x55 al 100 %): el icono del juego si existe; si no, la "F1" del logo sobre negro.
$pequenas = @(@(55, 55), @(64, 68), @(83, 80), @(92, 97), @(110, 106), @(119, 123), @(138, 140))
if (Test-Path $icono) {
    $im = [System.Drawing.Image]::FromFile($icono)
    foreach ($t in $pequenas) {
        $b = New-Object System.Drawing.Bitmap $t[0], $t[1], ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $g = [System.Drawing.Graphics]::FromImage($b); $g.Clear([System.Drawing.Color]::White)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $lado = [math]::Min($t[0], $t[1])
        $g.DrawImage($im, [single](($t[0] - $lado) / 2), [single](($t[1] - $lado) / 2), [single]$lado, [single]$lado)
        $g.Dispose()
        $b.Save((Join-Path $salida ("asistente-pequena-{0}x{1}.bmp" -f $t[0], $t[1])), [System.Drawing.Imaging.ImageFormat]::Bmp); $b.Dispose()
    }
    $im.Dispose()
} else { Write-Host "Sin build\icono\F1-06.png: no se hace la imagen pequena (scripts\hacer-icono.ps1)" }
$pc.Dispose()
Write-Host "Listo: $salida"
