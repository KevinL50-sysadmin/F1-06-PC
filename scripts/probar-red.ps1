# Prueba del juego en red en un solo PC, sin nadie delante (odd/tasks/pruebas-autonomas.md): arranca el juego como
# anfitrion (solo en 127.0.0.1, sin preguntas del cortafuegos) y su invitado con un guion de mando
# (PS2X_INVITADO_GUION, sucesos de la ventana del invitado) que saca capturas y la cierra al acabar.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probar-red.ps1 -Nombre X -Guion '...'
#        [-RetrasoAnfitrion 0] [-CacheJuego 'dist\F1-06-prueba\cache\juego'] [-Clave PRUEBARED26] [-Puerto 50651]
#        [-Limite 300] [-Exe '<ruta del .exe>'] [-Argumentos 'juego\SCES_539.50'] [-GuionAnfitrion '...']
#        [-Tarjetas 'logs\tarjetas-prueba']
# -Guion: "evento[#n]+retraso:boton[:duracion],..." (sucesos: formulario, conectando, contrasena, conectado,
#   rechazado, desconectado, error; botones del mando, captura o cerrar). Ejemplo: formulario, bajar a CONECTAR, X,
#   foto de la imagen del juego y cerrar:
#   'formulario+2:captura,formulario+3:abajo,formulario+3.5:abajo,formulario+4:abajo,formulario+5:cruz,conectado+20:captura,conectado+21:cerrar'
# -RetrasoAnfitrion N: el anfitrion arranca N s despues del invitado (para ver el error y la vuelta al formulario);
#   -1: sin anfitrion.
# -CacheJuego: carpeta con la letra y los botones del juego para el invitado; '' = sin cache (Segoe UI).
# El invitado corre en logs\probar-red-<Nombre>\invitado (copia portable del .exe: su config\red.ini apunta a
# 127.0.0.1:<Puerto>, la contrasena llega por PS2X_RED_CLAVE y rellena el formulario).
# Salida: logs\probar-red-<Nombre>\captura-<n>.png, invitado.err.log, anfitrion.out/err.log y resumen.txt.
param(
    [Parameter(Mandatory = $true)][string]$Nombre,
    [Parameter(Mandatory = $true)][string]$Guion,
    [int]$RetrasoAnfitrion = 0,
    [string]$CacheJuego = 'dist\F1-06-prueba\cache\juego',
    [string]$Clave = 'PRUEBARED26',
    [int]$Puerto = 50651,
    [int]$Limite = 300,
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe',
    [string]$Argumentos = 'juego\SCES_539.50',
    [string]$GuionAnfitrion = '',
    [string]$Tarjetas = 'logs\tarjetas-prueba'
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$rutaExe = if ([System.IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $raiz $Exe }
$salida = Join-Path $raiz "logs\probar-red-$Nombre"
$carpetaInvitado = Join-Path $salida 'invitado'
if (Test-Path $salida) { Remove-Item $salida -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $carpetaInvitado 'config') -Force | Out-Null

# El invitado: copia portable del .exe y sus DLL, su red.ini y, si se pide, la cache del juego.
Copy-Item -Path $rutaExe -Destination $carpetaInvitado
Get-ChildItem (Split-Path -Parent $rutaExe) -File -Filter '*.dll' | Copy-Item -Destination $carpetaInvitado
Set-Content -Path (Join-Path $carpetaInvitado 'portable.txt') -Value 'portable' -Encoding Ascii
# red.ini en config\ (paquete portable) y junto al .exe: el .exe de desarrollo abierto sin imagen del juego entra en
# modo desarrollo, que guarda la configuracion junto al .exe.
foreach ($ini in (Join-Path $carpetaInvitado 'config\red.ini'), (Join-Path $carpetaInvitado 'red.ini')) {
    Set-Content -Path $ini -Encoding Ascii -Value @('direccion = 127.0.0.1', "puerto = $Puerto")
}
if ($CacheJuego) {
    $cache = if ([System.IO.Path]::IsPathRooted($CacheJuego)) { $CacheJuego } else { Join-Path $raiz $CacheJuego }
    if (-not (Test-Path $cache)) { throw "No existe la cache del juego: $cache" }
    $destinoCache = Join-Path $carpetaInvitado 'cache\juego'
    New-Item -ItemType Directory -Path $destinoCache -Force | Out-Null
    Copy-Item -Path (Join-Path $cache '*') -Destination $destinoCache -Recurse -Force
}

# Los dos: ventanas en segundo plano y sin tocar los mandos del usuario.
$env:PS2X_BACKGROUND_WINDOW = '1'
$env:PS2X_SIN_MANDOS = '1'
foreach ($v in 'F1_GUION', 'F1_PULSAR', 'PS2X_INVITADO_GUION', 'PS2X_INVITADO_CAPTURAS', 'PS2X_RED_CLAVE') {
    Remove-Item "env:$v" -ErrorAction SilentlyContinue
}

$anfitrion = $null
function ArrancarAnfitrion() {
    if ($Tarjetas) {
        $carpeta = [System.IO.Path]::GetFullPath((Join-Path $raiz $Tarjetas))
        foreach ($mc in 'mc0', 'mc1') {
            $destino = Join-Path $carpeta $mc
            if (-not (Test-Path $destino)) {
                New-Item -ItemType Directory -Path $destino -Force | Out-Null
                $origen = Join-Path $raiz "juego\$mc"
                if (Test-Path $origen) { Copy-Item -Path (Join-Path $origen '*') -Destination $destino -Recurse -Force }
            }
        }
        $env:PS2X_TARJETAS = $carpeta
    }
    if ($GuionAnfitrion) { $env:F1_GUION = $GuionAnfitrion }
    $lineaAnfitrion = "$Argumentos --anfitrion --clave $Clave --puerto $Puerto --escuchar 127.0.0.1".Trim()
    $p = Start-Process -FilePath $rutaExe -WorkingDirectory $raiz -ArgumentList $lineaAnfitrion -PassThru `
        -WindowStyle Hidden -RedirectStandardOutput (Join-Path $salida 'anfitrion.out.log') `
        -RedirectStandardError (Join-Path $salida 'anfitrion.err.log')
    Remove-Item env:F1_GUION -ErrorAction SilentlyContinue
    $p
}
if ($RetrasoAnfitrion -eq 0) { $anfitrion = ArrancarAnfitrion }

$env:PS2X_RED_CLAVE = $Clave
$env:PS2X_INVITADO_GUION = $Guion
$env:PS2X_INVITADO_CAPTURAS = Join-Path $salida 'captura'
$errInvitado = Join-Path $salida 'invitado.err.log'
$invitado = Start-Process -FilePath (Join-Path $carpetaInvitado (Split-Path -Leaf $rutaExe)) -ArgumentList '--invitado' `
    -WorkingDirectory $carpetaInvitado -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $salida 'invitado.out.log') -RedirectStandardError $errInvitado
foreach ($v in 'PS2X_RED_CLAVE', 'PS2X_INVITADO_GUION', 'PS2X_INVITADO_CAPTURAS') { Remove-Item "env:$v" }
$inicio = Get-Date

$agotado = $false
while (-not $invitado.HasExited) {
    $s = ((Get-Date) - $inicio).TotalSeconds
    if (-not $anfitrion -and $RetrasoAnfitrion -gt 0 -and $s -ge $RetrasoAnfitrion) { $anfitrion = ArrancarAnfitrion }
    if ($s -ge $Limite) { $agotado = $true; Stop-Process -Id $invitado.Id -Force -ErrorAction SilentlyContinue; break }
    Start-Sleep -Milliseconds 250
}
$invitado.WaitForExit()
$codigo = if ($agotado) { 'agotado el limite' } else { $invitado.ExitCode }
if ($anfitrion -and -not $anfitrion.HasExited) { Stop-Process -Id $anfitrion.Id -Force -ErrorAction SilentlyContinue }

$resumen = @("Prueba de red '$Nombre': invitado terminado ($codigo) a los {0:N0} s" -f ((Get-Date) - $inicio).TotalSeconds)
if (Test-Path $errInvitado) { $resumen += Get-Content $errInvitado | Where-Object { $_ -match '^\[red\] (invitado|guion|no se|El anfitrion|Desconectado|PS2X_)' } }
$resumen += Get-ChildItem $salida -Filter 'captura-*.png' | Sort-Object { [int]($_.BaseName -replace '\D', '') } |
    ForEach-Object { "Captura: $($_.FullName)" }
$resumen | Set-Content -Path (Join-Path $salida 'resumen.txt') -Encoding UTF8
$resumen
