@echo off
rem Configura y compila PS2Recomp (third_party\PS2Recomp) en build\ps2recomp
rem con las herramientas portables de tools\.
rem La primera vez CMake descarga el codigo de las dependencias (raylib, ImGui,
rem fmt, ELFIO, toml11, libdwarf, rabbitizer, nlohmann/json).
rem Desactivados: ps2xStudio (editor grafico, arrastra SDL) y el FFmpeg
rem precompilado de terceros (solo hace falta para los videos del juego).
setlocal
call "%~dp0entorno.cmd" || exit /b 1

set "SRC=%~dp0third_party\PS2Recomp"
set "BUILD=%~dp0build\ps2recomp"

cmake -S "%SRC%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
  -DPS2X_BUILD_STUDIO=OFF ^
  -DPS2X_ENABLE_FFMPEG=OFF ^
  -DPS2X_ENABLE_SCCACHE=OFF || exit /b 1

cmake --build "%BUILD%" || exit /b 1
echo.
echo [ps2recomp] Compilado en build\ps2recomp
