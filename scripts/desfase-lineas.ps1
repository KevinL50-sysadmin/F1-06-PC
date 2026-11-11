# Mide el desfase vertical de una captura frente a otra de referencia en varias zonas: para cada zona, la media de la
# diferencia absoluta por canal con la captura desplazada dy filas (-3..3) y el dy con la menor (B(y + dy) ~ A(y):
# dy = -1 es la escena de B una linea mas arriba que la de A).
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\desfase-lineas.ps1 -A ref.png -B otra.png
#        [-Zonas 'escena3d:260,40,240,70;hud:30,120,220,320']   (nombre:x,y,ancho,alto)
param([Parameter(Mandatory = $true)][string]$A, [Parameter(Mandatory = $true)][string]$B,
    [string]$Zonas = 'escena3d:260,40,240,70;coche:280,170,190,110;hud:30,120,220,320')
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @'
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class DesfaseLineas {
    static byte[] Leer(Bitmap b) {
        int w = b.Width, h = b.Height;
        var d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        var p = new byte[w * h * 4];
        for (int y = 0; y < h; y++) Marshal.Copy(d.Scan0 + y * d.Stride, p, y * w * 4, w * 4);
        b.UnlockBits(d); return p;
    }
    public static string Medir(string a, string b, string nombre, int x0, int y0, int ancho, int alto) {
        using (var ia = new Bitmap(a)) using (var ib = new Bitmap(b)) {
            int w = ia.Width, h = ia.Height;
            if (ib.Width != w || ib.Height != h) return nombre + ": tamanos distintos";
            var pa = Leer(ia); var pb = Leer(ib);
            string r = nombre + ":"; int mejor = 0; double minimo = double.MaxValue;
            for (int dy = -3; dy <= 3; dy++) {
                double s = 0; long n = 0;
                for (int y = y0; y < y0 + alto; y++) {
                    int yb = y + dy; if (y < 0 || y >= h || yb < 0 || yb >= h) continue;
                    for (int x = x0; x < x0 + ancho && x < w; x++) {
                        int i = (y * w + x) * 4, j = (yb * w + x) * 4;
                        for (int c = 0; c < 3; c++) s += Math.Abs(pa[i + c] - pb[j + c]);
                        n += 3;
                    }
                }
                double m = n > 0 ? s / n : 0;
                r += String.Format(" {0:+0;-0;0}={1:F2}", dy, m);
                if (m < minimo) { minimo = m; mejor = dy; }
            }
            return r + String.Format(" -> dy={0}", mejor);
        }
    }
}
'@
$rutaA = (Resolve-Path $A).Path; $rutaB = (Resolve-Path $B).Path
foreach ($zona in ($Zonas -split ';')) {
    if ($zona -notmatch '^\s*([^:]+):(\d+),(\d+),(\d+),(\d+)\s*$') { throw "Zona no valida: $zona" }
    [DesfaseLineas]::Medir($rutaA, $rutaB, $Matches[1], [int]$Matches[2], [int]$Matches[3], [int]$Matches[4], [int]$Matches[5])
}
