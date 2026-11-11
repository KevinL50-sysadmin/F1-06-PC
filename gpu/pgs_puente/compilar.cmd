@echo off
rem Compila pgs_puente.dll (puente propio sobre paraLLEl-GS) en build\pgs-puente con las herramientas portables.
rem Necesita el clon de third_party\parallel-gs (scripts\obtener-parallel-gs.ps1 lo descarga y llama a este archivo).
setlocal
call "%~dp0..\..\entorno.cmd" >nul || exit /b 1
set "RAIZ=%~dp0..\.."
set "BUILD=%RAIZ%\build\pgs-puente"

cmake -S "%~dp0." -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
  "-DPGS_SOURCE_DIR=%RAIZ%\third_party\parallel-gs" || exit /b 1

cmake --build "%BUILD%" --target pgs_puente || exit /b 1
echo [pgs-puente] Listo: build\pgs-puente\pgs_puente.dll
