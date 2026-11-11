# Prueba un modo del menu RACE (o del menu principal) aceptando todo con cruz: baja N lineas en el menu, entra, y
# pulsa cruz cada -Paso s capturando antes de cada pulsacion (scripts\explorar.ps1).
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar-modo.ps1 -Nombre wc -Abajo 3
#        [-Menu SinglePlayer|NoInGameAtBoot] [-Pasos 12] [-Paso 6] [-Extra ',game+25:cruz:0.5'] [-Fin 'game+100']
#        [-Tarjetas 'logs\tarjetas-x'] (carpeta de tarjetas de explorar.ps1; por defecto logs\tarjetas-prueba)
param(
    [Parameter(Mandatory = $true)][string]$Nombre,
    [int]$Abajo = 0,
    [string]$Menu = 'SinglePlayer',
    [int]$Pasos = 12,
    [double]$Paso = 6,
    [string]$Extra = ',game+25:cruz:0.5,game+35:cruz:0.5,game+45:cruz:0.5,game+55:cruz:0.5,game+65:cruz:120',
    [string]$CapturasExtra = ',game+20,game+60,game+100',
    [string]$Fin = 'game+105',
    [int]$Limite = 480,
    [string]$Variables = 'F1_PILOTO=1',
    [string]$Tarjetas = 'logs\tarjetas-prueba'
)
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture
$raiz = Split-Path -Parent $PSScriptRoot
$g = 'pad+3:cruz:0.5,pad+8:cruz:0.5,Continue#3+1.5:start:0.5,Attract+5:start:0.5,LOADfromDir+4:cruz:0.5,' +
     'SerializeSucceeded+4:cruz:0.5'
if ($Menu -eq 'SinglePlayer') { $g += ',NoInGameAtBoot+4:cruz:0.5' }
for ($i = 0; $i -lt $Abajo; $i++) { $g += (',{0}+{1}:abajo:0.15' -f $Menu, (4 + 0.6 * $i)) }
$entrar = 5 + 0.6 * $Abajo
$g += (',{0}+{1}:cruz:0.5' -f $Menu, $entrar)
$c = @()
for ($i = 1; $i -le $Pasos; $i++) {
    $g += (',{0}+{1}:cruz:0.5' -f $Menu, ($entrar + $Paso * $i))
    $c += ('{0}+{1}' -f $Menu, ($entrar + $Paso * $i - 1))
}
$g += $Extra
$capturas = ($c -join ',') + $CapturasExtra
& powershell -NoProfile -ExecutionPolicy Bypass -File "$raiz\scripts\explorar.ps1" -Nombre $Nombre -Guion $g `
    -Capturas $capturas -Fin $Fin -Limite $Limite -Variables $Variables -Tarjetas $Tarjetas
Get-Process ps2EntryRunner -ErrorAction SilentlyContinue | Stop-Process -Force
