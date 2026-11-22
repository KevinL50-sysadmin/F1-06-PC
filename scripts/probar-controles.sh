#!/usr/bin/env bash
# Compila y corre scripts/probar-controles.cpp contra el bloque puro de los controles personalizados de
# src/f1_overrides.cpp (entre F1CONTROLES-PURO-INICIO y -FIN). Necesita entorno.cmd (Visual Studio, cl).
# Uso (desde la raiz del proyecto, en Git Bash): bash scripts/probar-controles.sh   -> codigo 0 si todo pasa
set -euo pipefail
raiz="$(cd "$(dirname "$0")/.." && pwd)"
d="$(mktemp -d)"
trap 'rm -rf "$d"' EXIT
sed -n '/F1CONTROLES-PURO-INICIO/,/F1CONTROLES-PURO-FIN/p' "$raiz/src/f1_overrides.cpp" > "$d/controles-puro.inc"
cp "$raiz/scripts/probar-controles.cpp" "$d/"
cat > "$d/c.cmd" <<EOF
@call "$(cygpath -w "$raiz/entorno.cmd")" >nul
@cd /d "$(cygpath -w "$d")"
@cl /nologo /std:c++17 /EHsc /W4 probar-controles.cpp >"%~dp0compilar.log" || (type "%~dp0compilar.log" & exit /b 2)
@"%~dp0probar-controles.exe"
EOF
MSYS2_ARG_CONV_EXCL='*' cmd.exe /c "$(cygpath -w "$d/c.cmd")"
