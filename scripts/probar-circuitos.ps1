# Prueba circuitos en Time Trial: por cada N (veces "derecha" en CIRCUIT desde Silverstone) arranca el juego, entra
# en Time Trial, elige el circuito, sale del garaje y captura en pista (scripts\explorar.ps1, con el piloto).
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar-circuitos.ps1 -Circuitos '1,3,5' [-Prefijo cir]
# Salida: logs\explorar-<Prefijo>-<N>.txt y sus capturas (circuito elegido, garaje, pista a game+40/+80/+120).
# -Tarjetas: carpeta de tarjetas de explorar.ps1 (por defecto logs\tarjetas-prueba).
param([string]$Circuitos = '1', [string]$Prefijo = 'cir', [string]$Variables = 'F1_PILOTO=1',
      [string]$Tarjetas = 'logs\tarjetas-prueba')
$raiz = Split-Path -Parent $PSScriptRoot
# Decimales con punto (con la cultura del sistema, 7.8 saldria "7,8" y partiria las listas separadas por comas).
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture
$base ='pad+3:cruz:0.5,pad+8:cruz:0.5,Continue#3+1.5:start:0.5,Attract+5:start:0.5,LOADfromDir+4:cruz:0.5,' +
        'SerializeSucceeded+4:cruz:0.5,NoInGameAtBoot+4:cruz:0.5,SinglePlayer+4:abajo:0.15,SinglePlayer+5:cruz:0.5'
foreach ($n in ($Circuitos -split ',' | Where-Object { $_.Trim() } | ForEach-Object { [int]$_ })) {
    # En la pantalla de Time Trial (TestDrive): tres abajo hasta CIRCUIT, N derecha, tres arriba y START RACE.
    $g = $base + ',TestDrive+3:abajo:0.15,TestDrive+3.6:abajo:0.15,TestDrive+4.2:abajo:0.15'
    for ($i = 0; $i -lt $n; $i++) { $g += (',TestDrive+{0}:derecha:0.15' -f (5 + $i)) }
    $t = 6 + $n
    $g += (',TestDrive+{0}:arriba:0.15,TestDrive+{1}:arriba:0.15,TestDrive+{2}:arriba:0.15,TestDrive+{3}:cruz:0.5' -f $t, ($t + 0.6), ($t + 1.2), ($t + 2.5))
    # Garaje: LEAVE GARAGE (abajo y cruz) y acelerar.
    $g += ',game+8:abajo:0.15,game+9:cruz:0.5,game+15:cruz:200'
    $c = ('TestDrive+{0},game+5,game+40,game+80,game+120' -f ($t + 1.8))
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$raiz\scripts\explorar.ps1" -Nombre "$Prefijo-$n" -Guion $g `
        -Capturas $c -Fin 'game+125' -Limite 480 -Variables $Variables -Tarjetas $Tarjetas | Select-Object -Last 8
    Get-Process ps2EntryRunner -ErrorAction SilentlyContinue | Stop-Process -Force
}
