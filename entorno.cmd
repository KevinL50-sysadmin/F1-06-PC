@echo off
rem Prepara ESTA consola para compilar con las herramientas portables de tools\
rem (MSVC, Windows SDK, CMake y Ninja). No instala nada ni cambia Windows:
rem al cerrar la ventana todo vuelve a como estaba.
rem Uso desde cmd:     call entorno.cmd
rem Uso desde scripts: call "%~dp0entorno.cmd" || exit /b 1

set "F1_RAIZ=%~dp0"
if not exist "%F1_RAIZ%tools\msvc\setup_x64.bat" (
  echo [entorno] Falta tools\msvc\setup_x64.bat: hay que descargar MSVC portable primero.
  exit /b 1
)
call "%F1_RAIZ%tools\msvc\setup_x64.bat"
set "PATH=%F1_RAIZ%tools\cmake\bin;%F1_RAIZ%tools\ninja;%PATH%"
echo [entorno] Listo: MSVC %VCToolsVersion%, Windows SDK %WindowsSDKVersion:\=%, CMake y Ninja de tools\
