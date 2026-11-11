# Mide la fluidez de los menus (fase 3, T9): imagenes distintas por segundo que muestra la ventana, cuanto se queda cada
# una en pantalla, congelaciones y tiempos entre pantallas, en el port o en un video de PCSX2 (pcsx2-ref.ps1 -Video).
# Uso:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\medir-menus.ps1 -Nombre X [-Variables 'PS2X_X=1;...']
#     Arranca el juego con explorar.ps1 y un guion por los menus: menu principal -> RACE -> QUICK RACE -> atras ->
#     atras -> OPTIONS -> DISPLAY SETTINGS -> atras -> atras -> RACE -> QUICK RACE -> START RACE (carga de la
#     carrera). Por defecto con la configuracion grafica del jugador (gs gpu, supersampling 16, alta_resolucion,
#     vsync, fsr); -Variables se anade detras y gana. Trazas: PS2X_TRAZA_FOTOGRAMAS (lineas [ps2x:fotogramas] por
#     segundo y [ps2x:congelada]) y PS2X_TRAZA_FOTOGRAMAS_CSV (logs\medir-menus-X.csv: cada imagen nueva con su
#     diferencia con la anterior, rejilla de 32x24 de luma).
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\medir-menus.ps1 -Nombre X -Video logs\ref-pcsx2-Y-1.mp4
#     Analiza un video de PCSX2 (ffmpeg en el PATH) con la misma rejilla: logs\medir-menus-X.csv.
# Salida: logs\medir-menus-X.txt con, por pantalla (entre dos sucesos graph.outcome), las imagenes/s medias y los
# refrescos por imagen; y la linea de tiempo de la actividad de la imagen (tramos con cambios y tramos quietos de 0,2 s
# o mas), que da la duracion de cada transicion (en PCSX2, en tiempo emulado: el del video).
param(
    [Parameter(Mandatory = $true)][string]$Nombre,
    [string]$Variables = '',
    [string]$Video = '',
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe',
    [double]$Umbral = 0.3
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$csv = Join-Path $raiz "logs\medir-menus-$Nombre.csv"
$salida = Join-Path $raiz "logs\medir-menus-$Nombre.txt"

Add-Type @'
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
public static class MedirMenus {
    // Video en bruto de 32x24 en grises (ffmpeg) -> CSV t,diferencia a 50 imagenes/s.
    public static int Diferencias(Stream entrada, string csv) {
        var cultura = CultureInfo.InvariantCulture;
        byte[] actual = new byte[768], anterior = null;
        int n = 0;
        using (var w = new StreamWriter(csv)) {
            w.WriteLine("t,diferencia");
            while (true) {
                int leidos = 0;
                while (leidos < 768) { int r = entrada.Read(actual, leidos, 768 - leidos); if (r <= 0) break; leidos += r; }
                if (leidos < 768) break;
                double d = 0;
                if (anterior != null) { int s = 0; for (int i = 0; i < 768; i++) s += Math.Abs(actual[i] - anterior[i]); d = s / 768.0; }
                else anterior = new byte[768];
                w.WriteLine(string.Format(cultura, "{0:F4},{1:F2}", n / 50.0, d));
                Buffer.BlockCopy(actual, 0, anterior, 0, 768);
                n++;
            }
        }
        return n;
    }
    // Tramos de actividad (cambios por encima del umbral) separados por 0,2 s o mas sin cambios.
    public static List<string> Tramos(string csv, double umbral) {
        var cultura = CultureInfo.InvariantCulture;
        var lineas = new List<string>();
        double inicio = -1, previo = -1, maximo = 0; int n = 0;
        foreach (var l in File.ReadLines(csv)) {
            var p = l.Split(',');
            double t, d;
            if (p.Length < 2 || !double.TryParse(p[0], NumberStyles.Float, cultura, out t) ||
                !double.TryParse(p[1], NumberStyles.Float, cultura, out d) || d <= umbral) continue;
            if (previo >= 0 && t - previo >= 0.2) {
                lineas.Add(string.Format(cultura, "activo {0:F2}-{1:F2} ({2:F2} s, {3} cambios, max {4:F0}) | quieto {5:F2} s",
                    inicio, previo, previo - inicio, n, maximo, t - previo));
                inicio = -1;
            }
            if (inicio < 0) { inicio = t; n = 0; maximo = 0; }
            n++; maximo = Math.Max(maximo, d); previo = t;
        }
        if (inicio >= 0)
            lineas.Add(string.Format(cultura, "activo {0:F2}-{1:F2} ({2:F2} s, {3} cambios, max {4:F0})", inicio, previo, previo - inicio, n, maximo));
        return lineas;
    }
}
'@

$informe = New-Object System.Collections.Generic.List[string]
if ($Video) {
    $rutaVideo = if ([System.IO.Path]::IsPathRooted($Video)) { $Video } else { Join-Path $raiz $Video }
    $info = New-Object System.Diagnostics.ProcessStartInfo 'ffmpeg'
    $info.Arguments = "-v error -i `"$rutaVideo`" -vf scale=32:24:flags=neighbor,format=gray -f rawvideo -"
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $p = [System.Diagnostics.Process]::Start($info)
    $n = [MedirMenus]::Diferencias($p.StandardOutput.BaseStream, $csv)
    $p.WaitForExit()
    $informe.Add("Video $rutaVideo : $n imagenes (50/s, tiempo emulado)")
    # Imagenes distintas por segundo (cambios por encima del umbral).
    $porSegundo = @{}
    foreach ($l in (Get-Content $csv | Select-Object -Skip 1)) {
        $c = $l -split ','
        if ([double]::Parse($c[1], [Globalization.CultureInfo]::InvariantCulture) -gt $Umbral) {
            $s = [int][Math]::Floor([double]::Parse($c[0], [Globalization.CultureInfo]::InvariantCulture))
            $porSegundo[$s] = 1 + [int]$porSegundo[$s]
        }
    }
    $informe.Add('Cambios por segundo (segundo:cambios): ' + (($porSegundo.Keys | Sort-Object | ForEach-Object { "${_}:$($porSegundo[$_])" }) -join ' '))
} else {
    $guion = 'pad+3:cruz:0.5,pad+8:cruz:0.5,Continue#3+1.5:start:0.5,Attract+5:start:0.5,LOADfromDir+4:cruz:0.5,' +
        'SerializeSucceeded+4:cruz:0.5,NoInGameAtBoot+8:cruz:0.3,SinglePlayer+8:cruz:0.3,InstantRace+8:triangulo:0.3,' +
        'Back#1+8:triangulo:0.3,Back#2+8:abajo:0.1,Back#2+8.6:abajo:0.1,Back#2+9.4:cruz:0.3,Options+8:abajo:0.1,' +
        'Options+8.6:abajo:0.1,Options+9.2:abajo:0.1,Options+9.8:abajo:0.1,Options+10.6:cruz:0.3,' +
        'DisplaySettings+8:triangulo:0.3,Back#3+8:triangulo:0.3,Back#4+8:arriba:0.1,Back#4+8.6:arriba:0.1,' +
        'Back#4+9.4:cruz:0.3,SinglePlayer#2+8:cruz:0.3,InstantRace#2+8:cruz:0.3'
    $base = 'PS2X_GS=gpu;PS2X_GS_SSAA=16;PS2X_GS_ALTA_RES=1;PS2X_VSYNC=1;PS2X_FILTRO=fsr;PS2X_TRAZA_FOTOGRAMAS=1;' +
        "PS2X_TRAZA_FOTOGRAMAS_CSV=$csv"
    $todas = if ($Variables) { "$base;$Variables" } else { $base }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'explorar.ps1') -Nombre "menus-$Nombre" `
        -Guion $guion -Fin 'game+15' -Limite 500 -Variables $todas -Exe $Exe | Out-Null
    $texto = Get-Content (Join-Path $raiz "logs\explorar-menus-$Nombre.out.log") -Raw
    $sucesos = @([regex]::Matches($texto, '\[f1-06:lua\] t=([0-9.]+) graph\.outcome\("([^"]*)"\)') | ForEach-Object {
        [pscustomobject]@{ T = [double]::Parse($_.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture); Nombre = $_.Groups[2].Value } })
    $segundos = @([regex]::Matches($texto, '\[ps2x:fotogramas\] t=([0-9.]+) \S+ \S+ \S+ \S+ imagenes/s=([0-9.]+) .*?refrescos=([0-9: ]*)') | ForEach-Object {
        [pscustomobject]@{ T = [double]::Parse($_.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
            Imagenes = [double]::Parse($_.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture); Refrescos = $_.Groups[3].Value.Trim() } })
    $informe.Add('Pantallas (desde el suceso hasta el siguiente; sin el primer y el ultimo segundo):')
    for ($i = 0; $i -lt $sucesos.Count; $i++) {
        $desde = $sucesos[$i].T + 1.0
        $hasta = if ($i + 1 -lt $sucesos.Count) { $sucesos[$i + 1].T - 1.0 } else { $desde + 14.0 }
        $tramo = @($segundos | Where-Object { $_.T -ge $desde -and $_.T -le $hasta })
        if ($tramo.Count -eq 0) { continue }
        $media = ($tramo | Measure-Object -Property Imagenes -Average).Average
        $informe.Add(('  t={0:F1} {1}: {2:F1} imagenes/s ({3} s); refrescos del ultimo segundo: {4}' -f $sucesos[$i].T,
            $sucesos[$i].Nombre, $media, $tramo.Count, $tramo[-1].Refrescos))
    }
    foreach ($m in [regex]::Matches($texto, '\[ps2x:congelada\] t=([0-9.]+) ms=(\d+)')) {
        if ([int]$m.Groups[2].Value -ge 200) { $informe.Add("  congelada: t=$($m.Groups[1].Value) $($m.Groups[2].Value) ms") }
    }
    $informe.Add('Sucesos: ' + (($sucesos | ForEach-Object { '{0:F2} {1}' -f $_.T, $_.Nombre }) -join ', '))
}
$informe.Add("Actividad de la imagen (umbral $Umbral):")
foreach ($l in [MedirMenus]::Tramos($csv, $Umbral)) { $informe.Add("  $l") }
$informe | Out-File -Encoding utf8 $salida
# En la consola, el resumen sin la linea de tiempo (que esta entera en el informe).
$informe | Where-Object { -not $_.StartsWith('  activo') } | Select-Object -First 60
"Informe: $salida"
