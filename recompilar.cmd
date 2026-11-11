@echo off
rem Traduce el ejecutable de F1 06 (juego\SCES_539.50) a C++ con PS2Recomp y despues compila
rem el .exe con compilar-runner.cmd. La traduccion borra y regenera
rem third_party\PS2Recomp\ps2xRuntime\src\runner (codigo generado, ignorado por git).
setlocal
cd /d "%~dp0"
call "%~dp0entorno.cmd" || exit /b 1
if not exist juego\SCES_539.50 goto falta_elf
set "RUNTIME=third_party\PS2Recomp\ps2xRuntime"

echo [recompilar] Traduciendo el ejecutable a C++...
if exist "%RUNTIME%\src\runner" rmdir /s /q "%RUNTIME%\src\runner"
mkdir "%RUNTIME%\src\runner"
build\ps2recomp\ps2xRecomp\ps2_recomp.exe config\recomp.toml || exit /b 1
move /y "%RUNTIME%\src\runner\ps2_recompiled_functions.h" "%RUNTIME%\include\" >nul || exit /b 1
move /y "%RUNTIME%\src\runner\ps2_recompiled_stubs.h" "%RUNTIME%\include\" >nul || exit /b 1

call "%~dp0compilar-runner.cmd"
exit /b %errorlevel%

:falta_elf
echo [recompilar] Falta juego\SCES_539.50: extrae el disco con scripts\iso_ps2.py
exit /b 1
