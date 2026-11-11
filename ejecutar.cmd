@echo off
rem Ejecuta F1 06 recompilado y guarda la salida en logs\.
rem Los datos del disco se leen de juego\ (extraidos de la ISO con scripts\iso_ps2.py).
setlocal
cd /d "%~dp0"
if not exist build\runner-dev\ps2xRuntime\ps2EntryRunner.exe goto falta_exe
if not exist juego\SCES_539.50 goto falta_juego
if not exist logs mkdir logs
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%t"
echo [ejecutar] Salida en logs\ejecucion-%STAMP%.log
build\runner-dev\ps2xRuntime\ps2EntryRunner.exe juego\SCES_539.50 > "logs\ejecucion-%STAMP%.log" 2>&1
echo [ejecutar] Terminado con codigo %errorlevel%
exit /b %errorlevel%

:falta_exe
echo [ejecutar] Falta el .exe: ejecuta antes recompilar.cmd
exit /b 1

:falta_juego
echo [ejecutar] Falta juego\: extrae el disco con scripts\iso_ps2.py
exit /b 1
