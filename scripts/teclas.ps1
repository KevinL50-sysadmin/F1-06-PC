# Pulsa teclas en la ventana del juego aunque esté en segundo plano y sin foco (PostMessage WM_KEYDOWN/WM_KEYUP con el
# código de exploración que usa GLFW), para probar el teclado del parche 0105 sin tocar el teclado del usuario.
# Uso: powershell -File scripts\teclas.ps1 -Pasos 'X:0.5,espera:2,ARRIBA:0.2'
#   <tecla>:<segundos mantenida> (teclas: A..Z, 0..9, F1..F12, ARRIBA ABAJO IZQUIERDA DERECHA, ESPACIO INTRO TAB ESC
#   RETROCESO PUNTO) o
#   espera:<segundos>. Varias teclas a la vez: 'X+DERECHA:3'.
#   -Id <pid>: la ventana de ese proceso (p. ej. el invitado del juego en red, que tambien es ps2EntryRunner).
param([string]$Pasos, [string]$Proceso = 'ps2EntryRunner', [int]$Id = 0)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Teclas {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
}
'@
$p = if ($Id) { Get-Process -Id $Id -ErrorAction Stop } else { Get-Process -Name $Proceso -ErrorAction Stop | Select-Object -First 1 }
$hwnd = $p.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { throw 'El juego no tiene ventana' }
$vk = @{ ARRIBA = 0x26; ABAJO = 0x28; IZQUIERDA = 0x25; DERECHA = 0x27; ESPACIO = 0x20; INTRO = 0x0D; TAB = 0x09;
         ESC = 0x1B; RETROCESO = 0x08; PUNTO = 0xBE }
$extendida = @{ ARRIBA = 1; ABAJO = 1; IZQUIERDA = 1; DERECHA = 1 }
function CodigoVirtual([string]$t) {
    if ($vk.ContainsKey($t)) { return $vk[$t] }
    if ($t -match '^F([0-9]{1,2})$') { return 0x6F + [int]$Matches[1] }
    [int][char]$t.ToUpper()
}
function Mensaje([string]$t, [bool]$abajo) {
    $v = CodigoVirtual $t
    $sc = [Teclas]::MapVirtualKey([uint32]$v, 0)
    $l = 1 -bor ($sc -shl 16)
    if ($extendida.ContainsKey($t)) { $l = $l -bor (1 -shl 24) }
    if (-not $abajo) { $l = $l -bor (1 -shl 30) -bor (1 -shl 31) }
    $msg = if ($abajo) { 0x0100 } else { 0x0101 }
    [void][Teclas]::PostMessage($hwnd, $msg, [IntPtr]$v, [IntPtr][int64]([uint32]$l))
}
foreach ($paso in ($Pasos -split ',')) {
    $partes = $paso.Trim() -split ':'
    $seg = [double]$partes[1]
    if ($partes[0] -eq 'espera') { Start-Sleep -Milliseconds ([int]($seg * 1000)); continue }
    $teclas = $partes[0] -split '\+'
    foreach ($t in $teclas) { Mensaje $t $true }
    Start-Sleep -Milliseconds ([int]($seg * 1000))
    foreach ($t in $teclas) { Mensaje $t $false }
    Start-Sleep -Milliseconds 150
}
