@echo off
rem Compila el .exe del juego (ps2EntryRunner.exe) a partir del codigo ya traducido.
rem Usalo tras cambiar algo en src\; recompilar.cmd lo llama despues de traducir el ELF.
rem Compilacion de desarrollo: optimiza cada bloque (/O2) pero sin LTCG, para que el enlazado
rem sea rapido y solo se recompile lo que cambie. Salida: build\runner-dev\ps2xRuntime\
rem Procesos en paralelo: variable F1_JOBS (por defecto 4; cada uno puede ocupar 1-2 GB de RAM).
rem Traza de llamadas (ps2_log.txt, cada funcion escribe al entrar y salir): F1_TRAZA=ON. Por defecto OFF, porque
rem frena mucho el hilo del EE. Cambiarla obliga a recompilar las 14.000 funciones (unos 40 minutos).
setlocal
cd /d "%~dp0"
call "%~dp0entorno.cmd" || exit /b 1
if "%F1_JOBS%"=="" set F1_JOBS=4
if "%F1_TRAZA%"=="" set F1_TRAZA=OFF
rem F1_LTCG=ON: la libreria del runtime con /GL y el enlace con /LTCG (parche 0079; el codigo traducido no cambia).
if "%F1_LTCG%"=="" set F1_LTCG=OFF
rem F1_FFMPEG=ON (por defecto): videos .pss con el HLE de sceMpeg de PS2Recomp y FFmpeg. La primera vez CMake descarga
rem ffmpeg-n7.1-241205-lgpl-amd64-shared.zip (35 MB, github.com/System233/ffmpeg-msvc-prebuilt) en
rem buildunner-dev\ThirdParty y copia sus DLL (LGPL) junto al .exe.
if "%F1_FFMPEG%"=="" set F1_FFMPEG=ON
set "RUNTIME=third_party\PS2Recomp\ps2xRuntime"
set "DEPS=%~dp0build\ps2recomp\_deps"
if not exist "%RUNTIME%\include\ps2_recompiled_functions.h" goto falta_codigo

echo [compilar-runner] Copiando el codigo propio de F1 06...
rem Solo se copian los archivos que han cambiado, y la copia recibe la fecha actual: copy conserva la fecha del
rem original y, si se edita mientras compila, ninja veria el objeto mas nuevo que la copia y no lo recompilaria.
for %%F in (src\*.cpp) do (
  fc /b "%%F" "%RUNTIME%\src\runner\%%~nxF" >nul 2>&1 || (
    copy /y "%%F" "%RUNTIME%\src\runner\" >nul || exit /b 1
    copy /b "%RUNTIME%\src\runner\%%~nxF"+,, "%RUNTIME%\src\runner\%%~nxF" >nul || exit /b 1
  )
)

echo [compilar-runner] Configurando build\runner-dev...
cmake -S third_party\PS2Recomp -B build\runner-dev -G Ninja ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/O2 /Ob1 /DNDEBUG" ^
  "-DCMAKE_C_FLAGS_RELWITHDEBINFO=/O2 /Ob1 /DNDEBUG" ^
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
  -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF ^
  -DPS2X_ENABLE_FFMPEG=%F1_FFMPEG% -DPS2X_ENABLE_SCCACHE=OFF -DPS2X_ENABLE_AGRESSIVE_LOGS=%F1_TRAZA% -DPS2X_LTCG_RUNTIME=%F1_LTCG% ^
  "-DFETCHCONTENT_SOURCE_DIR_RAYLIB=%DEPS%\raylib-src" ^
  "-DFETCHCONTENT_SOURCE_DIR_IMGUI=%DEPS%\imgui-src" ^
  "-DFETCHCONTENT_SOURCE_DIR_RLIMGUI=%DEPS%\rlimgui-src" >nul || exit /b 1

echo [compilar-runner] Compilando ps2EntryRunner.exe con %F1_JOBS% procesos...
cmake --build build\runner-dev --target ps2EntryRunner -j %F1_JOBS% || exit /b 1
rem GS por GPU (gs = gpu): el puente pgs_puente.dll junto al .exe, si esta compilado (scripts\obtener-parallel-gs.ps1).
if exist "build\pgs-puente\pgs_puente.dll" copy /y "build\pgs-puente\pgs_puente.dll" "build\runner-dev\ps2xRuntime\" >nul
echo [compilar-runner] Listo: build\runner-dev\ps2xRuntime\ps2EntryRunner.exe
exit /b 0

:falta_codigo
echo [compilar-runner] No hay codigo traducido: ejecuta antes recompilar.cmd
exit /b 1
