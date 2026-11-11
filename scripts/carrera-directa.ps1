# Carrera directa para pruebas: arranca una carrera rapida con solo X (guion de scripts\medir-carrera.ps1) y fija
# circuito, piloto, clima, IA, danos y sanciones con las variables F1_CARRERA_* (src\f1_overrides.cpp, carreraDirecta).
# El menu de PreRace ya muestra el circuito y el piloto pedidos; clima, IA, danos y sanciones se escriben en el perfil
# en cada graph.outcome. Sin parametros es la carrera de siempre (Silverstone, J. Button / Honda).
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\carrera-directa.ps1 -Nombre cd-monaco
#        [-Circuito monaco] [-Piloto falonso] [-Clima lluviafuerte] [-IA dificil] [-Danos 1] [-Sanciones 0]
#        [-Lista] [-Variables 'F1_PRUEBA_CARRERA=0.1,0;F1_AYUDAS=0'] [-Capturas 'InstantRace+5,game+40'] [-Fin 'game+42']
#        [-Limite 400]
# -Circuito: 1-18 (game.track, calendario de 2006: 1 Bahrain, 2 Sepang, 3 Melbourne, 4 Imola, 5 Nurburgring,
#   6 Catalunya, 7 Monaco, 8 Silverstone, 9 Montreal, 10 Indianapolis, 11 Magny-Cours, 12 Hockenheim, 13 Budapest,
#   14 Istanbul, 15 Monza, 16 Shanghai, 17 Suzuka, 18 Interlagos) o su nombre en minusculas (monaco, suzuka...).
# -Piloto: id del juego (falonso, gfisichella, kraikkonen, jmontoya, mschumacher, fmassa, rschumacher, jtrulli,
#   mwebber, nrosberg, rbarrichello, jbutton, dcoulthard, cklien, nheidfeld, jvilleneuve, tmonteiro, calbers, vliuzzi,
#   sspeed, tsato, yide) o su final (alonso); el equipo va con el.
# -Clima: variable, soleado, nublado, lluvia, lluviafuerte (o sunny, overcast, lightrain, heavyrain, o un numero).
# -IA: facil, media, dificil (o 0, 1, 2). -Danos, -Sanciones: 0 o 1.
# -Lista: F1_CARRERA_LISTA=1, anota en el log las constantes del clima, los valores del perfil, circuitos y pilotos.
# Salida: la de scripts\explorar.ps1 (logs\explorar-<Nombre>.txt, .out.log y las capturas); lineas [f1-06:carrera].
param(
    [Parameter(Mandatory = $true)][string]$Nombre,
    [string]$Circuito = '',
    [string]$Piloto = '',
    [string]$Clima = '',
    [string]$IA = '',
    [string]$Danos = '',
    [string]$Sanciones = '',
    [switch]$Lista,
    [string]$Variables = '',
    [string]$Capturas = 'InstantRace+5,game+40',
    [string]$Fin = 'game+42',
    [int]$Limite = 400
)
$raiz = Split-Path -Parent $PSScriptRoot
# Solo X: menus hasta QUICK RACE, START RACE en PreRace y X en carrera (salidas de pantallas y acelerar).
$guion = 'pad+3:cruz:0.5,pad+8:cruz:0.5,Continue#3+1.5:start:0.5,Attract+5:start:0.5,LOADfromDir+4:cruz:0.5,' +
         'SerializeSucceeded+4:cruz:0.5,NoInGameAtBoot+4:cruz:0.5,SinglePlayer+4:cruz:0.5,InstantRace+6:cruz:0.5,' +
         'game+25:cruz:0.5,game+35:cruz:0.5,game+45:cruz:900'
$pares = @()
if ($Circuito) { $pares += "F1_CARRERA_CIRCUITO=$Circuito" }
if ($Piloto) { $pares += "F1_CARRERA_PILOTO=$Piloto" }
if ($Clima) { $pares += "F1_CARRERA_CLIMA=$Clima" }
if ($IA) { $pares += "F1_CARRERA_IA=$IA" }
if ($Danos) { $pares += "F1_CARRERA_DANOS=$Danos" }
if ($Sanciones) { $pares += "F1_CARRERA_SANCIONES=$Sanciones" }
if ($Lista) { $pares += 'F1_CARRERA_LISTA=1' }
if ($Variables) { $pares += $Variables }
# powershell -File pierde un argumento vacio: -Variables solo si hay alguna.
$extra = @()
if ($pares.Count -gt 0) { $extra = @('-Variables', ($pares -join ';')) }
& powershell -NoProfile -ExecutionPolicy Bypass -File "$raiz\scripts\explorar.ps1" -Nombre $Nombre -Guion $guion `
    -Capturas $Capturas -Fin $Fin -Limite $Limite @extra
Get-Process ps2EntryRunner -ErrorAction SilentlyContinue | Stop-Process -Force
