# Repite partidas cortas hasta que una se cuelgue antes de la carrera (el cuelgue intermitente de los menus).
# Cada una llega a la carrera y acaba poco despues. La tanda solo se para si sale el chivato [f1-06:cuelgue]
# (videoTerminado en src\f1_overrides.cpp); una partida que no llega a tiempo sin chivato es solo lenta (PC cargado:
# medir-carrera.ps1 guarda igualmente las pilas en logs\pila-<nombre>.txt) y la tanda sigue.
# Las variables de diagnostico van en -Variables.
# Uso: powershell -File scripts\cazar-cuelgue.ps1 -Nombre caza -Veces 5 -Variables 'PS2X_EE_DESPACHOS=1;F1_HILOS=30'
param(
    [string]$Nombre = 'caza',
    [int]$Veces = 5,
    [int]$Limite = 300,
    [string]$Variables = ''
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
$guion = 'pad+3:cruz:0.5,pad+8:cruz:0.5,Continue#3+1.5:start:0.5,Attract+5:start:0.5,LOADfromDir+4:cruz:0.5,SerializeSucceeded+4:cruz:0.5,NoInGameAtBoot+4:cruz:0.5,SinglePlayer+4:cruz:0.5,InstantRace+6:cruz:0.5'
$lentas = 0
for ($i = 1; $i -le $Veces; $i++) {
    $n = "$Nombre-$i"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $raiz 'scripts\medir-carrera.ps1') -Nombre $n `
        -Limite $Limite -FinTras 15 -Muestras 0 -Variables $Variables -Guion $guion | Out-Null
    $log = Join-Path $raiz "logs\medida-$n.out.log"
    $chivato = (Test-Path $log) -and (Select-String -Path $log -Pattern '[f1-06:cuelgue]' -SimpleMatch -Quiet)
    if ($chivato) {
        "Colgado en la partida $i ($n): chivato en logs\medida-$n.out.log"
        exit 0
    }
    if (Test-Path (Join-Path $raiz "logs\pila-$n.txt")) {
        "Partida $i ($n): lenta, sin chivato (pilas en logs\pila-$n.txt)"
        $lentas++
        continue
    }
    "Partida $i ($n): sin cuelgue"
}
"Sin cuelgue en $Veces partidas ($lentas lentas sin chivato)"
