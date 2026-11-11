@echo off
rem Banco de pruebas diferencial del interprete de VU1 (scripts\prueba_vu1.cpp): compila el interprete actual de
rem third_party\PS2Recomp y uno de referencia, ejecuta con los dos los volcados de logs\vu1 y compara los resultados.
rem Uso: scripts\prueba_vu1.cmd <carpeta de referencia> [repeticiones] [carpeta de volcados] [solo-nuevo]
rem      scripts\prueba_vu1.cmd <carpeta de referencia> aleatorio [semilla] [programas]
rem      scripts\prueba_vu1.cmd <carpeta de referencia> fmac [semilla] [programas]
rem   La carpeta de referencia tiene ps2_vu1_core.cpp, ps2_vu1_upper.cpp, ps2_vu1_lower.cpp y ps2_vu1_detail.h.
rem   Los volcados se sacan con F1_VU1_VOLCADO_N=<n> F1_VU1_VOLCADO_CADA=<k> F1_VU1_VOLCADO_MAX=<m> (ver ESTADO.md).
rem   "aleatorio" genera programas al azar con valores especiales y compara los dos interpretes (sin volcados).
rem   "fmac" genera MADD/MSUB/OPMSUB con ACC junto a -+a*b (cancelaciones) y guarda MAC, estado y resultado de cada una.
setlocal
cd /d "%~dp0.."
if "%~1"=="" (
  echo Uso: scripts\prueba_vu1.cmd ^<carpeta de referencia^> [repeticiones] [carpeta de volcados]
  exit /b 1
)
call "%~dp0..\entorno.cmd" >nul || exit /b 1
set "REF=%~1"
set "REPS=%~2"
if "%REPS%"=="" set "REPS=10"
set "VOLCADOS=%~3"
if "%VOLCADOS%"=="" set "VOLCADOS=logs\vu1"
set "RT=third_party\PS2Recomp\ps2xRuntime"
set "OBJ=build\prueba_vu1"
if not exist "%OBJ%\nuevo" mkdir "%OBJ%\nuevo"
if not exist "%OBJ%\viejo" mkdir "%OBJ%\viejo"
set "FLAGS=/nologo /std:c++20 /EHsc /O2 /Ob2 /Z7 /DNDEBUG /DPS2_RUNTIME_LOGS=1 /I %RT%\include"
cl %FLAGS% /c /Fo%OBJ%\nuevo\ %RT%\src\lib\vu\ps2_vu1_core.cpp %RT%\src\lib\vu\ps2_vu1_upper.cpp %RT%\src\lib\vu\ps2_vu1_lower.cpp || exit /b 1
cl %FLAGS% /DVU1Interpreter=VU1InterpreterViejo /DVU1State=VU1StateViejo /c /Fo%OBJ%\viejo\ "%REF%\ps2_vu1_core.cpp" "%REF%\ps2_vu1_upper.cpp" "%REF%\ps2_vu1_lower.cpp" || exit /b 1
cl %FLAGS% /Fo%OBJ%\ /Fe%OBJ%\prueba_vu1.exe scripts\prueba_vu1.cpp %OBJ%\nuevo\*.obj %OBJ%\viejo\*.obj /link /DEBUG || exit /b 1
if /i "%~2"=="fmac" (
  "%OBJ%\prueba_vu1.exe" fmac %3 %4
  exit /b
)
if /i "%~2"=="aleatorio" (
  "%OBJ%\prueba_vu1.exe" aleatorio %3 %4
  exit /b
)
"%OBJ%\prueba_vu1.exe" "%VOLCADOS%" %REPS% %4
