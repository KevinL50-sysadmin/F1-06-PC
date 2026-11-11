# Repite arranques cortos hasta la comprobacion de la tarjeta de memoria para contar cuantos se cuelgan en
# "Checking status of the Memory Card" (tras graph.outcome("PS2") no llega graph.outcome("CanLoadProfile")).
# Cada arranque se corta en cuanto llega CanLoadProfile (o a los -Espera segundos tras "PS2": colgado). En un cuelgue
# guarda las pilas (logs\pila-<nombre>.txt) y, si -Variables lleva F1_VOLCAR_RAM, deja correr hasta ese volcado.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\tanda-tarjeta.ps1 -Nombre mc -Veces 10
#        [-Variables 'PS2X_VBLANK_HZ=60;F1_HILOS=30'] [-Espera 40] [-Limite 240]
param(
    [string]$Nombre = 'mc',
    [int]$Veces = 10,
    [int]$Espera = 40,
    [int]$Limite = 240,
    [string]$Variables = '',
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe'
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$env:F1_GUION = 'pad+3:cruz:0.5,pad+8:cruz:0.5,Continue#3+1.5:start:0.5,Attract+5:start:0.5'
$env:PS2X_BACKGROUND_WINDOW = '1'
$env:PS2X_SIN_MANDOS = '1' # no tocar los mandos ni el volante del usuario (=0 con -Variables para probarlos)
$env:F1_PILOTO = '1'
foreach ($par in ($Variables -split ';')) {
    if ($par -match '^\s*([^=]+)=(.*)$') { Set-Item -Path ("env:" + $Matches[1].Trim()) -Value $Matches[2] }
}
$volcado = 0
if ($env:F1_VOLCAR_RAM) { $volcado = [int]($env:F1_VOLCAR_RAM -split ',')[0] }

function Buscar([string]$out, [string]$patron) {
    if (-not (Test-Path $out)) { return $false }
    try { return [bool](Select-String -Path $out -Pattern $patron -SimpleMatch -Quiet) } catch { return $false }
}

$colgados = 0; $buenos = 0; $otros = 0
for ($i = 1; $i -le $Veces; $i++) {
    $n = "$Nombre-$i"
    $base = Join-Path $raiz "logs\medida-$n"
    $out = "$base.out.log"; $err = "$base.err.log"
    $proc = Start-Process -FilePath (Join-Path $raiz $Exe) -ArgumentList 'juego\SCES_539.50' -WorkingDirectory $raiz `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err
    $inicio = Get-Date
    $tPs2 = $null
    $estado = 'limite'
    try {
        while (((Get-Date) - $inicio).TotalSeconds -lt $Limite -and -not $proc.HasExited) {
            Start-Sleep -Seconds 2
            if (Buscar $out 'graph.outcome("CanLoadProfile")') { $estado = 'bien'; break }
            if (-not $tPs2 -and (Buscar $out 'graph.outcome("PS2")')) { $tPs2 = Get-Date }
            if ($tPs2 -and ((Get-Date) - $tPs2).TotalSeconds -gt $Espera) { $estado = 'colgado'; break }
        }
        if ($estado -eq 'colgado') {
            $pila = Join-Path $raiz "logs\pila-$n.txt"
            & "$raiz\build\pila.exe" $proc.Id | Out-File -Encoding utf8 $pila
            while ($volcado -gt 0 -and ((Get-Date) - $inicio).TotalSeconds -lt $volcado + 5 -and -not $proc.HasExited) {
                Start-Sleep -Seconds 1
            }
            $ram = Join-Path $raiz 'logs\ee_ram.bin'
            if (Test-Path $ram) { Move-Item -Force $ram (Join-Path $raiz "logs\ee_ram-$n.bin") }
        }
    }
    finally {
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    }
    Start-Sleep -Seconds 1
    $t = [int]((Get-Date) - $inicio).TotalSeconds
    switch ($estado) {
        'bien' { $buenos++; "Arranque $i ($n): bien ($t s)" }
        'colgado' { $colgados++; "Arranque $i ($n): COLGADO en la tarjeta ($t s, pilas en logs\pila-$n.txt)" }
        default { $otros++; "Arranque $i ($n): sin llegar a la tarjeta ($t s)" }
    }
}
"Resumen ${Nombre}: $buenos bien, $colgados colgados, $otros sin llegar, de $Veces"
