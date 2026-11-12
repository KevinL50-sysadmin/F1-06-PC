#!/usr/bin/env bash
# Exporta la parte publica del proyecto a dist/F1-06-PC, la copia de trabajo del repositorio publico F1-06-PC
# (este repositorio sigue privado). Solo sale una lista blanca revisada de archivos del repositorio (nunca lo que
# .gitignore deja fuera): archivo a archivo los .cmd de la raiz, .gitignore, README.md, LICENSE, .github/FUNDING.yml,
# config/recomp.toml, gpu/pgs_puente/, scripts/ y las capturas de docs/lanzamiento/img/; por tipo los .cpp y .h de src/
# y los .patch de parches/. Se quedan fuera las notas de trabajo (odd/, ESTADO.md, ROADMAP.md, analisis/), los
# borradores del anuncio (docs/lanzamiento/*.md), scripts/pcsx2-ref.ps1 (lleva la ruta de la ISO del usuario), .git-blame-ignore-revs (nombra
# commits privados) y, por supuesto, cualquier dato del juego. Un archivo nuevo en src/, parches/, gpu/, config/,
# scripts/ o docs/ que no este en la lista ni entre los privados conocidos hace fallar la exportacion: hay que
# revisarlo y anadirlo a una de las dos listas de abajo.
#
# La exportacion se monta en una carpeta temporal y se comprueba; falla (codigo 1, sin tocar dist/F1-06-PC) si
# encuentra archivos sin revisar, rutas excluidas, datos personales (tambien en UTF-16 y en el texto de los binarios,
# como los metadatos tEXt de un PNG; el texto comprimido de un PNG, zTXt/iTXt, no se mira), archivos de mas de 5 MB o
# datos del juego. Si todo esta bien sustituye el contenido de dist/F1-06-PC entero (sin restos de archivos
# borrados o renombrados) y conserva su .git, asi cada version es un commit nuevo encima del anterior.
#
# Despues comprueba la identidad con la que se hara el commit publico y falla (codigo 2, con la exportacion ya
# copiada) si no se ha elegido a proposito o si algun commit de dist/F1-06-PC tiene otro autor. Se elige con
#   F1_PUBLICO_NOMBRE="Nombre" F1_PUBLICO_EMAIL="<id>+<usuario>@users.noreply.github.com" bash scripts/exportar-publico.sh
# (el correo noreply esta en GitHub, Settings > Emails); se guarda en dist/F1-06-PC/.git/config y en las siguientes
# exportaciones basta si es un correo noreply de GitHub. No hace commit ni push: eso se hace a mano en dist/F1-06-PC.
#
# Uso (desde la raiz del proyecto, en Git Bash):
#   bash scripts/exportar-publico.sh             # exporta lo que hay en HEAD (git archive: solo lo que tiene commit)
#   bash scripts/exportar-publico.sh --trabajo   # exporta la carpeta tal como quedaria tras el proximo commit
#                                                # (cambios sin commit y archivos nuevos no ignorados incluidos)
# Prueba del propio script: bash scripts/probar-exportar-publico.sh
set -euo pipefail

raiz="$(cd "$(dirname "$0")/.." && pwd)"
destino="$raiz/dist/F1-06-PC"

origen=HEAD
case "${1:-}" in
    "") ;;
    --trabajo) origen="carpeta de trabajo" ;;
    *) echo "Uso: bash scripts/exportar-publico.sh [--trabajo]" >&2; exit 1 ;;
esac

# Lista blanca revisada, archivo a archivo. Un archivo de la lista que no exista no es un fallo.
revisados=(
    .gitignore README.md LICENSE .github/FUNDING.yml
    compilar-ps2recomp.cmd compilar-runner.cmd ejecutar.cmd entorno.cmd recompilar.cmd
    config/recomp.toml
    gpu/pgs_puente/CMakeLists.txt gpu/pgs_puente/LICENSE.txt gpu/pgs_puente/compilar.cmd
    gpu/pgs_puente/compose_circuits.comp gpu/pgs_puente/pgs_puente.cpp gpu/pgs_puente/pgs_puente.h
    docs/lanzamiento/img/cockpit-hud.png docs/lanzamiento/img/cockpit-start-lights.png
    docs/lanzamiento/img/grid-22-cars-aerial.png docs/lanzamiento/img/menu-pc-graphics.png
    docs/lanzamiento/img/replay-tv-camera.png docs/lanzamiento/img/cabecera-readme.png
    scripts/ab_ciclos.py scripts/ab_diagnostico.py scripts/ab_resumen.py scripts/bft_decodificar.py
    scripts/buscar_valor.py scripts/capturar-ventana.ps1 scripts/carrera-directa.ps1 scripts/cazar-cuelgue.ps1
    scripts/comparar_partidas.py scripts/desfase-lineas.ps1 scripts/ee_xref.py scripts/elf_ps2.py
    scripts/empaquetar.ps1 scripts/entradas_datos.py scripts/explorar.ps1 scripts/exportar-publico.sh
    scripts/filtrar_diff.py scripts/funciones_sin_optimizar.py scripts/gif_decodificar.py scripts/hacer-icono.ps1
    scripts/hacer-imagen-instalador.ps1 scripts/hacer-parche.sh scripts/instalador/F1-06.iss scripts/irx_importa.py
    scripts/iso_ps2.py scripts/jit_desensamblar.py scripts/jit_mapa.py scripts/leer-consola.ps1 scripts/lua50_ram.py
    scripts/lua_volcados.py scripts/medir-carrera.ps1 scripts/medir-menus.ps1 scripts/mips_dis.py scripts/mosaico.ps1
    scripts/obtener-parallel-gs.ps1 scripts/pct_decodificar.py scripts/perfil.cpp scripts/pgs-reproducir.ps1
    scripts/pila.cpp scripts/probar-circuitos.ps1 scripts/probar-modo.ps1
    scripts/probar-red.ps1 scripts/prueba_ipu.cpp scripts/prueba_tablas_gs.cpp scripts/prueba_vu1.cmd
    scripts/prueba_vu1.cpp scripts/recortar.ps1 scripts/tanda-tarjeta.ps1 scripts/teclas.ps1 scripts/vu_dis.py
    scripts/wav_chasquidos.py scripts/wav_comparar.py scripts/wav_niveles.py scripts/wav_recortar.py
    scripts/wav_vs_pss.py
)
declare -A es_revisado
for f in "${revisados[@]}"; do es_revisado[$f]=1; done

# Deja en $clase si un archivo del repositorio es publico, privado, sin revisar (en una carpeta publicada pero en
# ninguna lista) o queda fuera sin mas (el resto del repositorio).
clasificar() {
    if [ -n "${es_revisado[$1]:-}" ]; then clase=publico; return; fi
    case "$1" in
        src/*.cpp | src/*.h | parches/*.patch) clase=publico ;;
        # probar-exportar-publico.sh monta por piezas el nombre y las rutas reales del usuario para probar la busqueda: privado.
        scripts/pcsx2-ref.ps1 | scripts/probar-exportar-publico.sh | docs/lanzamiento/*.md | docs/lanzamiento/img/redes/* | docs/lanzamiento/img/*-espejo.png) clase=privado ;;
        src/* | parches/* | gpu/* | config/* | scripts/* | docs/*) clase=sin-revisar ;;
        # Un archivo nuevo en la raiz (p. ej. otro .cmd) hay que revisarlo, no dejarlo fuera sin avisar.
        */*) clase=fuera ;;
        *.md | ESTADO* | ROADMAP* | .git-blame-ignore-revs) clase=fuera ;;
        *) clase=sin-revisar ;;
    esac
}

case "$destino" in
    "$raiz"/dist/F1-06-PC) ;;
    *) echo "Destino inesperado: $destino" >&2; exit 1 ;;
esac
mkdir -p "$raiz/dist"
temporal="$(mktemp -d "$raiz/dist/.exportando-XXXXXX")"
trap 'rm -rf "$temporal"' EXIT
arbol="$temporal/arbol"
mkdir "$arbol"

fallos=0
fallo() { echo "FALLO: $*" >&2; fallos=$((fallos + 1)); }

# Listas de git separadas por NUL: los nombres con espacios o no ASCII no salen entre comillas.
git -C "$raiz" ls-tree -r -z --name-only HEAD > "$temporal/en-head"
declare -A en_head cambiado
while IFS= read -r -d '' f; do en_head[$f]=1; done < "$temporal/en-head"
if [ "$origen" = HEAD ]; then
    cp "$temporal/en-head" "$temporal/archivos"
else
    # Lo que quedaria tras el proximo commit: el indice y los nuevos sin ignorar, menos lo borrado en la carpeta. Los
    # que no cambian salen de HEAD tal cual (algunas copias de trabajo tienen CRLF y el commit no).
    git -C "$raiz" ls-files -z --cached --others --exclude-standard > "$temporal/archivos"
    git -C "$raiz" diff -z --name-only --no-renames HEAD > "$temporal/cambiados"
    while IFS= read -r -d '' f; do cambiado[$f]=1; done < "$temporal/cambiados"
fi

de_head=()
de_carpeta=()
sin_revisar=()
while IFS= read -r -d '' f; do
    if [ "$origen" != HEAD ] && [ ! -f "$raiz/$f" ]; then continue; fi
    clasificar "$f"
    case "$clase" in
        publico)
            if [ "$origen" = HEAD ] || { [ -n "${en_head[$f]:-}" ] && [ -z "${cambiado[$f]:-}" ]; }; then
                de_head+=("$f")
            else
                de_carpeta+=("$f")
            fi ;;
        sin-revisar) sin_revisar+=("$f") ;;
    esac
done < "$temporal/archivos"

if [ "${#de_head[@]}" -gt 0 ]; then
    # Por tandas (xargs) para no pasar del limite de la linea de ordenes de Windows.
    printf '%s\0' "${de_head[@]}" | xargs -0 bash -c 'set -o pipefail; arbol=$1; shift
        git -C "$0" --literal-pathspecs archive --format=tar HEAD -- "$@" | tar -x -C "$arbol"' "$raiz" "$arbol" \
        || { echo "No se pudo extraer HEAD" >&2; exit 1; }
fi
for f in ${de_carpeta[@]+"${de_carpeta[@]}"}; do
    case "$f" in */*) mkdir -p "$arbol/${f%/*}" ;; esac
    cp -- "$raiz/$f" "$arbol/$f"
done
for f in ${sin_revisar[@]+"${sin_revisar[@]}"}; do
    fallo "archivo sin revisar (anadelo a la lista blanca o a los privados de scripts/exportar-publico.sh): $f"
done

# Comprobaciones, sobre la carpeta temporal.
cd "$arbol"

excluidas=(odd ESTADO.md ROADMAP.md analisis .git-blame-ignore-revs config/recomp.toml.antes-entradas logs
           scripts/pcsx2-ref.ps1 juego mc0 mc1 datos build tools third_party)
for ruta in "${excluidas[@]}"; do
    [ ! -e "$ruta" ] || fallo "ruta excluida presente: $ruta"
done
for borrador in docs/lanzamiento/*.md; do
    [ ! -e "$borrador" ] || fallo "borrador del anuncio presente: $borrador"
done

# Datos personales, sin distinguir mayusculas: rutas de la unidad J: y de la carpeta del usuario de Windows, su nombre,
# su correo y las carpetas del proyecto y de la ISO. Se permiten el enlace al repositorio publico
# (github.com/<cuenta>/F1-06-PC), los perfiles publicos de Ko-fi y X del autor (tambien el ko_fi de .github/FUNDING.yml)
# y las carpetas de usuario genericas (Users/Public y Users/p de los tests).
# Los corchetes evitan que este mismo script coincida con sus patrones.
personales='J:[\\/]|[a-z]:[\\/]+users[\\/]+[a-z0-9._-]{2,}|K[e]vin|@[g]mail|IAs[\\/]+Claude|(Descargas|Downloads)[\\/]+Formula'
permitidos='s#github\.com/K[e]vinL50-sysadmin/##gI; s#ko-fi\.com/k[e]vinl50##gI; s#ko_fi: k[e]vinl50##gI; s#(x|twitter)\.com/K[e]vinLA50##gI; s#@K[e]vinLA50##gI; s#users[\\/]+(public|default)\b##gI'
# En los binarios solo los patrones largos (los cortos darian falsos positivos en datos comprimidos).
personales_binario='K[e]vin|@[g]mail|IAs[\\/]+Claude|(Descargas|Downloads)[\\/]+Formula|[a-z]:[\\/]+users[\\/]+k[e]vin'
buscar_personales() { sed -E "$permitidos" | grep -i -E "$personales"; }

coincidencias="$(grep -r -I -n -i -E "$personales" . | buscar_personales || true)"
if [ -n "$coincidencias" ]; then
    fallo "datos personales en archivos de texto:"
    echo "$coincidencias" >&2
fi
# Los que grep toma por binarios: texto en UTF-16 (con o sin BOM, .ps1/.iss/.rc guardados asi) o binarios de verdad.
while IFS= read -r -d '' archivo; do
    [ -s "$archivo" ] || continue
    bom="$(head -c 2 "$archivo" | od -An -tx1 | tr -d ' \n')"
    case "$bom" in
        fffe | feff) codificacion=UTF-16 ;;
        *) codificacion=UTF-16LE ;;
    esac
    if iconv -f "$codificacion" -t UTF-8 "$archivo" > "$temporal/texto" 2>/dev/null && grep -I -q . "$temporal/texto"; then
        coincidencias="$(grep -n -i -E "$personales" "$temporal/texto" | buscar_personales || true)"
        if [ -n "$coincidencias" ]; then
            fallo "datos personales en texto UTF-16: $archivo"
            echo "$coincidencias" >&2
        fi
    elif grep -a -q -i -E "$personales_binario" "$archivo" \
         || tr -d '\000' < "$archivo" | grep -a -q -i -E "$personales_binario"; then
        fallo "datos personales en binario: $archivo"
    fi
done < <(grep -r -I -L -Z . . || true)

grandes="$(find . -type f -size +5242880c -print)"
[ -z "$grandes" ] || fallo "archivos de mas de 5 MB:"$'\n'"$grandes"

juego="$(find . \( -iname '*.iso' -o -iname '*.wad' -o -iname '*.elf' -o -iname '*.irx' -o -iname '*.pss' \
    -o -iname '*.bin' -o -iname 'SCES_*' -o -iname 'prod.keys' -o -iname 'mc0' -o -iname 'mc1' -o -iname 'datos' \) \
    -print)"
[ -z "$juego" ] || fallo "datos del juego:"$'\n'"$juego"

privados='ESTADO\.md|ROADMAP\.md|odd[/\\]|analisis[/\\]'
if [ -f README.md ] && grep -n -E "$privados" README.md >&2; then fallo "README.md nombra archivos privados"; fi
menciones="$(grep -r -I -l -E "$privados" . | grep -v '^\./README\.md$' || true)"
if [ -n "$menciones" ]; then
    echo "Aviso: comentarios que nombran notas privadas (no se publican; no es un fallo):" >&2
    echo "$menciones" | sed 's/^/  /' >&2
fi

archivos="$(find . -type f | wc -l)"
tamano="$(du -sh . | cut -f1)"
echo "Archivos: $archivos; tamano: $tamano"
if [ "$fallos" -gt 0 ]; then
    echo "Exportacion NO valida: $fallos fallo(s). dist/F1-06-PC no se ha tocado." >&2
    exit 1
fi

# Sustituir el contenido de la exportacion anterior, menos su .git.
cd "$raiz"
mkdir -p "$destino"
find "$destino" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
find "$arbol" -mindepth 1 -maxdepth 1 -exec mv -t "$destino" {} +
echo "Exportado ($origen) a $destino"
echo "Comprobaciones superadas."

# Identidad del commit publico: nunca la del repositorio privado sin mas.
nombre="${F1_PUBLICO_NOMBRE:-}"
correo="${F1_PUBLICO_EMAIL:-}"
identidad=0
como="Elige la identidad publica: F1_PUBLICO_NOMBRE=\"Nombre\" F1_PUBLICO_EMAIL=\"<id>+<usuario>@users.noreply.github.com\" bash scripts/exportar-publico.sh (el correo noreply esta en GitHub, Settings > Emails)."
if [ -n "$nombre$correo" ] && { [ -z "$nombre" ] || [ -z "$correo" ]; }; then
    echo "FALLO: hay que dar F1_PUBLICO_NOMBRE y F1_PUBLICO_EMAIL juntos." >&2
    identidad=1
elif [ -n "$nombre" ]; then
    if [ -d "$destino/.git" ]; then
        git -C "$destino" config --local user.name "$nombre"
        git -C "$destino" config --local user.email "$correo"
    fi
elif [ -d "$destino/.git" ]; then
    nombre="$(git -C "$destino" config --local user.name || true)"
    correo="$(git -C "$destino" config --local user.email || true)"
    case "$correo" in
        *@users.noreply.github.com) ;;
        *)
            echo "FALLO: la identidad de dist/F1-06-PC/.git (\"$nombre <$correo>\") no es un correo noreply de GitHub" \
                 "ni se ha elegido con F1_PUBLICO_NOMBRE/F1_PUBLICO_EMAIL; puede ser la copiada del repositorio privado." >&2
            identidad=1 ;;
    esac
else
    echo "FALLO: falta la identidad del commit publico (F1_PUBLICO_NOMBRE y F1_PUBLICO_EMAIL)." >&2
    identidad=1
fi
if [ "$identidad" = 0 ]; then
    echo "Identidad del repositorio publico: $nombre <$correo>"
    if [ ! -d "$destino/.git" ]; then
        echo "Aviso: dist/F1-06-PC aun no tiene .git; tras git init vuelve a exportar con la identidad para guardarla." >&2
    elif git -C "$destino" rev-parse -q --verify HEAD > /dev/null; then
        otros=""
        while IFS=$'\t' read -r h an ae cn ce; do
            if [ "$an" != "$nombre" ] || [ "$ae" != "$correo" ] || [ "$cn" != "$nombre" ] || [ "$ce" != "$correo" ]; then
                otros+="  $h autor $an <$ae>, committer $cn <$ce>"$'\n'
            fi
        done < <(git -C "$destino" log --format='%h%x09%an%x09%ae%x09%cn%x09%ce')
        if [ -n "$otros" ]; then
            echo "FALLO: commits de dist/F1-06-PC con otra identidad:" >&2
            printf '%s' "$otros" >&2
            echo "Corrigelo antes de subirlo (con un solo commit): git -C dist/F1-06-PC commit --amend --reset-author --no-edit" >&2
            identidad=1
        fi
    fi
fi
if [ "$identidad" != 0 ]; then
    echo "$como" >&2
    echo "La exportacion es valida y esta copiada, pero no hagas commit ni push hasta arreglar la identidad." >&2
    exit 2
fi
