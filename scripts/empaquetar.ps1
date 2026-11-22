# Empaqueta F1 06 recompilado como un juego de PC, portable o con instalador (las dos cosas con los datos dentro).
# Portable: una carpeta (por defecto dist\F1-06-portable) con el .exe, sus DLL, portable.txt, README.txt (ingles) y
# LEEME.txt (espanol), "Play online - Guest.cmd" (abre el .exe con --invitado: el formulario del juego en red), data\ y
# mods\ vacia, y recomplauncher.json (de scripts\paquete; RecompLauncher, odd/tasks/recomplauncher-json.md). portable.txt hace que el .exe guarde config\, saves\, logs\, screenshots\ y cache\ en su carpeta (los
# crea al abrirse); sin el (instalado) van a Documentos\My Games\F1-06 y AppData\Local\F1-06\logs y cache.
# v1.1.1 (odd/tasks/player-folders.md): la carpeta del jugador va en ingles (antes datos\, partidas\, registros\,
# capturas\, licencias\, LEEME.txt solo y "Jugar en red - Invitado.cmd"); el juego renombra las viejas al abrirse.
# -Zip: ademas, <carpeta>.zip (dentro, una carpeta F1-06 con lo mismo, sin lo que haya creado el juego).
# -Instalador: ademas, compila scripts\instalador\F1-06.iss con tools\innosetup\ISCC.exe en dist\instalador
# (F1-06-Setup.exe y los .bin: los datos pasan de 4 GB). El instalador toma los archivos de la carpeta portable,
# Su pagina de bienvenida lleva la portada retocada como caja de PC (scripts\hacer-imagen-instalador.ps1, en
# build\instalador-imagen; se hace si falta).
# sin portable.txt ni lo que haya creado el juego.
# Icono: build\icono\F1-06.ico (el logo del juego; no esta en el repositorio). Si falta, se hace con
# scripts\hacer-icono.ps1 (arranca el juego unos 3 minutos). El .exe lo lleva dentro si se compilo con el icono ya
# hecho (compilar-runner.cmd); el instalador lo usa para Setup.exe.
# Los datos van aparte del .exe porque Windows no ejecuta archivos de mas de 4 GB.
# Datos: -Iso extrae la ISO en data\ con scripts\iso_ps2.py (la ISO solo se lee); -Datos copia una carpeta ya
# extraida (p. ej. juego) sin mc0, mc1 ni *.log. Sin ninguno: se deja el data\ que ya haya y, si no hay, se copia
# dist\F1-06\datos (solo se lee). data\ no se vuelve a copiar si ya existe (salvo con -Rehacer). Un datos\ de un
# paquete anterior (v1.1.0) se renombra a data\ si no hay data\ (no se copia otra vez); si estan los dos, datos\ se
# deja (con un aviso) y no entra en el paquete.
# Lanzamiento T1: -SinDatos hace la version publica, sin nada del juego (ni data\ ni la ISO): por defecto en
# dist\F1-06-publico, el zip en dist\F1-06-publico.zip y el instalador en dist\instalador-publico. El .exe pide la ISO del
# jugador la primera vez (o el instalador, en su pagina "Elige tu copia del juego (ISO)", con F1-06.exe --extraer-iso)
# y la copia en data\. Un data\ que ya haya en esa carpeta (p. ej. de probarlo) se deja, pero no entra en el paquete.
# Solo se borra lo que crea este script (.exe, .dll, README.txt, LEEME.txt, portable.txt, recomplauncher.json, el .cmd,
# licenses\, el .zip y
# el instalador) y sus nombres de la v1.1.0 ("Jugar en red - Invitado.cmd" y licencias\); config\, saves\, logs\,
# screenshots\, cache\, lo que haya en mods\ y las carpetas viejas del jugador (partidas\, registros\...) nunca se tocan.
# Simbolos (vigilante T4): el paquete no lleva el PDB; el .exe empaquetado y su PDB se guardan aparte en
# <Simbolos>\<version>_<guid>_<edad>\ (por defecto dist\simbolos, que no entra en el paquete, ni en el zip, ni en el
# instalador, ni en el repositorio publico) con identidad.txt (version, exe, fecha_pe, tam_imagen, pdb, guid, edad y
# SHA-256 del .exe). Antes de tocar el paquete se comprueba que el PDB junto a -Exe es el de ese .exe (mismo GUID y
# edad); si no, se para. scripts\simbolizar.ps1 busca ahi el PDB de los informes del vigilante de los jugadores.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\empaquetar.ps1 [-Iso 'ruta\juego.iso' |
#        -Datos 'juego' | -SinDatos] [-Rehacer] [-Zip] [-Instalador] [-Destino 'dist\F1-06-portable']
#        [-Exe 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe'] [-Nombre 'F1-06.exe'] [-Simbolos 'dist\simbolos']
param(
    [string]$Iso = '',
    [string]$Datos = '',
    [switch]$SinDatos,
    [switch]$Rehacer,
    [switch]$Zip,
    [switch]$Instalador,
    [string]$Destino = 'dist\F1-06-portable',
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe',
    [string]$Nombre = 'F1-06.exe',
    [string]$Simbolos = 'dist\simbolos'
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
# Version del paquete (README.txt, LEEME.txt e instalador): se cambia solo en scripts\paquete\version.txt, que tambien
# lee CMake al compilar el .exe (linea [version] de la cabecera del registro; odd/tasks/fallos-con-log.md T3a).
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'paquete\version.txt') -TotalCount 1).Trim()
if ($version -notmatch '^[0-9A-Za-z.+-]+$') { throw "scripts\paquete\version.txt no tiene una version valida: '$version'" }
$fecha = Get-Date -Format 'yyyy-MM-dd'
if ($Iso -and $Datos) { throw 'Usa -Iso o -Datos, no los dos' }
if ($SinDatos -and ($Iso -or $Datos)) { throw '-SinDatos no lleva -Iso ni -Datos' }
if ($SinDatos -and -not $PSBoundParameters.ContainsKey('Destino')) { $Destino = 'dist\F1-06-publico' }
$exeOrigen = [System.IO.Path]::GetFullPath((Join-Path $raiz $Exe))
# Fase 5 (T15, N4): el acceso del invitado del juego en red (el instalador pone uno en el menu Inicio).
$cmdInvitado = 'Play online - Guest.cmd'
# Nombres de la v1.1.0 que ponia este script: se quitan al rehacer el paquete (player-folders T3).
$cmdInvitadoViejo = 'Jugar en red - Invitado.cmd'
$carpeta = [System.IO.Path]::GetFullPath((Join-Path $raiz $Destino)).TrimEnd('\')
$carpetaDatos = Join-Path $carpeta 'data'
$carpetaDatosVieja = Join-Path $carpeta 'datos'
$datosPorDefecto = Join-Path $raiz 'dist\F1-06\datos'
if (-not (Test-Path $exeOrigen)) { throw "Falta $exeOrigen (compila antes con compilar-runner.cmd)" }
function Medir([scriptblock]$bloque) { $t = [Diagnostics.Stopwatch]::StartNew(); & $bloque; $t.Elapsed.TotalSeconds }

# Identidad de un .exe o .pdb (scripts\simbolizar.ps1 -Identidad): clave=valor separados por espacios.
function Identidad([string]$ruta) {
    $texto = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'simbolizar.ps1') -Identidad $ruta
    if ($LASTEXITCODE -ne 0) { throw "simbolizar.ps1 -Identidad $ruta ha fallado" }
    $id = [ordered]@{}
    foreach ($par in ("$texto".Trim() -split ' ')) { $k, $v = $par -split '=', 2; $id[$k] = $v }
    $id
}
# El PDB del .exe que se va a empaquetar (el que nombra su registro CodeView, en su carpeta), comprobado antes de
# tocar el paquete: mismo GUID y misma edad.
$idExe = Identidad $exeOrigen
if (-not $idExe.guid) { throw "$exeOrigen no lleva registro CodeView (se compilo sin /DEBUG): no hay PDB que guardar" }
$pdbOrigen = Join-Path (Split-Path -Parent $exeOrigen) $idExe.pdb
if (-not (Test-Path $pdbOrigen)) { throw "Falta $pdbOrigen, el PDB de ${exeOrigen}: sin el no se podran traducir los informes del vigilante" }
$idPdb = Identidad $pdbOrigen
if ($idPdb.guid -ne $idExe.guid -or $idPdb.edad -ne $idExe.edad) {
    throw ("$pdbOrigen (guid=$($idPdb.guid) edad=$($idPdb.edad)) no es el PDB de $exeOrigen (guid=$($idExe.guid) " +
        "edad=$($idExe.edad)): vuelve a compilar con compilar-runner.cmd")
}

$icono = Join-Path $raiz 'build\icono\F1-06.ico'
if (-not (Test-Path $icono)) {
    Write-Host 'Falta build\icono\F1-06.ico: se hace con scripts\hacer-icono.ps1'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'hacer-icono.ps1')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $icono)) { throw 'hacer-icono.ps1 no pudo hacer el icono' }
}
if ((Get-Item $icono).LastWriteTime -gt (Get-Item $exeOrigen).LastWriteTime) {
    Write-Warning 'El icono es mas nuevo que el .exe: vuelve a compilar con compilar-runner.cmd para que lo lleve dentro'
}

if (Test-Path $carpeta) {
    Get-ChildItem $carpeta -File |
        Where-Object { $_.Extension -in '.exe', '.dll' -or
            $_.Name -in 'README.txt', 'LEEME.txt', 'portable.txt', 'recomplauncher.json', $cmdInvitado, $cmdInvitadoViejo } |
        Remove-Item -Force
    # licencias\ (v1.1.0) solo tiene lo que puso este script: se rehace como licenses\.
    $licenciasViejas = Join-Path $carpeta 'licencias'
    if (Test-Path -LiteralPath $licenciasViejas) { Remove-Item -LiteralPath $licenciasViejas -Recurse -Force }
    # datos\ (v1.1.0) son los archivos del disco: nunca se borran. Sin data\, se renombra (como hace el juego; no se
    # vuelve a copiar); con los dos, se deja y no entra en el paquete.
    if (Test-Path -LiteralPath $carpetaDatosVieja) {
        if (-not (Test-Path -LiteralPath $carpetaDatos)) {
            try {
                Rename-Item -LiteralPath $carpetaDatosVieja -NewName 'data'
                Write-Host "datos\ (v1.1.0) renombrada a data\"
            } catch {
                Write-Warning "No se pudo renombrar $carpetaDatosVieja a data\ ($($_.Exception.Message)): se deja"
            }
        } else {
            Write-Warning "$carpetaDatosVieja y data\ existen: datos\ se deja (borrala tu si sobra); no entra en el paquete"
        }
    }
} else {
    New-Item -ItemType Directory -Path $carpeta | Out-Null
}
Get-ChildItem (Split-Path -Parent $exeOrigen) -Filter '*.dll' | Copy-Item -Destination $carpeta
$exeDestino = Join-Path $carpeta $Nombre
Copy-Item $exeOrigen $exeDestino

# Simbolos: el .exe empaquetado y su PDB, aparte y fuera del paquete (ver la cabecera).
$hashExe = (Get-FileHash -Algorithm SHA256 -LiteralPath $exeDestino).Hash
$carpetaSimbolos = Join-Path ([System.IO.Path]::GetFullPath((Join-Path $raiz $Simbolos))) "${version}_$($idExe.guid)_$($idExe.edad)"
if (-not (Test-Path $carpetaSimbolos)) { New-Item -ItemType Directory -Path $carpetaSimbolos | Out-Null }
$exeSimbolos = Join-Path $carpetaSimbolos $Nombre
Copy-Item $exeDestino $exeSimbolos -Force
Copy-Item $pdbOrigen (Join-Path $carpetaSimbolos $idExe.pdb) -Force
$idGuardado = Identidad (Join-Path $carpetaSimbolos $idExe.pdb)
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $exeSimbolos).Hash -ne $hashExe -or
    $idGuardado.guid -ne $idExe.guid -or $idGuardado.edad -ne $idExe.edad) {
    throw "Los simbolos guardados en $carpetaSimbolos no coinciden con $exeDestino"
}
Set-Content -Path (Join-Path $carpetaSimbolos 'identidad.txt') -Encoding Ascii -Value @(
    "version=$version fecha=$fecha",
    ("exe=$Nombre fecha_pe=$($idExe.fecha_pe) tam_imagen=$($idExe.tam_imagen) pdb=$($idExe.pdb) guid=$($idExe.guid) " +
        "edad=$($idExe.edad)"),
    "sha256=$hashExe",
    "paquete=$carpeta")
Write-Host ("Simbolos: {0} y {1} en {2} (guid={3} edad={4})" -f $Nombre, $idExe.pdb, $carpetaSimbolos, $idExe.guid,
    $idExe.edad)
Set-Content -Path (Join-Path $carpeta 'portable.txt') -Encoding UTF8 -Value (('Con este archivo junto a {0}, el juego ' +
    'guarda config\, saves\, logs\, screenshots\, cache\ y mods\ en esta carpeta; borralo para usar ' +
    'Documentos\My Games\F1-06.') -f $Nombre)
# ASCII sin BOM: cmd.exe no entiende el BOM de UTF-8.
Set-Content -Path (Join-Path $carpeta $cmdInvitado) -Encoding Ascii -Value @(
    '@echo off',
    'rem Juego en red: abre la ventana del invitado (jugador 2) para conectarse al PC del anfitrion (ver LEEME.txt).',
    'rem Online play: opens the guest window (player 2) to connect to the host PC (see README.txt).',
    ('start "" "%~dp0{0}" --invitado' -f $Nombre))
# recomplauncher.json: RecompLauncher (instalar con la ISO del jugador, opciones en su ficha) lo lee junto al .exe. Se
# copia tal cual de scripts\paquete (se comprueba con scripts\comprobar-recomplauncher.py); antes, que sea JSON.
$fuenteLauncher = Join-Path $raiz 'scripts\paquete\recomplauncher.json'
if (-not (Test-Path $fuenteLauncher)) { throw "Falta $fuenteLauncher" }
try { Get-Content -LiteralPath $fuenteLauncher -Raw -Encoding UTF8 | ConvertFrom-Json | Out-Null }
catch { throw "$fuenteLauncher no es JSON valido: $($_.Exception.Message)" }
Copy-Item -LiteralPath $fuenteLauncher -Destination (Join-Path $carpeta 'recomplauncher.json') -Force
$carpetaMods = Join-Path $carpeta 'mods'
if (-not (Test-Path $carpetaMods)) { New-Item -ItemType Directory -Path $carpetaMods | Out-Null }

# Licencias: pgs_puente.dll lleva dentro paraLLEl-GS (LGPL-3.0+), asi que el paquete incluye su licencia, la de lo que
# usa (Granite, volk y Vulkan-Headers, MIT/Apache) y el codigo fuente de paraLLEl-GS, de Granite y del puente, para
# que cualquiera pueda recompilar y sustituir la DLL. Se rehace siempre (es pequeno) y solo dentro de licenses\.
$clonPgs = Join-Path $raiz 'third_party\parallel-gs'
$carpetaLicencias = Join-Path $carpeta 'licenses'
if (Test-Path (Join-Path (Split-Path -Parent $exeOrigen) 'pgs_puente.dll')) {
    if (-not (Test-Path (Join-Path $clonPgs 'COPYING.LGPLv3'))) {
        throw "Falta $clonPgs (ejecuta scripts\obtener-parallel-gs.ps1): hace falta para las licencias y el codigo fuente"
    }
    if (Test-Path $carpetaLicencias) { Remove-Item -LiteralPath $carpetaLicencias -Recurse -Force }
    $carpetaFuente = Join-Path $carpetaLicencias 'fuente'
    New-Item -ItemType Directory -Path $carpetaFuente -Force | Out-Null
    Copy-Item (Join-Path $clonPgs 'COPYING.LGPLv3') (Join-Path $carpetaLicencias 'paraLLEl-GS-COPYING.LGPLv3.txt')
    Copy-Item (Join-Path $clonPgs 'Granite\LICENSE') (Join-Path $carpetaLicencias 'Granite-LICENSE.txt')
    Copy-Item (Join-Path $clonPgs 'Granite\third_party\volk\LICENSE.md') (Join-Path $carpetaLicencias 'volk-LICENSE.txt')
    Copy-Item (Join-Path $clonPgs 'Granite\third_party\khronos\vulkan-headers\LICENSE.md') (Join-Path $carpetaLicencias 'Vulkan-Headers-LICENSE.txt')
    Copy-Item (Join-Path $raiz 'gpu\pgs_puente\LICENSE.txt') (Join-Path $carpetaLicencias 'pgs_puente-LICENSE.txt')
    $commitPgs = (& git -C $clonPgs rev-parse HEAD).Trim()
    $commitGranite = (& git -C (Join-Path $clonPgs 'Granite') rev-parse HEAD).Trim()
    & git -C $clonPgs archive --format=zip -o (Join-Path $carpetaFuente "parallel-gs-$($commitPgs.Substring(0, 7)).zip") $commitPgs
    if ($LASTEXITCODE -ne 0) { throw 'git archive de paraLLEl-GS ha fallado' }
    & git -C (Join-Path $clonPgs 'Granite') archive --format=zip -o (Join-Path $carpetaFuente "Granite-$($commitGranite.Substring(0, 7)).zip") $commitGranite
    if ($LASTEXITCODE -ne 0) { throw 'git archive de Granite ha fallado' }
    Compress-Archive -Path (Join-Path $raiz 'gpu\pgs_puente\*') -DestinationPath (Join-Path $carpetaFuente 'pgs_puente.zip') -Force
    $submodulos = (& git -C $clonPgs submodule status --recursive) -join "`r`n"
    Set-Content -Path (Join-Path $carpetaLicencias 'LICENSES.txt') -Encoding UTF8 -Value @"
Licencias de los componentes de terceros de $Nombre
=========================================================

pgs_puente.dll (el GS por tarjeta grafica) incluye:
- paraLLEl-GS (https://github.com/Arntzen-Software/parallel-gs), commit $commitPgs.
  Licencia: GNU LGPL 3.0 o posterior (paraLLEl-GS-COPYING.LGPLv3.txt).
- Granite (https://github.com/Themaister/Granite), commit $commitGranite. Licencia MIT (Granite-LICENSE.txt).
- volk (MIT, volk-LICENSE.txt) y Vulkan-Headers (Apache 2.0 o MIT, Vulkan-Headers-LICENSE.txt).

Codigo fuente (carpeta fuente\): paraLLEl-GS y Granite en esos commits, y el puente pgs_puente (gpu\pgs_puente del
proyecto, con su CMakeLists.txt y compilar.cmd). Para sustituir pgs_puente.dll por una version propia, compila el
puente contra tu copia de paraLLEl-GS y pon la DLL nueva junto a ${Nombre}: el juego solo usa su API C (pgs_puente.h).

Submodulos de paraLLEl-GS en el momento de compilar (git submodule status --recursive):
$submodulos

$Nombre incluye el filtro de imagen fsr: AMD FidelityFX Super Resolution 1 (EASU y RCAS) adaptado a GLSL.
Licencia MIT:
  Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
  documentation files (the "Software"), to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of
  the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
  WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
  OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Las DLL de FFmpeg que acompanan al juego se distribuyen con su propia licencia LGPL.
"@
}
# Aspecto T5: los avisos usan la letra del propio juego (leida de los datos del usuario); el .exe ya no lleva letra
# propia ni su licencia.

$origenDatos = ''
if ($Datos) { $origenDatos = [System.IO.Path]::GetFullPath((Join-Path $raiz $Datos)) }
elseif (-not $Iso -and -not $SinDatos -and -not (Test-Path (Join-Path $carpetaDatos 'SYSTEM.CNF')) -and
    (Test-Path (Join-Path $datosPorDefecto 'SYSTEM.CNF'))) { $origenDatos = $datosPorDefecto }
if ($Iso -or $origenDatos) {
    if ((Test-Path $carpetaDatos) -and -not $Rehacer) {
        Write-Host "data\ ya existe: no se vuelve a copiar (usa -Rehacer para rehacerlo)"
    } else {
        if ($origenDatos -and $origenDatos.TrimEnd('\') -eq $carpetaDatos) { throw "No se puede copiar data\ sobre si mismo" }
        if (Test-Path $carpetaDatos) { Remove-Item $carpetaDatos -Recurse -Force }
        $segundos = Medir {
            if ($Iso) {
                $isoRuta = [System.IO.Path]::GetFullPath($Iso)
                if (-not (Test-Path $isoRuta)) { throw "Falta la ISO $isoRuta" }
                & python -I -X utf8 (Join-Path $raiz 'scripts\iso_ps2.py') extraer $isoRuta $carpetaDatos '*' | Out-Null
                if ($LASTEXITCODE -ne 0) { throw "iso_ps2.py no pudo extraer $isoRuta" }
            } else {
                if (-not (Test-Path (Join-Path $origenDatos 'SYSTEM.CNF'))) { throw "$origenDatos no tiene SYSTEM.CNF" }
                # robocopy: /E subcarpetas, /XD y /XF exclusiones; codigos de salida < 8 son correctos.
                & robocopy $origenDatos $carpetaDatos /E /XD (Join-Path $origenDatos 'mc0') (Join-Path $origenDatos 'mc1') `
                    /XF '*.log' /NFL /NDL /NJH /NJS /NP | Out-Null
                if ($LASTEXITCODE -ge 8) { throw "robocopy no pudo copiar $origenDatos (codigo $LASTEXITCODE)" }
            }
        }
        if (-not (Test-Path (Join-Path $carpetaDatos 'SYSTEM.CNF'))) { throw "data\ no tiene SYSTEM.CNF" }
        Write-Host ("data\ {0} en {1:N0} s" -f $(if ($Iso) { "extraido de $Iso" } else { "copiado de $origenDatos" }), $segundos)
    }
}

$conDatos = (-not $SinDatos) -and (Test-Path (Join-Path $carpetaDatos 'SYSTEM.CNF'))
# Lanzamiento T4: el instalador publico es otra aplicacion (F1-06.iss). lanzamiento-1.2.0 S6: los accesos de los dos
# se llaman "F1 06" (antes, en el publico, "F1 06 PC").
$accesoJuego = 'F1 06'
if ($SinDatos -and (Test-Path $carpetaDatos)) {
    Write-Warning "$carpetaDatos existe: se deja en la carpeta, pero no entra en el zip ni en el instalador"
}
# PowerShell 5 lee este .ps1 como ANSI: las letras con tilde del texto de Windows van como codigos.
$masInformacion = "M$([char]0xE1)s informaci$([char]0xF3)n"
$protegio = "Windows protegi$([char]0xF3) su PC"
# Fase 3 T6: la entrada que el juego muestra en OPCIONES, tal cual se ve en pantalla.
$graficosPc = "GR$([char]0xC1)FICOS PC"
# Fase 5 (T15, N4): la fila ANFITRION de JUEGO EN RED PC (Nativizacion T6; antes en AJUSTES PC) y lo que pregunta el
# Firewall de Windows.
$anfitrionFila = "ANFITRI$([char]0xD3)N"
$publicasFila = "DIRECCIONES P$([char]0xDA)BLICAS"
# Lejania T4-T5: el valor mas alto de DETALLE DE LOS COCHES (GRAFICOS PC).
$maximoFila = "M$([char]0xC1)XIMO"
$leeme = @"
English: see README.txt.

F1 06 (PS2) recompilado para Windows
====================================
Version $version ($fecha)
Que ha cambiado en cada version (en ingles): https://github.com/KevinL50-sysadmin/F1-06-PC/blob/main/CHANGELOG.md

Como jugar
----------
$(if ($conDatos) { "Los datos del juego ya estan en la carpeta data\: no hace falta nada mas." } else { "Este paquete no trae nada del juego: hace falta tu copia de Formula One 06 de PS2, version europea PAL
(SCES-53950), como imagen .iso (unos 4,2 GB; tambien vale una carpeta con el disco ya extraido). Solo se
pide una vez:
- Instalador: en la pagina `"Elige tu copia del juego (ISO)`" busca tu .iso (si esta en Descargas o en el
  Escritorio ya sale puesta). Al instalar se comprueba y se copian sus archivos en data\, con una barra de
  progreso. Si dejas la pagina en blanco, el juego la pide al abrirlo.
- Portable, o si no la elegiste al instalar: la primera vez que abres $Nombre, el juego busca la ISO en
  Descargas y en el Escritorio y te la ofrece; si no la encuentra, eligela en la ventana de Windows (o el
  SYSTEM.CNF de la carpeta del disco extraido). Comprueba que es Formula One 06 PAL, copia sus archivos en la
  carpeta data\ (unos minutos, con una barra de progreso que se puede cancelar) y arranca el juego. Las
  siguientes veces arranca directamente.
- La ISO solo se lee, nunca se cambia: despues puedes guardarla donde quieras. Hacen falta unos 4,3 GB libres.
- Si la copia se corta o la cancelas, no queda nada a medias y el juego la vuelve a pedir. Una carpeta data\
  incompleta no se borra: se aparta como data.anterior\ (puedes borrarla tu).
- Instalado en Archivos de programa sin elegir la ISO al instalar: la carpeta del juego no se puede escribir
  sin permisos de administrador, asi que data\ va a %LOCALAPPDATA%\F1-06\data.
- Cada archivo copiado se comprueba contra el disco (se vuelve a leer despues de escribirlo) y, si sale mal, se
  copia otra vez; si sigue mal tras 3 intentos, la copia falla con un mensaje que dice que archivo.
- Por linea de ordenes: $Nombre --extraer-iso `"ruta\juego.iso`" copia el disco en data\ sin abrir el juego; si
  data\ ya tiene ese disco, comprueba sus archivos y vuelve a copiar solo los danados (codigos de salida: 0 hecho
  o ya estaba, 1 uso incorrecto, 2 no es Formula One 06 PAL, 3 error de lectura, escritura o espacio, o un archivo
  que no se copia bien, 4 cancelado; con --ventana muestra la barra y los errores en ventanas).
  $Nombre --reparar-iso `"ruta\juego.iso`" solo comprueba data\ y vuelve a copiar los archivos danados (los
  demas no se tocan; 5 si no hay una carpeta data\ de ese disco) y $Nombre --comprobar-iso `"ruta\juego.iso`"
  solo comprueba la ISO." })
- Portable (.zip): descomprime la carpeta donde quieras y abre $Nombre. Todo lo tuyo se guarda dentro de esa
  carpeta, asi que puedes moverla o llevarla en un USB.
- Instalado: abre el acceso directo "$accesoJuego" del menu Inicio o del escritorio. Se desinstala desde
  Configuracion > Aplicaciones; pregunta si borrar tambien tus partidas y opciones.

La primera vez, Windows puede avisar con "$protegio" (SmartScreen) porque el .exe no esta firmado:
pulsa "$masInformacion" y luego "Ejecutar de todas formas".

La carpeta licenses\ tiene las licencias de los componentes de terceros (paraLLEl-GS, LGPL) y su codigo fuente.

Donde esta cada cosa
--------------------
Portable: todo en la carpeta del juego. Instalado: en Documentos\Mis juegos\F1-06 (en disco,
Documents\My Games\F1-06), salvo el registro y la cache, que van en %LOCALAPPDATA%\F1-06\logs y
%LOCALAPPDATA%\F1-06\cache.
Las carpetas se crean solas al abrir el juego.

Desde la version 1.1.1, las carpetas y archivos del jugador se llaman en ingles: datos\ es ahora data\,
partidas\ es saves\, registros\ es logs\, capturas\ es screenshots\, licencias\ es licenses\,
config\opciones.ini es config\settings.ini y config\controles.ini es config\controls.ini; LEEME.txt tiene al lado
README.txt (en ingles) y "Jugar en red - Invitado.cmd" es "Play online - Guest.cmd". Lo de dentro no cambia (las
opciones de los .ini, los menus y las ventanas siguen en espanol). Al abrirse, el juego renombra solo las carpetas y
archivos con el nombre viejo (en la carpeta del juego, en Documentos\My Games\F1-06 y en %LOCALAPPDATA%\F1-06), sin
borrar ni mezclar nada: si ya existen los dos nombres, usa el nuevo y deja el viejo como esta (puedes borrarlo tu
cuando veas que no te hace falta). Si no puede renombrar uno (un archivo abierto, o datos\ en Archivos de
programa, que no se puede escribir sin permisos de administrador), sigue usando el viejo. La ventana del invitado
del juego en red no renombra nada. El instalador renombra datos\ a data\ en la carpeta del juego al actualizar.

- $Nombre y las .dll: el juego. No los separes.
- data\: los archivos del disco. No se modifican nunca. $(if ($conDatos) { "" } else { "Los copia el juego de tu ISO la primera vez;
  data\F1-06-disco.txt es la lista con la que comprueba en cada arranque que esta completa." })
- config\settings.ini: ventana, sin bordes o pantalla completa al arrancar, tamano de la ventana, aspecto (auto,
  4:3, 16:9 o pixel), filtro de la imagen (cercano, bilineal, nitido, fsr o crt), sincronizacion vertical (vsync),
  contador de fotogramas por segundo, volumen (0 a 100), si se guarda el registro, el dibujo de los graficos
  (gs = gpu, en la tarjeta grafica con Vulkan, lo normal; gs = cpu, modo Software, mas lento), saltar los videos
  de inicio, la latencia del sonido (auto, baja o alta), sonido mono, la fuerza de la vibracion (0 a 100), el
  filtro para daltonismo (protanopia, deuteranopia o tritanopia), la tecla de las fotos, las cargas rapidas
  (cargas_rapidas = 1: las pantallas de carga acaban en cuanto el juego ha cargado; 0 = al ritmo de la
  consola), el campo de vision de las camaras de la carrera (fov = 0, el del juego; 5 a 30, ese tanto por
  ciento mas abierto), el detalle de los coches lejanos (detalle_coches = 1, como en la PS2; 0 reducido, 2 alto y
  3 maximo), la distancia del escenario (distancia_circuito = 100, como en la PS2; 150 o 200 por ciento), la
  dificultad de los rivales (dificultad_ia = 0, la del juego; 1 a 100, un punto entre
  facil, 0, media, 50, y dificil, 100) y, para el juego en red, el puerto (red_puerto), la calidad de la imagen
  (red_calidad: baja, media o alta) y si acepta direcciones publicas (red_publicas), y el boton de cada accion de
  conduccion (boton_acelerar, boton_frenar, boton_subir_marcha, boton_bajar_marcha y boton_mirar_atras: cruz,
  cuadrado, circulo, l1, r1, l2, r2 o triangulo; con triangulo, la camara pasa al boton que la accion deja libre).
  La contrasena del juego en red
  nunca se guarda. Cada opcion viene explicada dentro; borralo para volver a los valores por defecto. Lo mas comodo es
  cambiarlas desde el propio menu del juego (OPCIONES > OPCIONES PC: $graficosPc, PANTALLA PC, AJUSTES PC,
  CONTROLES PC y JUEGO EN RED PC, ver abajo), que guarda este archivo.
- config\controls.ini: los controles (mando, teclado y volante). Tambien viene explicado dentro. Para jugar dos
  a pantalla partida (SPLIT SCREEN), anade al final una seccion [jugador2] con el mando del segundo jugador
  (mando = 1, el segundo mando) o sus teclas (cruz = tecla:M, ...); sin ella el juego pide el segundo mando. Si tu
  controls.ini es de una version anterior, el ejemplo esta en uno nuevo: renombra el tuyo, abre el juego y copia
  la seccion comentada del final.
  Volante: el juego lo usa como su volante Logitech (Driving Force Pro), con su propia direccion, sus pedales, sus
  botones en los menus (cruz aceptar, circulo atras) y su fuerza de respuesta. Con el volante conectado, el jugador 1
  juega con el. Los botones y ejes se cambian en la seccion [volante] de controls.ini (sin ella, los de un Logitech
  G29). El giro es como el del Driving Force Pro original: 200 grados de tope a tope son todo el giro del juego. En
  [volante], grados_volante es el rango que tiene tu volante en G HUB o en su driver (900 por defecto; pon el mismo
  valor) y giro, los grados de tope a tope del juego (200 por defecto; menos = direccion mas rapida). Si la fuerza
  empuja al reves, abre el juego con la variable F1_VOLANTE_INVERTIR_FUERZA=1; con F1_VOLANTE_STICK=1 el volante
  vuelve a ir como el stick del mando (como en versiones anteriores).
- saves\mc0 y saves\mc1: las tarjetas de memoria (tus partidas). Copialas para hacer una copia de
  seguridad.
- screenshots\: las fotos (F12), en PNG, con la fecha y la hora en el nombre. Se crea con la primera foto.
- logs\F1-06.log: el registro de la ultima partida (la anterior queda en F1-06.anterior.log). Si algo
  falla, es lo que hay que mirar. Si el juego se queda congelado 10 segundos, anota ahi donde estaba parada
  cada parte del juego y, si sigue, cuanto duro el paron; esa espera se cambia con vigilante en
  config\settings.ini (3 a 600 segundos; 0 lo apaga).
  Para enviar un fallo: si juegas desde RecompLauncher y el juego se cierra por un fallo, RecompLauncher te
  ofrece enviar el informe con este registro ya adjunto. Si no, abre un issue en
  https://github.com/KevinL50-sysadmin/F1-06-PC/issues (hace falta una cuenta de GitHub), cuenta que pasaba y
  adjunta F1-06.log. El registro puede llevar tu nombre de usuario de Windows en las rutas.
  Sonido: el juego prueba WASAPI, luego DirectSound y luego WinMM, y se queda con el primero que suena. Con
  audio_backend en config\settings.ini (auto, wasapi, directsound o winmm; variable PS2X_AUDIO_BACKEND) el
  elegido va primero y los otros quedan de respaldo. Las lineas [audio] del registro dicen cual se abrio. Sin
  sonido en Linux (Proton o Wine): prueba audio_backend = directsound o winmm y, si sigue igual, envia F1-06.log.
  Sin sonido o con datos danados (sobre todo si copiaste el disco con Proton o Wine antes de esta version, issue
  #3): repara la carpeta data con $Nombre --reparar-iso `"ruta\juego.iso`"; solo vuelve a copiar los archivos
  danados. Las lineas [arranque] del registro dicen que archivos se repararon.
- cache\vulkan-pipelines.bin: los graficos ya preparados para tu tarjeta grafica (gs = gpu), para que no haya
  tirones la segunda vez que ves cada cosa. Ocupa unos 20 a 50 MB y se puede borrar sin problema: el juego lo
  vuelve a crear (las primeras partidas despues pueden ir a tirones un momento). Tras cambiar de tarjeta grafica o
  actualizar su driver se rehace solo.
- cache\juego\: la letra y los botones de los menus del juego, copiados de tu disco al abrirlo, para que la
  ventana del invitado del juego en red tenga el mismo aspecto. Se puede borrar: el juego los vuelve a copiar.
- mods\: un archivo aqui sustituye al del disco que tenga la misma ruta dentro de data\. Por ejemplo,
  mods\DATA2006\MOVIES\PAL\SINGLMOV.IPF sustituye a data\DATA2006\MOVIES\PAL\SINGLMOV.IPF. Solo archivos
  sueltos del disco (no lo que va dentro de los .WAD). Para quitar un mod, borra su archivo.
- portable.txt (solo en la version portable): hace que todo se guarde en la carpeta del juego. Sin el, el
  juego usa las carpetas de la version instalada.
- Play online - Guest.cmd (solo en la version portable): abre la ventana del invitado del juego en red.
- recomplauncher.json: lo lee RecompLauncher para instalar el juego con tu ISO y mostrar sus opciones en la ficha
  del juego. El juego no lo usa; si no usas RecompLauncher, puedes ignorarlo.
- config\red.ini: la ultima direccion y el puerto a los que se conecto este PC como invitado (nunca la contrasena).

Opciones del PC dentro del juego
--------------------------------
En OPCIONES del menu principal, debajo de OPCIONES DE PANTALLA, esta la entrada OPCIONES PC (PC OPTIONS si el
juego no esta en espanol). Lleva a cinco paginas: $graficosPc (PC GRAPHICS), PANTALLA PC (PC DISPLAY), AJUSTES PC
(PC SETTINGS), CONTROLES PC (PC CONTROLS) y JUEGO EN RED PC (PC NETPLAY); cruz entra en la pagina elegida y, al salir de ella (ACEPTAR o
atras), se vuelve a esta lista. Cada pagina tiene al final una fila VALORES POR DEFECTO (izquierda o derecha) que
devuelve las de esa pagina a los valores de serie.
- ${graficosPc}: filtro de la imagen, supersampling, doble resolucion, sincronizacion vertical, contador de FPS,
  modo de pantalla, volumen general, DETALLE DE LOS COCHES (CAR DETAIL) y DISTANCIA DEL ESCENARIO (SCENERY
  DISTANCE). DETALLE DE LOS COCHES: COMO EN PS2 (de serie: cada coche cambia a un modelo mas sencillo a 20, 40 y
  60 m, como en la consola), REDUCIDO (a menos de la mitad de esas distancias, como las versiones anteriores del
  port), ALTO (al doble de distancia que en la PS2) y $maximoFila (todos los coches con el modelo de cerca, como el
  del jugador). DISTANCIA DEL ESCENARIO: hasta donde se ven gradas, arboles y carteles, COMO EN PS2 (de serie; se
  funden a 150, 300, 450 o 1200 m segun el objeto), LEJOS (1,5 veces) o MUY LEJOS (el doble). Ambas se ven al
  momento, tambien en carrera. Cuanto mas detalle, mas trabajo para el procesador: en un PC de pruebas, en la
  salida con los 22 coches, ALTO cuesta un 16 %, LEJOS un 5 % y MUY LEJOS un 9 %, y $maximoFila baja a unos
  25 fotogramas por segundo.
- PANTALLA PC: forma de la imagen (automatica, 4:3, 16:9 o pixeles cuadrados), tamano de la ventana (los
  habituales que caben en tu escritorio; cuenta en modo ventana) y dibujo de los graficos (GPU o CPU: se aplica al
  reiniciar el juego, y el valor que aun no esta en uso lo dice con AL REINICIAR).
- AJUSTES PC: saltar los videos de inicio, latencia del sonido (automatica, baja o alta), salida de sonido (estereo
  o mono), vibracion, filtro para daltonismo, tecla de foto, HUD de la carrera (completo, minimo u oculto), tecla del
  HUD, cargas rapidas (activadas de serie: la carga de una carrera dura unos 2 s menos, la vuelta al menu unos 3,5 s
  menos y el arranque hasta la presentacion unos 15 s menos; los fundidos y los consejos se ven igual), campo de
  vision (NORMAL o de +5 % a +30 %: las camaras de la carrera, como la cabina, el morro o la persecucion, ven mas a
  los lados; el HUD y los menus no cambian) y tecla del modo foto.
- CONTROLES PC: el boton de ACELERAR, FRENAR, SUBIR MARCHA, BAJAR MARCHA y MIRAR ATRAS, entre X, CUADRADO,
  CIRCULO, L1, R1, L2 y R2 (de serie, los del juego original: X, CUADRADO, R2, L2 y R1). Si eliges un boton que ya
  usa otra accion, se intercambian: nunca hay dos acciones en un boton. Solo cuenta mientras conduces; en la
  repeticion, el modo foto, la pausa y los menus los botones hacen lo de siempre. Acelerar y frenar conservan la
  presion del boton elegido: con los gatillos (RT y LT del mando de Xbox, que son R2 y L2) el acelerador y el freno
  son progresivos. La marcha atras sigue siendo mantener el freno con el coche parado. Es aparte de controls.ini
  (que dice que boton o tecla del PC es cada boton de la PS2). En RecompLauncher esta como "Driving controls", con
  tres esquemas completos.
- JUEGO EN RED PC: $anfitrionFila (APAGADO o ENCENDIDO), puerto, calidad de la imagen (baja, media o alta) y
  $publicasFila (NO o SI); ESTADO (apagado, esperando al invitado, invitado conectado o no se pudo empezar; su
  ayuda, abajo, da el motivo del fallo o la direccion del invitado conectado) y DIRECCION (la de este PC y el
  puerto), que solo informan (izquierda o derecha los actualiza); y CONTRASE$([char]0xD1)A, que se ve en la fila: X la
  escribe con el teclado del juego (de 8 a 32 caracteres) e izquierda o derecha la deja AL AZAR (una nueva al
  encender). Con el anfitrion encendido, el puerto, la calidad y las direcciones publicas no cambian (se aplican al
  encenderlo): apagalo antes. Su VALORES POR DEFECTO devuelve esas tres. Ver "Juego en red" mas abajo.
En la pausa de la carrera, OPCIONES lleva directamente la entrada $graficosPc (debajo de OPCIONES DE PANTALLA) con las
mismas nueve filas, sin VALORES POR DEFECTO; y el menu de pausa tiene, debajo de REPLAY, la entrada MODO FOTO (PHOTO
MODE; ver "Teclas", F8).
Se maneja como el resto de las opciones del juego (cruceta arriba y abajo para elegir, izquierda y derecha para
cambiar); los cambios se ven al momento, ACEPTAR (cruz) los guarda en config\settings.ini y RESTABL. (triangulo)
los deshace.
En DIFICULTAD (DIFFICULTY, antes de cada carrera), la fila de la IA de los rivales (OPPONENT AI) va de FACIL a
DIFICIL en pasos de 10: FACIL, 10, 20, 30, 40, MEDIA, 60, 70, 80, 90 y DIFICIL. Los numeros son puntos entre los
niveles del juego: los rivales van mas rapido cuanto mas alto (en una carrera rapida en Silverstone, la media de
lo que recorren en dos minutos crece de FACIL a DIFICIL un 11 %, y cada paso queda entre los dos de al lado). El
juego guarda su nivel mas cercano (los tiempos de las sesiones que no se corren son los de ese nivel) y el numero
va a config\settings.ini (dificultad_ia); es uno para todos los modos y solo cuenta mientras el nivel del juego
sea el mas cercano a el (si DIFICULTAD GENERAL u otro perfil cambian el nivel, manda el del juego).

Teclas
------
- F11: pantalla completa (sin bordes, o la de pantalla en settings.ini) o ventana.
- F10: cambia el aspecto (4:3, 16:9, pixeles de la consola).
- F12: foto. Guarda la imagen del juego a su resolucion interna (sin el contador de FPS ni los menus del PC) en
  screenshots\; abajo sale un aviso con el nombre. La tecla se cambia en AJUSTES PC o settings.ini (tecla_foto).
- H: cambia el HUD de la carrera: completo, minimo (posiciones, banderas, velocidad y marcha), oculto (nada, para
  fotos sin marcadores) y otra vez completo. No se guarda solo (ACEPTAR en AJUSTES PC). La tecla
  se cambia en AJUSTES PC o settings.ini (tecla_hud: h, f7, f6 o no).
- F8, o MODO FOTO (PHOTO MODE) en el menu de pausa de la carrera o en el de la repeticion: modo foto, en carrera o
  en la repeticion (desde la pausa empieza en cuanto se cierra). Congela el juego (el crono no corre), quita el HUD, silencia el
  sonido y suelta la camara: W S A D la mueven (adelante, atras, a los lados), R y F la suben y la bajan, las
  flechas la giran, Q y E la inclinan, Z y X acercan y alejan, Mayusculas va mas rapido y Control mas despacio;
  Retroceso la devuelve a donde estaba. Con el mando: el stick izquierdo la mueve, el derecho la gira, R2 y L2 la
  suben y la bajan, L1 y R1 la inclinan, la cruceta arriba y abajo acerca y aleja, cruz rapido, cuadrado despacio
  y SELECT la devuelve. Cuanto mas cerca (zoom), mas despacio gira: las camaras de television de la repeticion
  estan muy cerca y empieza igual que ellas. Se aleja como mucho 30 m de donde empezo y no baja mas de 60 cm por debajo (para no
  atravesar el suelo). Como en la repeticion, se puede mirar otro coche y usar las camaras de la carrera: la
  cruceta izquierda y derecha (TAB en el teclado; con Mayusculas, hacia atras) pasa al coche de delante o al de
  detras en el orden de la carrera, dando la vuelta, y triangulo (V) pasa a la siguiente camara de ese coche: las
  de conduccion del juego (cabina, morro, persecucion...), las de la repeticion (rueda, aleron) y la de
  television, y vuelta a empezar. Al elegir piloto sale abajo a la izquierda su rotulo, como el de la repeticion
  (nombre y equipo, y su puesto), unos segundos; al cambiar de camara, abajo un aviso con ella. La camara libre
  sale de alli (se mueve desde ese coche). Al salir vuelven el coche y la camara que se veian. F12 o circulo sacan
  la foto: sin el rotulo, sin la barra de controles y sin los letreros de la repeticion (Replay, TV MODE, el
  logotipo), que en el modo foto no se ven; F8 otra vez o START vuelven a la carrera donde estaba (abajo sale un
  aviso con estos controles al entrar). La tecla
  se cambia en AJUSTES PC o settings.ini (tecla_modo_foto: f8, f5, p o no; si es la misma que la de foto, el
  modo foto no tiene tecla; circulo y START valen siempre).
  Con el mando, R3 (pulsar el stick derecho, solo) entra y sale del modo foto en carrera o en la repeticion, como
  F8; el juego no usa R3 ni L3 en carrera. L3 (pulsar el stick izquierdo, solo) muestra u oculta el aviso de los
  controles en el modo foto y en la repeticion: si estaba a la vista lo quita, y si no lo vuelve a poner hasta
  pulsarlo otra vez; el rotulo del piloto se va y vuelve con el. Los dos actuan al soltarlos; pulsados juntos
  no hacen nada.
- En la repeticion (pausa > REPLAY): K pausa y sigue; L mantenida va adelante x2 y, tras un segundo, x4; J
  mantenida va atras 2 segundos al momento y 2 mas cada cuarto de segundo (la imagen se queda quieta y abajo sale
  hasta donde) y al soltarla salta alli, en pausa si lo estaba. Con el mando: SELECT, R2 y L2 (el juego no los
  recibe mientras dura la repeticion, asi que su TV MODE, que va con SELECT, se queda en Man.). Los del juego
  siguen: triangulo cambia la camara (las de la repeticion y despues las de conduccion del modo foto: cabina,
  morro, persecucion..., de cualquier coche; abajo sale un aviso con ella), la cruceta izquierda y derecha cambia de
  coche y arriba y abajo pasa al de
  delante o al de detras (en el teclado, con los controles de controls.ini: V, A D y W S). Al empezar la
  repeticion sale abajo un aviso con todos estos controles (L3 lo muestra u oculta), y en su menu de pausa (START)
  la entrada CONTROLES (CONTROLS) los explica. El juego no guarda la repeticion como un video: la vuelve a calcular, asi
  que volver atras tarda un poco (en un PC normal, cerca de 1 segundo por cada 6 segundos desde el principio de la
  repeticion, que son como mucho los ultimos 38 segundos de carrera). El sonido calla en pausa y al volver atras.
- Para salir del juego, cierra la ventana (la X o Alt+F4); Esc nunca lo cierra.

Juego en red (dos PC, pantalla partida)
---------------------------------------
Que es: un PC, el anfitrion, ejecuta el juego; el otro, el invitado, ve y oye esa partida en una ventana y manda
su mando, que en el anfitrion es el del jugador 2 a pantalla partida (SPLIT SCREEN). El invitado usa este mismo
$Nombre, pero no necesita los datos del juego.

Requisitos:
- Los dos PC en la misma red local (en casa), o los dos en Tailscale o ZeroTier (redes privadas por internet,
  gratuitas para uso personal). Fuera de casa es lo recomendado: cifran todo y no hay que abrir puertos.
- Sin ellas, por internet, el anfitrion tendria que abrir en su router el puerto TCP 50650 hacia su PC. No se
  recomienda: el puerto queda abierto a internet.
- Seguridad: el juego firma todo lo que viaja, asi que sin la contrasena nadie puede unirse ni cambiar lo que se
  envia; pero no lo cifra, y quien grabe una conexion en tu red puede intentar adivinar la contrasena con su PC,
  sin conectarse. Usa la que genera el juego o una propia de 8 caracteres o mas, y fuera de casa mejor Tailscale
  o ZeroTier.
- El anfitrion solo acepta invitados de la red local y de VPN privadas (Tailscale, direcciones 100.x; ZeroTier).
  Con Hamachi, Radmin VPN o un puerto abierto en el router, pon $publicasFila en SI en OPCIONES > OPCIONES PC >
  JUEGO EN RED PC (o red_publicas = 1 en settings.ini).
- Una conexion que aguante la calidad elegida (media: unos 8 Mbit/s del anfitrion al invitado).

Anfitrion (el PC con el juego):
1. Abre el juego. En OPCIONES > OPCIONES PC > JUEGO EN RED PC pon $anfitrionFila en ENCENDIDO.
2. Arriba sale un aviso unos segundos: "esperando invitado en <direccion>:<puerto> - clave <contrasena>". ESTADO y
   DIRECCION de JUEGO EN RED PC dicen lo mismo, y la contrasena esta siempre en su fila CONTRASE$([char]0xD1)A. Si no se
   puede empezar, ESTADO dice NO SE PUDO EMPEZAR y su ayuda, abajo, el motivo. Dile al invitado la direccion, el
   puerto y la contrasena. La contrasena es
   nueva cada vez que abres el juego (8 letras y numeros, sin O, 0, I, L ni 1 para no confundirlos); antes de
   encender el anfitrion, X en CONTRASE$([char]0xD1)A escribe otra (de 8 a 32 caracteres) con el teclado del juego, e
   izquierda o derecha vuelve a AL AZAR.
3. La primera vez, Windows pregunta si deja a $Nombre usar la red (Firewall de Windows): marca "Redes privadas"
   y pulsa "Permitir" ("Permitir acceso" en Windows 10). El juego nunca cambia el cortafuegos por su cuenta. Si
   dijiste que no, se cambia en Seguridad de Windows > Firewall y proteccion de red > Permitir una aplicacion a
   traves del firewall. Tu red de Windows debe ser privada, no publica.
4. Cuando el invitado entra, sale "invitado conectado desde ...": su mando es el puerto 2. Elige en el menu
   principal el modo de dos jugadores (SPLIT SCREEN): el invitado elige su piloto como PLAYER 2.
5. Para terminar, pon $anfitrionFila en APAGADO en JUEGO EN RED PC: el invitado se desconecta y el puerto 2 vuelve a lo que
   diga controls.ini. Cerrar el juego tambien termina la sesion.

Invitado (el otro PC):
1. Abre "Play online - Guest.cmd", en la carpeta del juego (instalado: "$accesoJuego - Jugar en red (invitado)" en
   el menu Inicio). Tambien vale abrir $Nombre --invitado.
2. Escribe la direccion IP del anfitrion (en casa, como 192.168.1.20; con Tailscale, como 100.x.y.z), el puerto
   (50650 si no lo cambio) y la contrasena, y pulsa Intro o CONECTAR. Tab o las flechas cambian de campo, Ctrl+V
   pega y F4 muestra la contrasena; con el mando, la cruceta cambia de campo y X conecta (para escribir hace falta
   el teclado). La ventana tiene el aspecto de los menus del juego si el juego se ha abierto antes en este PC (usa
   su letra y sus botones, guardados en cache\juego); si no, los mismos colores con otra letra. La direccion y el
   puerto se recuerdan para la proxima vez (config\red.ini); la
   contrasena no se guarda nunca.
3. Tu mando es el jugador 1 de tu controls.ini (teclado, mando o volante). Teclas de la ventana: F3 muestra u
   oculta los datos de la conexion (imagenes por segundo, Mbit/s, ida y vuelta, perdidas, sonido); M silencia el
   sonido; Re Pag y Av Pag (o + y - del teclado numerico) suben y bajan el volumen. Si la sesion acaba, Intro
   (o X en el mando) vuelve al formulario. Para salir, cierra la ventana.

Calidad de la imagen (OPCIONES > OPCIONES PC > JUEGO EN RED PC o red_calidad en settings.ini; se elige con
$anfitrionFila apagado):
- baja: 4 Mbit/s, 360 lineas. Para wifi flojo o internet lento.
- media (la de serie): 8 Mbit/s, 540 lineas.
- alta: 15 Mbit/s, 720 lineas. Para una red local buena, mejor con cable.
El puerto (red_puerto, de serie 50650) se cambia en el mismo sitio; el invitado escribe el mismo.

Si algo falla:
- RECHAZADO, "contrasena incorrecta": escribela otra vez tal cual (distingue mayusculas y minusculas). Cada
  intento fallido desde la misma direccion hace esperar mas al siguiente (1, 2, 4... hasta 30 segundos).
- RECHAZADO, "solo acepta redes locales y VPN privadas": el invitado llega desde una direccion publica (Hamachi,
  Radmin VPN o internet). El anfitrion pone $publicasFila en SI en OPCIONES > OPCIONES PC > JUEGO EN RED PC
  (red_publicas = 1).
- RECHAZADO, "version del protocolo distinta": los dos PC deben tener la misma version de $Nombre.
- RECHAZADO, "el anfitrion ya tiene un invitado": solo cabe un invitado a la vez.
- ERROR o tiempo agotado al conectar: revisa la direccion y el puerto, que el anfitrion tenga JUEGO EN RED en
  ${anfitrionFila}, que esteis en la misma red (o los dos en Tailscale o ZeroTier) y que el Firewall de Windows del
  anfitrion permita el juego en redes privadas.
- La imagen va a saltos o con retraso: baja la calidad (apaga $anfitrionFila, cambia la calidad en OPCIONES >
  OPCIONES PC > JUEGO EN RED PC y vuelve a encenderlo; el invitado vuelve a conectarse), y mejor cable que wifi. Con F3 el invitado ve las
  imagenes perdidas y la ida y vuelta.
- El sonido se corta: la red no llega; baja tambien la calidad de la imagen.

Para usuarios avanzados
-----------------------
Variables de entorno (mandan sobre settings.ini): PS2X_TARJETAS=<carpeta> (tarjetas en otra carpeta),
PS2X_ISO=<ruta> (jugar desde una ISO), PS2X_MODS=<carpeta> (mods de otra carpeta),
PS2X_DETALLE_COCHES=<0 a 3> (detalle de los coches lejanos, como detalle_coches) y
PS2X_DISTANCIA_CIRCUITO=<100, 150 o 200> (distancia del escenario, como distancia_circuito).
Juego en red: PS2X_RED_CLAVE=<contrasena> (la del anfitrion en vez de una al azar, o la que rellena el formulario
del invitado), PS2X_RED_ESCUCHAR=<direccion> (el anfitrion solo escucha en esa direccion; 127.0.0.1 para probar
en un solo PC sin que Windows pregunte por el cortafuegos), PS2X_RED_PUBLICAS=1 (el anfitrion acepta tambien
direcciones publicas). Por linea de ordenes: $Nombre --anfitrion --clave <contrasena de 8 o mas> [--puerto N]
[--escuchar <direccion>] [--red-publicas] y $Nombre --invitado [<direccion>[:puerto]] [--clave <contrasena>].
Pruebas sin nadie delante: PS2X_INVITADO_GUION="evento[#n]+segundos:boton[:duracion],..." pulsa el mando del
invitado tras los sucesos de su ventana (formulario, conectando, contrasena, conectado, rechazado, desconectado,
error; botones como cruz, abajo o start, captura para guardar la ventana en <PS2X_INVITADO_CAPTURAS>-<n>.png o
cerrar para cerrarla). Un guion mal escrito cierra el invitado con el motivo en su registro.

Requisitos
----------
Windows 10/11 de 64 bits. Necesitas tu propia copia del juego (Formula One 06 de PS2, PAL, SCES-53950, en
.iso) y unos 4,3 GB libres para sus archivos. Una tarjeta grafica con Vulkan para el modo
normal; sin ella el juego dibuja en modo Software (mas lento).

Procesador: lo que mas cuenta es la velocidad de un nucleo. Si no llega, el juego no se salta fotogramas: va a
camara lenta. Lo que sabemos hasta ahora:
- Recomendado (segun los jugadores): un procesador de sobremesa como un Core i5-6600K o un Ryzen 5 3400G, o mas
  nuevo.
- Minimo: sin establecer. Las CPU de estacion de trabajo antiguas y las de portatil de bajo consumo van por debajo
  de 50 fotogramas por segundo en carrera.
- Medido en el PC del desarrollador (Core i7-13700): 50 en sus nucleos de rendimiento; solo con sus 8 nucleos
  eficientes, como si fuera una CPU mas lenta, 25-33 en carrera con la 1.1.2 y 41-43 con la 1.1.3 (+22-25 %).
- Medido en el log de un jugador: Xeon E5-2687W (2012, 3,0 GHz), unos 20 en carrera con la 1.1.2. La mejora de la
  1.1.3 esta medida en los nucleos eficientes de arriba, aun no en esa CPU.
- Segun los jugadores (RecompLauncher, "va bien", sin cifras): Core i5-6600K + GTX 1080, Ryzen 5 3400G + RX 6600,
  Ryzen 5 5600GT con su grafica integrada, Ryzen 5 5500, Ryzen 7 5700X, Core i5-14600K, Xeon E5-2667 v4, entre
  otros. Con problemas: un portatil con Ryzen 5 3500U (Vega 8, 6 GB de RAM).
Tarjeta grafica: cualquiera con Vulkan; con los valores de serie (supersampling x2, doble resolucion apagada) va
bien hasta con la grafica integrada de un Ryzen 5 5600GT, segun su jugador.

Rendimiento: el juego va a 50 fotogramas por segundo, como el original. La salida con los 22 coches es el momento
mas exigente: con el plan de energia Equilibrado de Windows y otros programas abiertos el procesador puede bajar
su frecuencia y la salida caer a unos 42-45 fotogramas por segundo durante unos segundos. Para ir siempre a 50,
usa el plan Alto rendimiento (Configuracion > Sistema > Energia) o cierra lo que no uses mientras juegas.
- Deja el dibujo de los graficos en GPU (el de serie); el modo Software (CPU) es mucho mas lento.
- El supersampling y la doble resolucion solo cargan la tarjeta grafica: subelos si le sobra, bajalos si va muy
  cargada. Con un procesador justo no ayudan.
- Programas de captura o streaming (Sunshine, OBS...): en la 1.1.2, con Sunshine abierto en el PC de un jugador, los
  menus iban a tirones porque el juego esperaba a la tarjeta grafica en cada imagen. Desde la 1.1.3 ya no espera
  cuando la tarjeta va retrasada: se salta esa copia. Si aun ves tirones con uno de ellos abierto, puedes probar
  esto (no esta confirmado que lo arregle): Panel de control de NVIDIA > Administrar la configuracion 3D >
  Configuracion de programa > F1-06.exe > metodo de presentacion de Vulkan/OpenGL (Vulkan/OpenGL present method)
  en Preferir nativo (Prefer native). O cierra el programa mientras juegas en el PC.
$(if ($conDatos) { "
AVISO: la carpeta data\ contiene los archivos de tu disco. Es solo para uso personal: no la compartas." } else { "" })
"@
Set-Content -Path (Join-Path $carpeta 'LEEME.txt') -Value $leeme -Encoding UTF8
# player-folders T3 (S4): README.txt, la traduccion completa al ingles de LEEME.txt (mismas secciones y contenido; al
# cambiar uno, cambiar el otro). Va aqui y no en una plantilla aparte porque los dos usan las mismas variables y
# condiciones del script ($version, $Nombre, $accesoJuego, $conDatos...). Los rotulos del juego van en ingles (los que
# muestra con Windows en ingles); los valores de los .ini y los mensajes en espanol se citan tal cual.
$readme = @"
Spanish: see LEEME.txt (en espanol).

F1 06 (PS2) recompiled for Windows
==================================
Version $version ($fecha)
What changed in each version: https://github.com/KevinL50-sysadmin/F1-06-PC/blob/main/CHANGELOG.md

How to play
-----------
$(if ($conDatos) { "The game data is already in the data\ folder: nothing else is needed." } else { "This package includes nothing from the game: you need your own copy of Formula One 06 for PS2, European PAL
version (SCES-53950), as an .iso image (about 4.2 GB; a folder with the disc already extracted also works). It is
only asked for once:
- Installer: on the page `"Choose your copy of the game (ISO)`" browse to your .iso (if it is in Downloads or on
  the Desktop it is already filled in). During the installation it is checked and its files are copied to data\,
  with a progress bar. If you leave the page blank, the game asks for it when you open it.
- Portable, or if you did not choose it when installing: the first time you open $Nombre, the game looks for the
  ISO in Downloads and on the Desktop and offers it to you; if it does not find it, choose it in the Windows
  window (or the SYSTEM.CNF of the extracted disc folder). It checks that it is Formula One 06 PAL, copies its
  files to the data\ folder (a few minutes, with a progress bar that can be cancelled) and starts the game. The
  next times it starts directly.
- The ISO is only read, never changed: afterwards you can keep it wherever you like. About 4.3 GB of free space
  is needed.
- If the copy is interrupted or you cancel it, nothing is left half done and the game asks for it again. An
  incomplete data\ folder is not deleted: it is moved aside as data.anterior\ (you can delete it yourself).
- Installed in Program Files without choosing the ISO when installing: the game folder cannot be written without
  administrator rights, so data\ goes to %LOCALAPPDATA%\F1-06\data.
- Every copied file is checked against the disc (it is read back after it is written) and copied again if it
  comes out wrong; if it is still wrong after 3 attempts, the copy fails with a message naming the file.
- From the command line: $Nombre --extraer-iso `"path\game.iso`" copies the disc to data\ without opening the
  game; if data\ already has that disc, it checks its files and copies again only the damaged ones (exit codes:
  0 done or already there, 1 wrong usage, 2 not Formula One 06 PAL, 3 read, write or space error, or a file that
  does not copy right, 4 cancelled; with --ventana it shows the progress bar and the errors in windows).
  $Nombre --reparar-iso `"path\game.iso`" only checks data\ and copies the damaged files again (the others are
  not touched; 5 if there is no data\ folder of that disc) and $Nombre --comprobar-iso `"path\game.iso`" only
  checks the ISO." })
- Portable (.zip): unzip the folder wherever you like and open $Nombre. Everything of yours is saved inside that
  folder, so you can move it or carry it on a USB stick.
- Installed: open the `"$accesoJuego`" shortcut in the Start menu or on the desktop. It is uninstalled from
  Settings > Apps; it asks whether to delete your saves and options too.

The first time, Windows may warn with "Windows protected your PC" (SmartScreen) because the .exe is not signed:
click "More info" and then "Run anyway".

The licenses\ folder has the licenses of the third-party components (paraLLEl-GS, LGPL) and their source code.

Where everything is
-------------------
Portable: everything in the game folder. Installed: in Documents\My Games\F1-06 (shown as Documentos\Mis juegos\F1-06
on a Spanish Windows), except the log and the cache, which go to %LOCALAPPDATA%\F1-06\logs and
%LOCALAPPDATA%\F1-06\cache.
The folders are created by themselves when you open the game.

From version 1.1.1, the player's folders and files have English names: datos\ is now data\, partidas\ is
saves\, registros\ is logs\, capturas\ is screenshots\, licencias\ is licenses\, config\opciones.ini is
config\settings.ini and config\controles.ini is config\controls.ini; LEEME.txt (Spanish) now has README.txt
(English) next to it and "Jugar en red - Invitado.cmd" is "Play online - Guest.cmd". What is inside does not
change (the options in the .ini files, the menus and the windows are still in Spanish). When it opens, the game
renames the folders and files with the old name by itself (in the game folder, in Documents\My Games\F1-06 and in
%LOCALAPPDATA%\F1-06), without deleting or merging anything: if both names already exist, it uses the new one and
leaves the old one as it is (you can delete it yourself once you see you do not need it). If it cannot rename one
(an open file, or datos\ in Program Files, which cannot be written without administrator rights), it keeps using
the old one. The online play guest window renames nothing. The installer renames datos\ to data\ in the game
folder when updating.

- $Nombre and the .dll files: the game. Do not separate them.
- data\: the disc files. They are never modified. $(if ($conDatos) { "" } else { "The game copies them from your ISO the first time;
  data\F1-06-disco.txt is the list it uses to check at every start that it is complete." })
- config\settings.ini: windowed, borderless or full screen at start, window size, aspect (auto, 4:3, 16:9 or
  pixel), image filter (cercano, bilineal, nitido, fsr or crt), vertical sync (vsync), frames per second
  counter, volume (0 to 100), whether the log is saved, the graphics renderer (gs = gpu, on the graphics card with
  Vulkan, the normal one; gs = cpu, Software mode, slower), skipping the intro videos, the audio latency (auto,
  baja or alta), mono sound, the vibration strength (0 to 100), the colour blindness filter (protanopia,
  deuteranopia or tritanopia), the photo key, fast loading (cargas_rapidas = 1: the loading screens end as soon
  as the game has loaded; 0 = at the console's pace), the field of view of the race cameras (fov = 0, the game's;
  5 to 30, that percentage wider), the detail of distant cars (detalle_coches = 1, as on the PS2; 0 reduced,
  2 high and 3 maximum), the scenery distance (distancia_circuito = 100, as on the PS2; 150 or 200 percent), the
  opponents' difficulty (dificultad_ia = 0, the game's; 1 to 100, a point between easy, 0, medium, 50, and
  hard, 100), for online play, the port (red_puerto), the picture quality (red_calidad: baja, media or alta)
  and whether it accepts public addresses (red_publicas), and the button for each driving action (boton_acelerar,
  boton_frenar, boton_subir_marcha, boton_bajar_marcha and boton_mirar_atras: cruz, cuadrado, circulo, l1, r1, l2,
  r2 or triangulo; with triangulo, the camera moves to the button that action leaves free). The online play password
  is never saved. Each option
  is explained inside (in Spanish); delete the file to go back to the default values. The easiest way is to
  change them from the game's own menu (OPTIONS > PC OPTIONS: PC GRAPHICS, PC DISPLAY, PC SETTINGS, PC CONTROLS
  and PC NETPLAY, see below), which saves this file.
- config\controls.ini: the controls (gamepad, keyboard and steering wheel). It is also explained inside. To play
  two players on split screen (SPLIT SCREEN), add at the end a [jugador2] section with the second player's
  gamepad (mando = 1, the second gamepad) or their keys (cruz = tecla:M, ...); without it the game asks for the
  second gamepad. If your controls.ini is from an earlier version, the example is in a new one: rename yours,
  open the game and copy the commented section at the end.
  Steering wheel: the game uses it as its Logitech wheel (Driving Force Pro), with its own steering, its pedals,
  its buttons in the menus (cross accept, circle back) and its force feedback. With the wheel connected, player 1
  plays with it. The buttons and axes are changed in the [volante] section of controls.ini (without it, those of
  a Logitech G29). The rotation is like that of the original Driving Force Pro: 200 degrees lock to lock are the
  game's whole steering. In [volante], grados_volante is the range your wheel has in G HUB or in its driver (900
  by default; set the same value) and giro, the game's degrees lock to lock (200 by default; less = quicker
  steering). If the force pushes the wrong way, open the game with the variable F1_VOLANTE_INVERTIR_FUERZA=1;
  with F1_VOLANTE_STICK=1 the wheel works like the gamepad stick again (as in earlier versions).
- saves\mc0 and saves\mc1: the memory cards (your saves). Copy them to make a backup.
- screenshots\: the photos (F12), in PNG, with the date and time in the name. It is created with the first photo.
- logs\F1-06.log: the log of the last session (the previous one stays in F1-06.anterior.log). If something
  fails, it is what has to be looked at. If the game freezes for 10 seconds, it writes there where every part
  of the game was stuck and, if it carries on, how long the freeze lasted; change that wait with vigilante in
  config\settings.ini (3 to 600 seconds; 0 turns it off).
  To report a problem: if you play from RecompLauncher and the game closes because of a failure, RecompLauncher
  offers to send the report with this log already attached. Otherwise, open an issue at
  https://github.com/KevinL50-sysadmin/F1-06-PC/issues (a GitHub account is needed), say what was happening and
  attach F1-06.log. The log may contain your Windows user name in its paths.
  Sound: the game tries WASAPI, then DirectSound, then WinMM, and keeps the first one that plays. With
  audio_backend in config\settings.ini (auto, wasapi, directsound or winmm; variable PS2X_AUDIO_BACKEND) the
  chosen one goes first and the others stay as fallback. The [audio] lines in the log say which one opened. No
  sound on Linux (Proton or Wine): try audio_backend = directsound or winmm and, if nothing changes, send F1-06.log.
  No sound or damaged data (above all if you copied the disc with Proton or Wine before this version, issue #3):
  repair the data folder with $Nombre --reparar-iso `"path\game.iso`"; it only copies the damaged files again.
  The [arranque] lines of the log say which files were repaired.
- cache\vulkan-pipelines.bin: the graphics already prepared for your graphics card (gs = gpu), so that there is
  no stutter the second time you see each thing. It takes about 20 to 50 MB and can be deleted without problem:
  the game creates it again (the first races afterwards may stutter for a moment). After changing graphics card
  or updating its driver it is rebuilt by itself.
- cache\juego\: the font and the buttons of the game's menus, copied from your disc when it opens, so that the
  online play guest window has the same look. It can be deleted: the game copies them again.
- mods\: a file here replaces the disc file with the same path inside data\. For example,
  mods\DATA2006\MOVIES\PAL\SINGLMOV.IPF replaces data\DATA2006\MOVIES\PAL\SINGLMOV.IPF. Only loose files of the
  disc (not what goes inside the .WAD files). To remove a mod, delete its file.
- portable.txt (only in the portable version): makes everything be saved in the game folder. Without it, the
  game uses the folders of the installed version.
- Play online - Guest.cmd (only in the portable version): opens the online play guest window.
- recomplauncher.json: read by RecompLauncher to set the game up from your ISO and show its options on the game's
  page. The game itself does not use it; if you do not use RecompLauncher, you can ignore it.
- config\red.ini: the last address and port this PC connected to as a guest (never the password).

PC options inside the game
--------------------------
In OPTIONS in the main menu, below the display options (OPCIONES DE PANTALLA in Spanish), is the PC OPTIONS entry
(OPCIONES PC if the game is in Spanish). It leads to five pages: PC GRAPHICS (GRAFICOS PC), PC DISPLAY (PANTALLA
PC), PC SETTINGS (AJUSTES PC), PC CONTROLS (CONTROLES PC) and PC NETPLAY (JUEGO EN RED PC); cross enters the chosen page and, when leaving it
(accept or back), you return to this list. Each page has at the end a DEFAULT VALUES row (left or right) that
sets the options of that page back to their defaults.
- PC GRAPHICS: image filter, supersampling, double resolution, vertical sync, FPS counter, screen mode, master
  volume, CAR DETAIL and SCENERY DISTANCE. CAR DETAIL: AS ON PS2 (the default: each car switches to a simpler
  model at 20, 40 and 60 m, as on the console), REDUCED (at less than half those distances, like the earlier
  versions of the port), HIGH (at twice the distance of the PS2) and MAXIMUM (all cars with the close-up model,
  like the player's). SCENERY DISTANCE: how far stands, trees and billboards are seen, AS ON PS2 (the default;
  they fade out at 150, 300, 450 or 1200 m depending on the object), FAR (1.5 times) or VERY FAR (twice). Both
  show at once, also in the race. The more detail, the more work for the processor: on a test PC, at the start
  with the 22 cars, HIGH costs 16 %, FAR 5 % and VERY FAR 9 %, and MAXIMUM drops to about 25 frames per second.
- PC DISPLAY: picture shape (automatic, 4:3, 16:9 or square pixels), window size (the usual ones that fit on your
  desktop; it counts in windowed mode) and graphics renderer (GPU or CPU: it applies when the game is restarted,
  and the value not yet in use says so with ON RESTART).
- PC SETTINGS: skip intro videos, audio latency (automatic, low or high), sound output (stereo or mono),
  vibration, colour blindness filter, photo key, race HUD (full, minimal or hidden), HUD key, fast loading (on by
  default: loading a race takes about 2 s less, going back to the menu about 3.5 s less and the start up to the
  presentation about 15 s less; the fades and the tips are shown the same), field of view (NORMAL or from +5 % to
  +30 %: the race cameras, such as the cockpit, the nose or the chase one, see more to the sides; the HUD and the
  menus do not change) and photo mode key.
- PC CONTROLS: the button for ACCELERATE, BRAKE, GEAR UP, GEAR DOWN and LOOK BEHIND, among X, SQUARE, CIRCLE, L1,
  R1, L2 and R2 (by default, the original game's: X, SQUARE, R2, L2 and R1). If you choose a button another action
  already uses, the two swap: there are never two actions on one button. It only counts while driving; in replays,
  photo mode, the pause and the menus the buttons do what they always do. Accelerate and brake keep the pressure of
  the chosen button: with the triggers (RT and LT on an Xbox controller, which are R2 and L2) throttle and brake
  are progressive. Reverse is still holding the brake with the car stopped. It is separate from controls.ini (which
  says which PC button or key is each PS2 button). In RecompLauncher it is "Driving controls", with three complete
  layouts.
- PC NETPLAY: HOST (OFF or ON), port, picture quality (low, medium or high) and PUBLIC ADDRESSES (NO or YES);
  STATE (off, waiting for the guest, guest connected or could not start; its help, below, gives the reason for
  the failure or the address of the connected guest) and ADDRESS (this PC's and the port), which only inform
  (left or right refreshes them); and PASSWORD, which is shown in the row: X types it with the game's keyboard
  (8 to 32 characters) and left or right sets it to RANDOM (a new one when turned on). With the host on, the
  port, the quality and the public addresses do not change (they apply when it is turned on): turn it off first.
  Its DEFAULT VALUES resets those three. See "Online play" below.
In the race pause, OPTIONS leads directly to the PC GRAPHICS entry (below the display options) with the same nine
rows, without DEFAULT VALUES; and the pause menu has, below REPLAY, the PHOTO MODE entry (see "Keys", F8).
It works like the rest of the game's options (d-pad up and down to choose, left and right to change); the changes
show at once, accept (cross) saves them in config\settings.ini and reset (triangle) undoes them.
In DIFFICULTY (before each race), the OPPONENT AI row goes from EASY to HARD in steps of 10: EASY, 10, 20, 30, 40,
MEDIUM, 60, 70, 80, 90 and HARD. The numbers are points between the game's levels: the opponents go faster the
higher it is (in a quick race at Silverstone, the average distance they cover in two minutes grows 11 % from EASY
to HARD, and each step falls between the two next to it). The game saves its nearest level (the times of the
sessions that are not raced are those of that level) and the number goes to config\settings.ini (dificultad_ia);
it is one for all modes and only counts while the game's level is the nearest one to it (if the general
difficulty or another profile changes the level, the game's level rules).

Keys
----
- F11: full screen (borderless, or the one set in pantalla in settings.ini) or window.
- F10: changes the aspect (4:3, 16:9, console pixels).
- F12: photo. Saves the game image at its internal resolution (without the FPS counter or the PC menus) in
  screenshots\; a notice with the name appears at the bottom. The key is changed in PC SETTINGS or settings.ini
  (tecla_foto).
- H: changes the race HUD: full, minimal (positions, flags, speed and gear), hidden (nothing, for photos without
  displays) and full again. It is not saved by itself (accept in PC SETTINGS). The key is changed in PC SETTINGS
  or settings.ini (tecla_hud: h, f7, f6 or no).
- F8, or PHOTO MODE in the race pause menu or in the replay one: photo mode, in the race or in the replay (from
  the pause it starts as soon as the menu closes). It freezes the game (the clock does not run), removes the HUD,
  mutes the sound and frees the camera: W S A D move it (forward, back, sideways), R and F raise and lower it,
  the arrows turn it, Q and E tilt it, Z and X zoom in and out, Shift goes faster and Control slower; Backspace
  puts it back where it was. With the gamepad: the left stick moves it, the right one turns it, R2 and L2 raise
  and lower it, L1 and R1 tilt it, d-pad up and down zoom in and out, cross fast, square slow and SELECT puts it
  back. The closer (zoom), the slower it turns: the television cameras of the replay are very close and it starts
  just like them. It goes at most 30 m from where it started and no lower than 60 cm below (so as not to go
  through the ground). As in the replay, you can look at another car and use the race cameras: d-pad left and
  right (TAB on the keyboard; with Shift, backwards) switches to the car in front or behind in the race order,
  wrapping around, and triangle (V) switches to the next camera of that car: the game's driving ones (cockpit,
  nose, chase...), the replay ones (wheel, wing) and the television one, and round again. When you choose a
  driver, their caption appears at the bottom left, like the replay one (name and team, and their position), for
  a few seconds; when you change camera, a notice with it at the bottom. The free camera starts from there (it
  moves from that car). When leaving, the car and the camera that were shown come back. F12 or circle take the
  photo: without the caption, without the controls bar and without the replay labels (Replay, TV MODE, the logo),
  which are not shown in photo mode; F8 again or START go back to the race where it was (a notice with these
  controls appears at the bottom on entering). The key is changed in PC SETTINGS or settings.ini
  (tecla_modo_foto: f8, f5, p or no; if it is the same as the photo key, photo mode has no key; circle and START
  always work).
  With the gamepad, R3 (pressing the right stick, alone) enters and leaves photo mode in the race or in the
  replay, like F8; the game does not use R3 or L3 in the race. L3 (pressing the left stick, alone) shows or hides
  the controls notice in photo mode and in the replay: if it was visible it removes it, and if not it shows it
  again until it is pressed again; the driver caption goes and comes back with it. Both act on release; pressed
  together they do nothing.
- In the replay (pause > REPLAY): K pauses and resumes; L held goes forward x2 and, after a second, x4; J held
  goes back 2 seconds at once and 2 more every quarter of a second (the image stays still and the bottom shows how
  far) and on release it jumps there, paused if it was. With the gamepad: SELECT, R2 and L2 (the game does not
  receive them while the replay lasts, so its TV MODE, which uses SELECT, stays at Man.). The game's own ones
  still work: triangle changes the camera (the replay ones and then the driving ones of photo mode: cockpit, nose,
  chase..., of any car; a notice with it appears at the bottom), d-pad left and right changes car and up and down
  switches to the one in front or behind (on the keyboard, with the controls of controls.ini: V, A D and W S).
  When the replay starts, a notice with all these controls appears at the bottom (L3 shows or hides it), and in
  its pause menu (START) the CONTROLS entry explains them. The game does not save the replay as a video: it
  calculates it again, so going back takes a little while (on a normal PC, about 1 second for every 6 seconds
  since the start of the replay, which is at most the last 38 seconds of the race). The sound is silent while
  paused and when going back.
- To quit the game, close the window (the X or Alt+F4); Esc never closes it.

Online play (two PCs, split screen)
-----------------------------------
What it is: one PC, the host, runs the game; the other, the guest, sees and hears that game in a window and sends
its gamepad, which on the host is player 2's on split screen (SPLIT SCREEN). The guest uses this same $Nombre,
but does not need the game data.

Requirements:
- Both PCs on the same local network (at home), or both on Tailscale or ZeroTier (private networks over the
  internet, free for personal use). Away from home it is the recommended way: they encrypt everything and no
  ports have to be opened.
- Without them, over the internet, the host would have to open TCP port 50650 to their PC on their router. Not
  recommended: the port stays open to the internet.
- Security: the game signs everything that travels, so without the password nobody can join or change what is
  sent; but it does not encrypt it, and someone who records a connection on your network can try to guess the
  password with their PC, without connecting. Use the one the game generates or your own one of 8 characters or
  more, and away from home better Tailscale or ZeroTier.
- The host only accepts guests from the local network and from private VPNs (Tailscale, 100.x addresses;
  ZeroTier). With Hamachi, Radmin VPN or a port opened on the router, set PUBLIC ADDRESSES to YES in OPTIONS >
  PC OPTIONS > PC NETPLAY (or red_publicas = 1 in settings.ini).
- A connection that can carry the chosen quality (medium: about 8 Mbit/s from the host to the guest).

Host (the PC with the game):
1. Open the game. In OPTIONS > PC OPTIONS > PC NETPLAY set HOST to ON.
2. A notice appears at the top for a few seconds: "esperando invitado en <address>:<port> - clave <password>"
   (waiting for the guest). STATE and ADDRESS in PC NETPLAY say the same, and the password is always in its
   PASSWORD row. If it cannot start, STATE says COULD NOT START and its help, below, the reason. Tell the guest
   the address, the port and the password. The password is new every time you open the game (8 letters and
   digits, without O, 0, I, L or 1 so they are not confused); before turning the host on, X on PASSWORD types
   another one (8 to 32 characters) with the game's keyboard, and left or right goes back to RANDOM.
3. The first time, Windows asks whether to let $Nombre use the network (Windows Firewall): tick "Private
   networks" and click "Allow" ("Allow access" on Windows 10). The game never changes the firewall by itself. If
   you said no, it is changed in Windows Security > Firewall & network protection > Allow an app through
   firewall. Your Windows network must be private, not public.
4. When the guest joins, "invitado conectado desde ..." (guest connected from ...) appears: their gamepad is
   port 2. Choose the two-player mode (SPLIT SCREEN) in the main menu: the guest chooses their driver as PLAYER 2.
5. To finish, set HOST to OFF in PC NETPLAY: the guest is disconnected and port 2 goes back to what controls.ini
   says. Closing the game also ends the session.

Guest (the other PC):
1. Open "Play online - Guest.cmd", in the game folder (installed: `"$accesoJuego - Jugar en red (invitado)`" in
   the Start menu). Opening $Nombre --invitado also works.
2. Type the IP address of the host (at home, like 192.168.1.20; with Tailscale, like 100.x.y.z), the port (50650
   if it was not changed) and the password, and press Enter or CONECTAR (connect). Tab or the arrows change
   field, Ctrl+V pastes and F4 shows the password; with the gamepad, the d-pad changes field and X connects
   (typing needs the keyboard). The window has the look of the game's menus if the game has been opened before on
   this PC (it uses its font and its buttons, saved in cache\juego); if not, the same colours with another font.
   The address and the port are remembered for next time (config\red.ini); the password is never saved.
3. Your gamepad is player 1 of your controls.ini (keyboard, gamepad or wheel). Keys of the window: F3 shows or
   hides the connection data (frames per second, Mbit/s, round trip, losses, sound); M mutes the sound; Page Up
   and Page Down (or + and - on the numeric keypad) raise and lower the volume. If the session ends, Enter (or X
   on the gamepad) goes back to the form. To quit, close the window.

Picture quality (OPTIONS > PC OPTIONS > PC NETPLAY or red_calidad in settings.ini; chosen with HOST off):
- low (baja): 4 Mbit/s, 360 lines. For weak wifi or slow internet.
- medium (media, the default): 8 Mbit/s, 540 lines.
- high (alta): 15 Mbit/s, 720 lines. For a good local network, better with a cable.
The port (red_puerto, 50650 by default) is changed in the same place; the guest types the same one.

If something fails:
- RECHAZADO (rejected), "contrasena incorrecta" (wrong password): type it again exactly as it is (upper and
  lower case are different). Each failed attempt from the same address makes the next one wait longer (1, 2,
  4... up to 30 seconds).
- RECHAZADO, "solo acepta redes locales y VPN privadas" (only accepts local networks and private VPNs): the guest
  comes from a public address (Hamachi, Radmin VPN or the internet). The host sets PUBLIC ADDRESSES to YES in
  OPTIONS > PC OPTIONS > PC NETPLAY (red_publicas = 1).
- RECHAZADO, "version del protocolo distinta" (different protocol version): both PCs must have the same version
  of $Nombre.
- RECHAZADO, "el anfitrion ya tiene un invitado" (the host already has a guest): only one guest fits at a time.
- ERROR or timeout when connecting: check the address and the port, that the host has HOST ON in PC NETPLAY, that
  you are on the same network (or both on Tailscale or ZeroTier) and that the Windows Firewall of the host allows
  the game on private networks.
- The picture jumps or lags: lower the quality (turn HOST off, change the quality in OPTIONS > PC OPTIONS > PC
  NETPLAY and turn it on again; the guest connects again), and better a cable than wifi. With F3 the guest sees
  the lost frames and the round trip.
- The sound cuts out: the network cannot keep up; lower the picture quality too.

For advanced users
------------------
Environment variables (they override settings.ini): PS2X_TARJETAS=<folder> (memory cards in another folder),
PS2X_ISO=<path> (play from an ISO), PS2X_MODS=<folder> (mods from another folder),
PS2X_DETALLE_COCHES=<0 to 3> (detail of distant cars, like detalle_coches) and
PS2X_DISTANCIA_CIRCUITO=<100, 150 or 200> (scenery distance, like distancia_circuito).
Online play: PS2X_RED_CLAVE=<password> (the host's instead of a random one, or the one that fills in the guest's
form), PS2X_RED_ESCUCHAR=<address> (the host only listens on that address; 127.0.0.1 to test on a single PC
without Windows asking about the firewall), PS2X_RED_PUBLICAS=1 (the host also accepts public addresses). From
the command line: $Nombre --anfitrion --clave <password of 8 or more> [--puerto N] [--escuchar <address>]
[--red-publicas] and $Nombre --invitado [<address>[:port]] [--clave <password>].
Unattended tests: PS2X_INVITADO_GUION="evento[#n]+segundos:boton[:duracion],..." presses the guest's gamepad
after the events of its window (formulario, conectando, contrasena, conectado, rechazado, desconectado, error;
buttons such as cruz, abajo or start, captura to save the window in <PS2X_INVITADO_CAPTURAS>-<n>.png or cerrar to
close it). A badly written script closes the guest with the reason in its log.

Requirements
------------
Windows 10/11 64-bit. You need your own copy of the game (Formula One 06 for PS2, PAL, SCES-53950, as an .iso)
and about 4.3 GB free for its files. A graphics card with Vulkan for the normal mode; without it the game draws in
Software mode (slower).

Processor: single-core speed matters most. If it cannot keep up, the game does not skip frames: it runs in slow
motion. What we know so far:
- Recommended (reported by players): a desktop processor like a Core i5-6600K or a Ryzen 5 3400G, or newer.
- Minimum: not established. Older workstation CPUs and low-power laptop CPUs run races below 50 frames per second.
- Measured on the developer's PC (Core i7-13700): 50 on its performance cores; limited to its 8 efficiency cores,
  as a stand-in for a slower CPU, 25-33 in a race on 1.1.2 and 41-43 on 1.1.3 (+22-25 %).
- Measured from a player's log: Xeon E5-2687W (2012, 3.0 GHz), about 20 in a race on 1.1.2. The 1.1.3 gains are
  measured on the efficiency cores above, not on that CPU yet.
- Reported by players (RecompLauncher, "runs well", no figures): Core i5-6600K + GTX 1080, Ryzen 5 3400G + RX
  6600, Ryzen 5 5600GT with its integrated graphics, Ryzen 5 5500, Ryzen 7 5700X, Core i5-14600K, Xeon E5-2667 v4,
  among others. With problems: a laptop with a Ryzen 5 3500U (Vega 8, 6 GB of RAM).
Graphics card: any with Vulkan; with the default values (supersampling x2, double resolution off) even the
integrated graphics of a Ryzen 5 5600GT runs well, according to its player.

Performance: the game runs at 50 frames per second, like the original. The start with the 22 cars is the most
demanding moment: with the Balanced power plan of Windows and other programs open, the processor may lower its
frequency and the start drop to about 42-45 frames per second for a few seconds. To always run at 50, use the
High performance plan (Settings > System > Power) or close what you do not use while playing.
- Keep the graphics renderer on GPU (the default); the Software (CPU) mode is much slower.
- Supersampling and double resolution only load the graphics card: raise them if it has room, lower them if it is
  very busy. With a processor that is just enough they do not help.
- Capture or streaming tools (Sunshine, OBS...): on 1.1.2, with Sunshine open on a player's PC, the menus stuttered
  because the game waited for the graphics card on every picture. Since 1.1.3 it no longer waits when the card
  falls behind: it skips that copy. If you still see stutter with one of them open, you can try this (not a
  confirmed fix): NVIDIA Control Panel > Manage 3D settings > Program Settings > F1-06.exe > Vulkan/OpenGL present
  method: Prefer native. Or close the tool while you play on the PC.
$(if ($conDatos) { "
WARNING: the data\ folder contains the files of your disc. It is for personal use only: do not share it." } else { "" })
"@
Set-Content -Path (Join-Path $carpeta 'README.txt') -Value $readme -Encoding UTF8
$tamExe = (Get-Item $exeDestino).Length
Write-Host ("Listo: {0} ({1:N0} bytes), portable.txt, recomplauncher.json, README.txt, LEEME.txt, {2} y mods\ en {3}" -f $Nombre, $tamExe,
    $cmdInvitado, $carpeta)
if ($conDatos) {
    $tamDatos = (Get-ChildItem $carpetaDatos -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host ("data\: {0:N0} bytes" -f $tamDatos)
} else {
    Write-Host "Sin data\: el juego pide la ISO del jugador la primera vez (ver LEEME.txt)"
}

# Lo que va en el paquete: lo que pone este script, nunca lo que crea el juego al abrirse (ni un datos\ viejo).
$archivosPaquete = @(Get-ChildItem $carpeta -File |
    Where-Object { $_.Extension -in '.exe', '.dll' -or
        $_.Name -in 'README.txt', 'LEEME.txt', 'portable.txt', 'recomplauncher.json', $cmdInvitado })
if (Test-Path $carpetaLicencias) { $archivosPaquete += @(Get-ChildItem $carpetaLicencias -Recurse -File) }
if ($conDatos) { $archivosPaquete += @(Get-ChildItem $carpetaDatos -Recurse -File) }

if ($Zip) {
    if (-not $conDatos -and -not $SinDatos) { throw '-Zip necesita data\ (o -SinDatos para la version publica)' }
    $rutaZip = "$carpeta.zip"
    if (Test-Path $rutaZip) { Remove-Item $rutaZip -Force }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $segundos = Medir {
        $zipArchivo = [System.IO.Compression.ZipFile]::Open($rutaZip, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($a in $archivosPaquete) {
                $entrada = 'F1-06/' + $a.FullName.Substring($carpeta.Length + 1).Replace('\', '/')
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zipArchivo, $a.FullName, $entrada,
                    [System.IO.Compression.CompressionLevel]::Optimal)
            }
            [void]$zipArchivo.CreateEntry('F1-06/mods/')
        } finally { $zipArchivo.Dispose() }
    }
    Write-Host ("Zip: {0} ({1:N0} bytes, {2} archivos) en {3:N0} s" -f $rutaZip, (Get-Item $rutaZip).Length,
        $archivosPaquete.Count, $segundos)
}

if ($Instalador) {
    if (-not $conDatos -and -not $SinDatos) { throw '-Instalador necesita data\ (o -SinDatos para la version publica)' }
    $iscc = Join-Path $raiz 'tools\innosetup\ISCC.exe'
    if (-not (Test-Path $iscc)) { throw "Falta $iscc (Inno Setup portable)" }
    $salida = Join-Path $raiz $(if ($SinDatos) { 'dist\instalador-publico' } else { 'dist\instalador' })
    if (Test-Path $salida) {
        Get-ChildItem $salida -File | Where-Object { $_.Name -like 'F1-06-Setup*.exe' -or $_.Name -like 'F1-06-Setup-*.bin' } |
            Remove-Item -Force
    } else {
        New-Item -ItemType Directory -Path $salida | Out-Null
    }
    # Imagenes del asistente (la portada retocada como caja de PC): de scripts\hacer-imagen-instalador.ps1, que se
    # lanza si faltan (descarga la portada). Sin ellas, el instalador sale con las de Inno Setup.
    $carpetaImagen = Join-Path $raiz 'build\instalador-imagen'
    if (-not (Test-Path (Join-Path $carpetaImagen 'asistente-164x314.bmp'))) {
        try { & (Join-Path $PSScriptRoot 'hacer-imagen-instalador.ps1') | Out-Null }
        catch { Write-Host "Sin imagenes del asistente: $($_.Exception.Message)" }
    }
    $argumentosImagen = @()
    $grandes = @(Get-ChildItem $carpetaImagen -Filter 'asistente-*x*.bmp' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notlike 'asistente-pequena-*' } | Sort-Object Length | ForEach-Object FullName)
    $pequenas = @(Get-ChildItem $carpetaImagen -Filter 'asistente-pequena-*.bmp' -ErrorAction SilentlyContinue |
        Sort-Object Length | ForEach-Object FullName)
    if ($grandes.Count) { $argumentosImagen += "/DImagenAsistente=$($grandes -join ',')" }
    if ($pequenas.Count) { $argumentosImagen += "/DImagenPequena=$($pequenas -join ',')" }
    # Lanzamiento T1: sin data\, el instalador lleva la pagina de la ISO y no la carpeta data.
    if ($SinDatos) { $argumentosImagen += '/DSinDatos' }
    $segundos = Medir {
        & $iscc /Q "/DOrigen=$carpeta" "/DNombreExe=$Nombre" "/DVersion=$version" "/DFecha=$fecha" "/DIcono=$icono" @argumentosImagen "/O$salida" `
            (Join-Path $raiz 'scripts\instalador\F1-06.iss')
        if ($LASTEXITCODE -ne 0) { throw "ISCC.exe fallo (codigo $LASTEXITCODE)" }
    }
    Write-Host ("Instalador en {0:N0} s:" -f $segundos)
    Get-ChildItem $salida -File | Where-Object { $_.Name -like 'F1-06-Setup*' } |
        ForEach-Object { Write-Host ("  {0} ({1:N0} bytes)" -f $_.Name, $_.Length) }
}
