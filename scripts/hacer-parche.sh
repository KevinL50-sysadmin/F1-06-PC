#!/usr/bin/env bash
# Genera un parche nuevo para parches/ con los cambios actuales de unos archivos de third_party/PS2Recomp, medidos
# contra el arbol con todos los parches anteriores aplicados (asi no se mezclan bloques de otros parches que tocan
# los mismos archivos). Al final comprueba que base + todos los parches = arbol real.
#
# Uso (desde la raiz del proyecto, en Git Bash):
#   bash scripts/hacer-parche.sh 0016-nombre-del-parche ruta/relativa/a/PS2Recomp/archivo1 [archivo2 ...]
# Las rutas son relativas a third_party/PS2Recomp (p. ej. ps2xRuntime/src/lib/ps2_memory.cpp).
set -euo pipefail

nombre="$1"
shift
raiz="$(cd "$(dirname "$0")/.." && pwd)"
repo="$raiz/third_party/PS2Recomp"
parches="$raiz/parches"
destino="$parches/$nombre.patch"
temporal="$(mktemp -d)/ps2r-base"

if [ -e "$destino" ]; then
    echo "Ya existe $destino" >&2
    exit 1
fi

git -C "$repo" worktree add --detach "$temporal" HEAD >/dev/null 2>&1
limpiar() { git -C "$repo" worktree remove --force "$temporal" >/dev/null 2>&1 || true; git -C "$repo" worktree prune; }
trap limpiar EXIT

# Parches anteriores, en orden.
for p in "$parches"/[0-9][0-9][0-9][0-9]-*.patch; do
    git -C "$temporal" apply --whitespace=nowarn "$p" || { echo "No se aplica $p" >&2; exit 1; }
done
git -C "$temporal" add -A >/dev/null 2>&1

for f in "$@"; do
    if [ ! -e "$repo/$f" ]; then
        rm -f "$temporal/$f" # borrado en el arbol real: el parche lo borra
        continue
    fi
    mkdir -p "$(dirname "$temporal/$f")"
    cp "$repo/$f" "$temporal/$f"
    git -C "$temporal" add -N -- "$f" >/dev/null 2>&1 # los archivos nuevos tambien salen en el diff
done
# Arbol de trabajo (archivos actuales) frente al indice (base + parches anteriores).
git -C "$temporal" diff > "$destino"
echo "Parche: $destino ($(wc -l < "$destino") lineas)"
git -C "$temporal" add -A >/dev/null 2>&1

# Comprobacion: base + todos los parches debe ser igual al arbol real (sin el codigo generado).
a="$(git -C "$temporal" diff --cached HEAD 2>/dev/null | grep -v '^index ' | md5sum)"
b="$(git -C "$repo" diff HEAD -- . ':!ps2xRuntime/src/runner' 2>/dev/null | grep -v '^index ' | md5sum)"
if [ "$a" = "$b" ]; then
    echo "Comprobado: base + parches = arbol real"
else
    echo "AVISO: base + parches no coincide con el arbol real (falta algun archivo en el parche?)" >&2
    exit 2
fi
