# Empaqueta F1 06 recompilado como un juego de PC, portable o con instalador (las dos cosas con los datos dentro).
# Portable: una carpeta (por defecto dist\F1-06-portable) con el .exe, sus DLL, portable.txt, LEEME.txt,
# "Jugar en red - Invitado.cmd" (abre el .exe con --invitado: el formulario del juego en red), datos\ y mods\ vacia. portable.txt hace que el .exe guarde config\, partidas\, registros\ y cache\ en su carpeta (los
# crea al abrirse); sin el (instalado) van a Documentos\My Games\F1-06 y AppData\Local\F1-06\registros y cache.
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
# Datos: -Iso extrae la ISO en datos\ con scripts\iso_ps2.py (la ISO solo se lee); -Datos copia una carpeta ya
# extraida (p. ej. juego) sin mc0, mc1 ni *.log. Sin ninguno: se deja el datos\ que ya haya y, si no hay, se copia
# dist\F1-06\datos (solo se lee). datos\ no se vuelve a copiar si ya existe (salvo con -Rehacer).
# Lanzamiento T1: -SinDatos hace la version publica, sin nada del juego (ni datos\ ni la ISO): por defecto en
# dist\F1-06-publico, el zip en dist\F1-06-publico.zip y el instalador en dist\instalador-publico. El .exe pide la ISO del
# jugador la primera vez (o el instalador, en su pagina "Elige tu copia del juego (ISO)", con F1-06.exe --extraer-iso)
# y la copia en datos\. Un datos\ que ya haya en esa carpeta (p. ej. de probarlo) se deja, pero no entra en el paquete.
# Solo se borra lo que crea este script (.exe, .dll, LEEME.txt, portable.txt, el .cmd, el .zip y el instalador); config\,
# partidas\, registros\, cache\ y lo que haya en mods\ nunca se tocan.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\empaquetar.ps1 [-Iso 'ruta\juego.iso' |
#        -Datos 'juego' | -SinDatos] [-Rehacer] [-Zip] [-Instalador] [-Destino 'dist\F1-06-portable']
#        [-Exe 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe'] [-Nombre 'F1-06.exe']
param(
    [string]$Iso = '',
    [string]$Datos = '',
    [switch]$SinDatos,
    [switch]$Rehacer,
    [switch]$Zip,
    [switch]$Instalador,
    [string]$Destino = 'dist\F1-06-portable',
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe',
    [string]$Nombre = 'F1-06.exe'
)
$ErrorActionPreference = 'Stop'
# Version del paquete: el unico sitio donde se cambia (LEEME.txt e instalador).
$version = '1.1.0'
$fecha = Get-Date -Format 'yyyy-MM-dd'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
if ($Iso -and $Datos) { throw 'Usa -Iso o -Datos, no los dos' }
if ($SinDatos -and ($Iso -or $Datos)) { throw '-SinDatos no lleva -Iso ni -Datos' }
if ($SinDatos -and -not $PSBoundParameters.ContainsKey('Destino')) { $Destino = 'dist\F1-06-publico' }
$exeOrigen = [System.IO.Path]::GetFullPath((Join-Path $raiz $Exe))
# Fase 5 (T15, N4): el acceso del invitado del juego en red (el instalador pone uno en el menu Inicio).
$cmdInvitado = 'Jugar en red - Invitado.cmd'
$carpeta = [System.IO.Path]::GetFullPath((Join-Path $raiz $Destino)).TrimEnd('\')
$carpetaDatos = Join-Path $carpeta 'datos'
$datosPorDefecto = Join-Path $raiz 'dist\F1-06\datos'
if (-not (Test-Path $exeOrigen)) { throw "Falta $exeOrigen (compila antes con compilar-runner.cmd)" }
function Medir([scriptblock]$bloque) { $t = [Diagnostics.Stopwatch]::StartNew(); & $bloque; $t.Elapsed.TotalSeconds }

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
        Where-Object { $_.Extension -in '.exe', '.dll' -or $_.Name -in 'LEEME.txt', 'portable.txt', $cmdInvitado } |
        Remove-Item -Force
} else {
    New-Item -ItemType Directory -Path $carpeta | Out-Null
}
Get-ChildItem (Split-Path -Parent $exeOrigen) -Filter '*.dll' | Copy-Item -Destination $carpeta
$exeDestino = Join-Path $carpeta $Nombre
Copy-Item $exeOrigen $exeDestino
Set-Content -Path (Join-Path $carpeta 'portable.txt') -Encoding UTF8 -Value (('Con este archivo junto a {0}, el juego ' +
    'guarda opciones, partidas, registros y mods en esta carpeta; borralo para usar Documentos\My Games\F1-06.') -f $Nombre)
# ASCII sin BOM: cmd.exe no entiende el BOM de UTF-8.
Set-Content -Path (Join-Path $carpeta $cmdInvitado) -Encoding Ascii -Value @(
    '@echo off',
    'rem Juego en red: abre la ventana del invitado (jugador 2) para conectarse al PC del anfitrion (ver LEEME.txt).',
    ('start "" "%~dp0{0}" --invitado' -f $Nombre))
$carpetaMods = Join-Path $carpeta 'mods'
if (-not (Test-Path $carpetaMods)) { New-Item -ItemType Directory -Path $carpetaMods | Out-Null }

# Licencias: pgs_puente.dll lleva dentro paraLLEl-GS (LGPL-3.0+), asi que el paquete incluye su licencia, la de lo que
# usa (Granite, volk y Vulkan-Headers, MIT/Apache) y el codigo fuente de paraLLEl-GS, de Granite y del puente, para
# que cualquiera pueda recompilar y sustituir la DLL. Se rehace siempre (es pequeno) y solo dentro de licencias\.
$clonPgs = Join-Path $raiz 'third_party\parallel-gs'
$carpetaLicencias = Join-Path $carpeta 'licencias'
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
    Set-Content -Path (Join-Path $carpetaLicencias 'LICENCIAS.txt') -Encoding UTF8 -Value @"
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
        Write-Host "datos\ ya existe: no se vuelve a copiar (usa -Rehacer para rehacerlo)"
    } else {
        if ($origenDatos -and $origenDatos.TrimEnd('\') -eq $carpetaDatos) { throw "No se puede copiar datos\ sobre si mismo" }
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
        if (-not (Test-Path (Join-Path $carpetaDatos 'SYSTEM.CNF'))) { throw "datos\ no tiene SYSTEM.CNF" }
        Write-Host ("datos\ {0} en {1:N0} s" -f $(if ($Iso) { "extraido de $Iso" } else { "copiado de $origenDatos" }), $segundos)
    }
}

$conDatos = (-not $SinDatos) -and (Test-Path (Join-Path $carpetaDatos 'SYSTEM.CNF'))
# Lanzamiento T4: el instalador publico es otra aplicacion (F1-06.iss): sus accesos se llaman "F1 06 PC".
$accesoJuego = if ($SinDatos) { 'F1 06 PC' } else { 'F1 06' }
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
$leeme = @"
F1 06 (PS2) recompilado para Windows
====================================
Version $version ($fecha)

Como jugar
----------
$(if ($conDatos) { "Los datos del juego ya estan en la carpeta datos\: no hace falta nada mas." } else { "Este paquete no trae nada del juego: hace falta tu copia de Formula One 06 de PS2, version europea PAL
(SCES-53950), como imagen .iso (unos 4,2 GB; tambien vale una carpeta con el disco ya extraido). Solo se
pide una vez:
- Instalador: en la pagina `"Elige tu copia del juego (ISO)`" busca tu .iso (si esta en Descargas o en el
  Escritorio ya sale puesta). Al instalar se comprueba y se copian sus archivos en datos\, con una barra de
  progreso. Si dejas la pagina en blanco, el juego la pide al abrirlo.
- Portable, o si no la elegiste al instalar: la primera vez que abres $Nombre, el juego busca la ISO en
  Descargas y en el Escritorio y te la ofrece; si no la encuentra, eligela en la ventana de Windows (o el
  SYSTEM.CNF de la carpeta del disco extraido). Comprueba que es Formula One 06 PAL, copia sus archivos en la
  carpeta datos\ (unos minutos, con una barra de progreso que se puede cancelar) y arranca el juego. Las
  siguientes veces arranca directamente.
- La ISO solo se lee, nunca se cambia: despues puedes guardarla donde quieras. Hacen falta unos 4,3 GB libres.
- Si la copia se corta o la cancelas, no queda nada a medias y el juego la vuelve a pedir. Una carpeta datos\
  incompleta no se borra: se aparta como datos.anterior\ (puedes borrarla tu).
- Instalado en Archivos de programa sin elegir la ISO al instalar: la carpeta del juego no se puede escribir
  sin permisos de administrador, asi que datos\ va a %LOCALAPPDATA%\F1-06\datos.
- Por linea de ordenes: $Nombre --extraer-iso `"ruta\juego.iso`" copia el disco en datos\ sin abrir el juego
  (codigos de salida: 0 hecho o ya estaba, 1 uso incorrecto, 2 no es Formula One 06 PAL, 3 error de lectura,
  escritura o espacio, 4 cancelado; con --ventana muestra la barra y los errores en ventanas) y
  $Nombre --comprobar-iso `"ruta\juego.iso`" solo lo comprueba." })
- Portable (.zip): descomprime la carpeta donde quieras y abre $Nombre. Todo lo tuyo se guarda dentro de esa
  carpeta, asi que puedes moverla o llevarla en un USB.
- Instalado: abre el acceso directo "$accesoJuego" del menu Inicio o del escritorio. Se desinstala desde
  Configuracion > Aplicaciones; pregunta si borrar tambien tus partidas y opciones.

La primera vez, Windows puede avisar con "$protegio" (SmartScreen) porque el .exe no esta firmado:
pulsa "$masInformacion" y luego "Ejecutar de todas formas".

La carpeta licencias\ tiene las licencias de los componentes de terceros (paraLLEl-GS, LGPL) y su codigo fuente.

Donde esta cada cosa
--------------------
Portable: todo en la carpeta del juego. Instalado: en Documentos\Mis juegos\F1-06 (en disco,
Documents\My Games\F1-06), salvo el registro y la cache, que van en %LOCALAPPDATA%\F1-06\registros y
%LOCALAPPDATA%\F1-06\cache.
Las carpetas se crean solas al abrir el juego.
- $Nombre y las .dll: el juego. No los separes.
- datos\: los archivos del disco. No se modifican nunca. $(if ($conDatos) { "" } else { "Los copia el juego de tu ISO la primera vez;
  datos\F1-06-disco.txt es la lista con la que comprueba en cada arranque que esta completa." })
- config\opciones.ini: ventana, sin bordes o pantalla completa al arrancar, tamano de la ventana, aspecto (auto,
  4:3, 16:9 o pixel), filtro de la imagen (cercano, bilineal, nitido, fsr o crt), sincronizacion vertical (vsync),
  contador de fotogramas por segundo, volumen (0 a 100), si se guarda el registro, el dibujo de los graficos
  (gs = gpu, en la tarjeta grafica con Vulkan, lo normal; gs = cpu, modo Software, mas lento), saltar los videos
  de inicio, la latencia del sonido (auto, baja o alta), sonido mono, la fuerza de la vibracion (0 a 100), el
  filtro para daltonismo (protanopia, deuteranopia o tritanopia), la tecla de las fotos, las cargas rapidas
  (cargas_rapidas = 1: las pantallas de carga acaban en cuanto el juego ha cargado; 0 = al ritmo de la
  consola), el campo de vision de las camaras de la carrera (fov = 0, el del juego; 5 a 30, ese tanto por
  ciento mas abierto), la dificultad de los rivales (dificultad_ia = 0, la del juego; 1 a 100, un punto entre
  facil, 0, media, 50, y dificil, 100) y, para el juego en red, el puerto (red_puerto), la calidad de la imagen
  (red_calidad: baja, media o alta) y si acepta direcciones publicas (red_publicas). La contrasena del juego en red
  nunca se guarda. Cada opcion viene explicada dentro; borralo para volver a los valores por defecto. Lo mas comodo es
  cambiarlas desde el propio menu del juego (OPCIONES > OPCIONES PC: $graficosPc, PANTALLA PC, AJUSTES PC y JUEGO
  EN RED PC, ver abajo), que guarda este archivo.
- config\controles.ini: los controles (mando, teclado y volante). Tambien viene explicado dentro. Para jugar dos
  a pantalla partida (SPLIT SCREEN), anade al final una seccion [jugador2] con el mando del segundo jugador
  (mando = 1, el segundo mando) o sus teclas (cruz = tecla:M, ...); sin ella el juego pide el segundo mando. Si tu
  controles.ini es de una version anterior, el ejemplo esta en uno nuevo: renombra el tuyo, abre el juego y copia
  la seccion comentada del final.
  Volante: el juego lo usa como su volante Logitech (Driving Force Pro), con su propia direccion, sus pedales, sus
  botones en los menus (cruz aceptar, circulo atras) y su fuerza de respuesta. Con el volante conectado, el jugador 1
  juega con el. Los botones y ejes se cambian en la seccion [volante] de controles.ini (sin ella, los de un Logitech
  G29). El giro es como el del Driving Force Pro original: 200 grados de tope a tope son todo el giro del juego. En
  [volante], grados_volante es el rango que tiene tu volante en G HUB o en su driver (900 por defecto; pon el mismo
  valor) y giro, los grados de tope a tope del juego (200 por defecto; menos = direccion mas rapida). Si la fuerza
  empuja al reves, abre el juego con la variable F1_VOLANTE_INVERTIR_FUERZA=1; con F1_VOLANTE_STICK=1 el volante
  vuelve a ir como el stick del mando (como en versiones anteriores).
- partidas\mc0 y partidas\mc1: las tarjetas de memoria (tus partidas). Copialas para hacer una copia de
  seguridad.
- capturas\: las fotos (F12), en PNG, con la fecha y la hora en el nombre. Se crea con la primera foto.
- registros\F1-06.log: el registro de la ultima partida (la anterior queda en F1-06.anterior.log). Si algo
  falla, es lo que hay que mirar.
- cache\vulkan-pipelines.bin: los graficos ya preparados para tu tarjeta grafica (gs = gpu), para que no haya
  tirones la segunda vez que ves cada cosa. Ocupa unos 20 a 50 MB y se puede borrar sin problema: el juego lo
  vuelve a crear (las primeras partidas despues pueden ir a tirones un momento). Tras cambiar de tarjeta grafica o
  actualizar su driver se rehace solo.
- cache\juego\: la letra y los botones de los menus del juego, copiados de tu disco al abrirlo, para que la
  ventana del invitado del juego en red tenga el mismo aspecto. Se puede borrar: el juego los vuelve a copiar.
- mods\: un archivo aqui sustituye al del disco que tenga la misma ruta dentro de datos\. Por ejemplo,
  mods\DATA2006\MOVIES\PAL\SINGLMOV.IPF sustituye a datos\DATA2006\MOVIES\PAL\SINGLMOV.IPF. Solo archivos
  sueltos del disco (no lo que va dentro de los .WAD). Para quitar un mod, borra su archivo.
- portable.txt (solo en la version portable): hace que todo se guarde en la carpeta del juego. Sin el, el
  juego usa las carpetas de la version instalada.
- Jugar en red - Invitado.cmd (solo en la version portable): abre la ventana del invitado del juego en red.
- config\red.ini: la ultima direccion y el puerto a los que se conecto este PC como invitado (nunca la contrasena).

Opciones del PC dentro del juego
--------------------------------
En OPCIONES del menu principal, debajo de OPCIONES DE PANTALLA, esta la entrada OPCIONES PC (PC OPTIONS si el
juego no esta en espanol). Lleva a cuatro paginas: $graficosPc (PC GRAPHICS), PANTALLA PC (PC DISPLAY), AJUSTES PC
(PC SETTINGS) y JUEGO EN RED PC (PC NETPLAY); cruz entra en la pagina elegida y, al salir de ella (ACEPTAR o
atras), se vuelve a esta lista. Cada pagina tiene al final una fila VALORES POR DEFECTO (izquierda o derecha) que
devuelve las de esa pagina a los valores de serie.
- ${graficosPc}: filtro de la imagen, supersampling, doble resolucion, sincronizacion vertical, contador de FPS,
  modo de pantalla y volumen general.
- PANTALLA PC: forma de la imagen (automatica, 4:3, 16:9 o pixeles cuadrados), tamano de la ventana (los
  habituales que caben en tu escritorio; cuenta en modo ventana) y dibujo de los graficos (GPU o CPU: se aplica al
  reiniciar el juego, y el valor que aun no esta en uso lo dice con AL REINICIAR).
- AJUSTES PC: saltar los videos de inicio, latencia del sonido (automatica, baja o alta), salida de sonido (estereo
  o mono), vibracion, filtro para daltonismo, tecla de foto, HUD de la carrera (completo, minimo u oculto), tecla del
  HUD, cargas rapidas (activadas de serie: la carga de una carrera dura unos 2 s menos, la vuelta al menu unos 3,5 s
  menos y el arranque hasta la presentacion unos 15 s menos; los fundidos y los consejos se ven igual), campo de
  vision (NORMAL o de +5 % a +30 %: las camaras de la carrera, como la cabina, el morro o la persecucion, ven mas a
  los lados; el HUD y los menus no cambian) y tecla del modo foto.
- JUEGO EN RED PC: $anfitrionFila (APAGADO o ENCENDIDO), puerto, calidad de la imagen (baja, media o alta) y
  $publicasFila (NO o SI); ESTADO (apagado, esperando al invitado, invitado conectado o no se pudo empezar; su
  ayuda, abajo, da el motivo del fallo o la direccion del invitado conectado) y DIRECCION (la de este PC y el
  puerto), que solo informan (izquierda o derecha los actualiza); y CONTRASE$([char]0xD1)A, que se ve en la fila: X la
  escribe con el teclado del juego (de 8 a 32 caracteres) e izquierda o derecha la deja AL AZAR (una nueva al
  encender). Con el anfitrion encendido, el puerto, la calidad y las direcciones publicas no cambian (se aplican al
  encenderlo): apagalo antes. Su VALORES POR DEFECTO devuelve esas tres. Ver "Juego en red" mas abajo.
En la pausa de la carrera, OPCIONES lleva directamente la entrada $graficosPc (debajo de OPCIONES DE PANTALLA) con las
mismas siete filas, sin VALORES POR DEFECTO; y el menu de pausa tiene, debajo de REPLAY, la entrada MODO FOTO (PHOTO
MODE; ver "Teclas", F8).
Se maneja como el resto de las opciones del juego (cruceta arriba y abajo para elegir, izquierda y derecha para
cambiar); los cambios se ven al momento, ACEPTAR (cruz) los guarda en config\opciones.ini y RESTABL. (triangulo)
los deshace.
En DIFICULTAD (DIFFICULTY, antes de cada carrera), la fila de la IA de los rivales (OPPONENT AI) va de FACIL a
DIFICIL en pasos de 10: FACIL, 10, 20, 30, 40, MEDIA, 60, 70, 80, 90 y DIFICIL. Los numeros son puntos entre los
niveles del juego: los rivales van mas rapido cuanto mas alto (en una carrera rapida en Silverstone, la media de
lo que recorren en dos minutos crece de FACIL a DIFICIL un 11 %, y cada paso queda entre los dos de al lado). El
juego guarda su nivel mas cercano (los tiempos de las sesiones que no se corren son los de ese nivel) y el numero
va a config\opciones.ini (dificultad_ia); es uno para todos los modos y solo cuenta mientras el nivel del juego
sea el mas cercano a el (si DIFICULTAD GENERAL u otro perfil cambian el nivel, manda el del juego).

Teclas
------
- F11: pantalla completa (sin bordes, o la de pantalla en opciones.ini) o ventana.
- F10: cambia el aspecto (4:3, 16:9, pixeles de la consola).
- F12: foto. Guarda la imagen del juego a su resolucion interna (sin el contador de FPS ni los menus del PC) en
  capturas\; abajo sale un aviso con el nombre. La tecla se cambia en AJUSTES PC u opciones.ini (tecla_foto).
- H: cambia el HUD de la carrera: completo, minimo (posiciones, banderas, velocidad y marcha), oculto (nada, para
  fotos sin marcadores) y otra vez completo. No se guarda solo (ACEPTAR en AJUSTES PC). La tecla
  se cambia en AJUSTES PC u opciones.ini (tecla_hud: h, f7, f6 o no).
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
  se cambia en AJUSTES PC u opciones.ini (tecla_modo_foto: f8, f5, p o no; si es la misma que la de foto, el
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
  delante o al de detras (en el teclado, con los controles de controles.ini: V, A D y W S). Al empezar la
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
  JUEGO EN RED PC (o red_publicas = 1 en opciones.ini).
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
   diga controles.ini. Cerrar el juego tambien termina la sesion.

Invitado (el otro PC):
1. Abre "Jugar en red - Invitado.cmd", en la carpeta del juego (instalado: "$accesoJuego - Jugar en red (invitado)" en
   el menu Inicio). Tambien vale abrir $Nombre --invitado.
2. Escribe la direccion IP del anfitrion (en casa, como 192.168.1.20; con Tailscale, como 100.x.y.z), el puerto
   (50650 si no lo cambio) y la contrasena, y pulsa Intro o CONECTAR. Tab o las flechas cambian de campo, Ctrl+V
   pega y F4 muestra la contrasena; con el mando, la cruceta cambia de campo y X conecta (para escribir hace falta
   el teclado). La ventana tiene el aspecto de los menus del juego si el juego se ha abierto antes en este PC (usa
   su letra y sus botones, guardados en cache\juego); si no, los mismos colores con otra letra. La direccion y el
   puerto se recuerdan para la proxima vez (config\red.ini); la
   contrasena no se guarda nunca.
3. Tu mando es el jugador 1 de tu controles.ini (teclado, mando o volante). Teclas de la ventana: F3 muestra u
   oculta los datos de la conexion (imagenes por segundo, Mbit/s, ida y vuelta, perdidas, sonido); M silencia el
   sonido; Re Pag y Av Pag (o + y - del teclado numerico) suben y bajan el volumen. Si la sesion acaba, Intro
   (o X en el mando) vuelve al formulario. Para salir, cierra la ventana.

Calidad de la imagen (OPCIONES > OPCIONES PC > JUEGO EN RED PC o red_calidad en opciones.ini; se elige con
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
Variables de entorno (mandan sobre opciones.ini): PS2X_TARJETAS=<carpeta> (tarjetas en otra carpeta),
PS2X_ISO=<ruta> (jugar desde una ISO), PS2X_MODS=<carpeta> (mods de otra carpeta).
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

Rendimiento: el juego va a 50 fotogramas por segundo, como el original. La salida con los 22 coches es el momento
mas exigente: con el plan de energia Equilibrado de Windows y otros programas abiertos el procesador puede bajar
su frecuencia y la salida caer a unos 42-45 fotogramas por segundo durante unos segundos. Para ir siempre a 50,
usa el plan Alto rendimiento (Configuracion > Sistema > Energia) o cierra lo que no uses mientras juegas.
$(if ($conDatos) { "
AVISO: la carpeta datos\ contiene los archivos de tu disco. Es solo para uso personal: no la compartas." } else { "" })
"@
Set-Content -Path (Join-Path $carpeta 'LEEME.txt') -Value $leeme -Encoding UTF8
$tamExe = (Get-Item $exeDestino).Length
Write-Host ("Listo: {0} ({1:N0} bytes), portable.txt, LEEME.txt, {2} y mods\ en {3}" -f $Nombre, $tamExe, $cmdInvitado, $carpeta)
if ($conDatos) {
    $tamDatos = (Get-ChildItem $carpetaDatos -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host ("datos\: {0:N0} bytes" -f $tamDatos)
} else {
    Write-Host "Sin datos\: el juego pide la ISO del jugador la primera vez (ver LEEME.txt)"
}

# Lo que va en el paquete: lo que pone este script, nunca lo que crea el juego al abrirse.
$archivosPaquete = @(Get-ChildItem $carpeta -File |
    Where-Object { $_.Extension -in '.exe', '.dll' -or $_.Name -in 'LEEME.txt', 'portable.txt', $cmdInvitado })
if (Test-Path $carpetaLicencias) { $archivosPaquete += @(Get-ChildItem $carpetaLicencias -Recurse -File) }
if ($conDatos) { $archivosPaquete += @(Get-ChildItem $carpetaDatos -Recurse -File) }

if ($Zip) {
    if (-not $conDatos -and -not $SinDatos) { throw '-Zip necesita datos\ (o -SinDatos para la version publica)' }
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
    if (-not $conDatos -and -not $SinDatos) { throw '-Instalador necesita datos\ (o -SinDatos para la version publica)' }
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
    # Lanzamiento T1: sin datos\, el instalador lleva la pagina de la ISO y no la carpeta datos.
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
