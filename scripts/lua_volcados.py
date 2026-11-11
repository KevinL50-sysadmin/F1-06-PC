#!/usr/bin/env python3
"""Trozos de Lua del front-end volcados con F1_LUA_VOLCAR y memoria de Lua por pantalla. Solo lectura.

Uso:
  python -I scripts/lua_volcados.py indice logs/lua                 resumen de indice.txt (formato y tamanos)
  python -I scripts/lua_volcados.py buscar logs/lua CADENA [...]    trozos (y funciones) con esas constantes de texto
  python -I scripts/lua_volcados.py pseudo logs/lua SALIDA          pseudo-Lua de todos los trozos en SALIDA/<trozo>.txt
  python -I scripts/lua_volcados.py memoria logs/explorar-X.out.log pico de bytes de Lua (F1_LUA_MEMORIA) entre
                                                                    sucesos graph.outcome

Los trozos son codigo del juego: se quedan en logs\\ (no se suben). El formato del bytecode y el desensamblado estan en
scripts/lua50_ram.py (--archivo).
"""
import contextlib
import importlib.util
import io
import os
import re
import sys


def lua50():
    ruta = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lua50_ram.py")
    spec = importlib.util.spec_from_file_location("lua50_ram", ruta)
    modulo = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modulo)
    return modulo


def leer_indice(carpeta):
    filas = []
    with open(os.path.join(carpeta, "indice.txt"), encoding="latin-1") as f:
        for linea in f:
            if linea.startswith("#") or not linea.strip():
                continue
            campos = linea.rstrip("\n").split("\t")
            filas.append({"nombre": campos[0], "bytes": int(campos[1]), "formato": campos[2], "archivo": campos[3]})
    return filas


def indice(carpeta):
    filas = leer_indice(carpeta)
    por_formato = {}
    for f in filas:
        por_formato.setdefault(f["formato"], []).append(f)
    for formato, lista in sorted(por_formato.items()):
        total = sum(f["bytes"] for f in lista)
        print(f"{formato}: {len(lista)} trozos, {total} bytes (mayor: {max(f['bytes'] for f in lista)})")
    for f in sorted(filas, key=lambda f: -f["bytes"])[:10]:
        print(f"  {f['bytes']:7d}  {f['nombre']}")


def recorrer(proto, ruta="0"):
    yield ruta, proto
    for i, hija in enumerate(proto.hijas):
        yield from recorrer(hija, f"{ruta}.{i}")


def buscar(carpeta, cadenas):
    modulo = lua50()
    for f in leer_indice(carpeta):
        if f["formato"] != "bytecode":
            continue
        proto = modulo.leer_trozo(os.path.join(carpeta, f["archivo"]))
        for ruta, p in recorrer(proto):
            vistas = [c for c in cadenas if c in p.k]
            if vistas:
                print(f"{f['nombre']}  F{ruta}: {', '.join(vistas)}")


def pseudo(carpeta, salida):
    modulo = lua50()
    for f in leer_indice(carpeta):
        if f["formato"] != "bytecode":
            continue
        destino = os.path.join(salida, f["archivo"] + ".txt")
        os.makedirs(os.path.dirname(destino), exist_ok=True)
        texto = io.StringIO()
        with contextlib.redirect_stdout(texto):
            modulo.pseudo_archivo(modulo.leer_trozo(os.path.join(carpeta, f["archivo"])))
        with open(destino, "w", encoding="utf-8") as g:
            g.write(texto.getvalue())
    print(f"pseudo-Lua en {salida}")


def memoria(registro):
    # Las lineas del juego salen a veces pegadas a otras ([MC] ...): se buscan las marcas en todo el texto.
    texto = open(registro, encoding="latin-1").read()
    marcas = []
    for m in re.finditer(r"\[f1-06:lua-mem\] t=([0-9.]+) bytes=(\d+) umbral=(\d+)", texto):
        marcas.append((float(m.group(1)), "mem", int(m.group(2)), int(m.group(3))))
    for m in re.finditer(r"\[f1-06:lua\] t=([0-9.]+) graph\.outcome\(\"([^\"]*)\"\)", texto):
        marcas.append((float(m.group(1)), "suceso", m.group(2), None))
    marcas.sort(key=lambda x: x[0])
    tramo, desde, pico, umbral, total = "arranque", 0.0, 0, 0, 0
    print(f"{'desde':>8}  {'tramo':28s} {'pico':>9} {'umbral':>9}")
    for t, tipo, valor, extra in marcas + [(float("inf"), "suceso", "(fin)", None)]:
        if tipo == "mem":
            if valor > pico:
                pico, umbral = valor, extra
            total = max(total, valor)
            continue
        if pico:
            print(f"{desde:8.1f}  {tramo:28s} {pico:9d} {umbral:9d}")
        tramo, desde, pico, umbral = valor, t, 0, 0
    print(f"pico total: {total} bytes")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    orden = sys.argv[1]
    if orden == "indice":
        indice(sys.argv[2])
    elif orden == "buscar" and len(sys.argv) > 3:
        buscar(sys.argv[2], sys.argv[3:])
    elif orden == "pseudo" and len(sys.argv) > 3:
        pseudo(sys.argv[2], sys.argv[3])
    elif orden == "memoria":
        memoria(sys.argv[2])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
