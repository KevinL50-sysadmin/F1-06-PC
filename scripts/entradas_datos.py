#!/usr/bin/env python3
"""Punteros a codigo desde los datos del ELF que no tienen entrada en la tabla de despacho (solo lectura).

Un metodo virtual o una retrollamada que el analizador fusiono con la funcion anterior no tiene entrada propia: el
hilo que lo llama con jalr muere con [ee:despacho]. Este script recorre .data/.sndata/.rodata buscando palabras que
apunten dentro de .text a una direccion que no este en g_ps2RecompiledFunctionTable (register_functions.cpp) y las
clasifica:
  vtable   la palabra anterior es el "delta" de una entrada de tabla virtual de GCC 2.95 (0 o un short negativo) y
           hay otras entradas de tabla alrededor
  switch   forma parte de una racha de 3 o mas palabras que apuntan dentro de la misma funcion (tabla de saltos)
  suelto   cualquier otro caso
Para cada candidata dice si va tras un jr $ra (o un j) y su hueco, o tras relleno de nop.

Uso (desde la raiz del proyecto):
  python -I scripts/entradas_datos.py [--todas]     por defecto omite las de tipo switch
  python -I scripts/entradas_datos.py --toml        solo las lineas para entry_points de config/recomp.toml
  python -I scripts/entradas_datos.py --codigo      en vez de los datos, los lui + addiu/ori del codigo que forman
                                                    una direccion de .text sin entrada (retrollamadas, hilos...)
Ojo: muchas candidatas "a mitad" son casualidades (numeros o cadenas que caen en el rango de .text) y los destinos
tras "jr $v0" son casos de switch que la propia funcion ya resuelve. Comprobar a mano antes de anadirlas.
"""
import bisect
import os
import re
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(RAIZ, "juego", "SCES_539.50")
REGISTRO = os.path.join(RAIZ, "third_party", "PS2Recomp", "ps2xRuntime", "src", "runner", "register_functions.cpp")

TEXTO = (0x100000, 0x3E22B8)
DATOS = [(0x3F8580, 0x422E08), (0x422F00, 0x438F00), (0x438F00, 0x477380)]


def cargar_elf():
    with open(ELF, "rb") as f:
        datos = f.read()
    phoff, = struct.unpack_from("<I", datos, 28)
    phentsize, phnum = struct.unpack_from("<HH", datos, 42)
    segmentos = []
    for i in range(phnum):
        p_type, p_off, vaddr, _, filesz, _, _, _ = struct.unpack_from("<8I", datos, phoff + i * phentsize)
        if p_type == 1:
            segmentos.append((vaddr, p_off, filesz))
    return datos, segmentos


def palabra(elf, direccion):
    datos, segmentos = elf
    for vaddr, off, filesz in segmentos:
        if vaddr <= direccion < vaddr + filesz:
            return struct.unpack_from("<I", datos, off + direccion - vaddr)[0]
    return None


def cargar_tabla():
    """Direcciones con entrada en la tabla de despacho y, de cada una, la funcion que la ejecuta."""
    patron = re.compile(r"g_ps2RecompiledFunctionTable\[\d+\] = (\w+); // 0x([0-9a-f]+)")
    entradas = {}
    inicios = set()
    with open(REGISTRO, encoding="latin-1") as f:
        for linea in f:
            m = patron.search(linea)
            if not m:
                continue
            direccion = int(m.group(2), 16)
            funcion = m.group(1)
            entradas[direccion] = funcion
            f_dir = re.search(r"_0x([0-9a-f]+)$", funcion)
            if f_dir and int(f_dir.group(1), 16) == direccion:
                inicios.add(direccion)
    return entradas, sorted(inicios)


def funcion_que_contiene(inicios, direccion):
    i = bisect.bisect_right(inicios, direccion) - 1
    return inicios[i] if i >= 0 else None


def contexto_previo(elf, direccion):
    """Que hay justo antes de la direccion: fin de funcion (jr $ra / j + hueco), relleno de nop u otra cosa."""
    previas = [palabra(elf, direccion - 4 * k) for k in range(1, 5)]
    if any(p is None for p in previas):
        return "?"
    # Saltar el relleno de nop.
    k = 0
    while k < len(previas) and previas[k] == 0:
        k += 1
    if k + 1 < len(previas):
        salto = previas[k + 1]  # la instruccion antes del hueco de retardo
        if salto == 0x03E00008:
            return "tras jr $ra" + (" + nop" if k else "")
        if (salto >> 26) == 0x02:
            return "tras j" + (" + nop" if k else "")
    if k > 0:
        return "tras nop"
    return "a mitad"


def buscar_en_codigo(elf, entradas, inicios):
    """lui + addiu/ori que calculan una direccion de .text que no esta en la tabla de despacho."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ee_xref
    instr = ee_xref.cargar_listado()
    inicio_funcion = set(inicios)
    lui = {}
    vistas = {}
    for d in sorted(instr):
        if d in inicio_funcion:
            lui = {}
        p = instr[d][0]
        op, rs, rt, imm = p >> 26, (p >> 21) & 31, (p >> 16) & 31, p & 0xFFFF
        valor = None
        if op == 0x0F:
            lui[rt] = imm << 16
            continue
        if op in (0x09, 0x19) and rs in lui:
            valor = (lui[rs] + ee_xref.con_signo(imm)) & 0xFFFFFFFF
        elif op == 0x0D and rs in lui:
            valor = lui[rs] | imm
        if valor is not None and TEXTO[0] <= valor < TEXTO[1] and (valor & 3) == 0 and valor not in entradas:
            vistas.setdefault(valor, []).append(d)
        if op == 0:
            lui.pop((p >> 11) & 31, None)
        elif op not in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17, 0x10, 0x11, 0x12,
                        0x28, 0x29, 0x2B, 0x3F, 0x1F, 0x39, 0x2A, 0x2E, 0x2C, 0x2D, 0x3E):
            lui.pop(rt, None)
    for v in sorted(vistas):
        func = funcion_que_contiene(inicios, v)
        desde = ",".join(f"0x{d:06x}" for d in vistas[v][:4])
        print(f"0x{v:06x}  en sub_{func:08X}  {contexto_previo(elf, v):18s} calculada en {desde}")
    print(f"# total {len(vistas)}")


def main():
    todas = "--todas" in sys.argv
    solo_toml = "--toml" in sys.argv
    elf = cargar_elf()
    entradas, inicios = cargar_tabla()
    if "--codigo" in sys.argv:
        buscar_en_codigo(elf, entradas, inicios)
        return

    # Todas las palabras de datos que apuntan a .text.
    punteros = []
    for ini, fin in DATOS:
        for loc in range(ini, fin, 4):
            v = palabra(elf, loc)
            if v is not None and TEXTO[0] <= v < TEXTO[1] and (v & 3) == 0:
                punteros.append((loc, v))
    por_loc = dict(punteros)

    candidatas = {}
    for loc, v in punteros:
        if v in entradas:
            continue
        func = funcion_que_contiene(inicios, v)
        # Racha de palabras consecutivas que apuntan a la misma funcion: tabla de saltos de un switch.
        racha = 1
        for paso in (-4, 4):
            l = loc + paso
            while l in por_loc and funcion_que_contiene(inicios, por_loc[l]) == func:
                racha += 1
                l += paso
        delta = palabra(elf, loc - 4)
        vecinos_vtable = sum(1 for l in (loc - 8, loc + 8) if l in por_loc)
        delta_ok = delta is not None and (delta == 0 or (delta & 0xFFFF) == 0 and (delta >> 16) >= 0x8000)
        if racha >= 3:
            clase = "switch"
        elif delta_ok and vecinos_vtable:
            clase = "vtable"
        else:
            clase = "suelto"
        c = candidatas.setdefault(v, {"clase": clase, "locs": [], "func": func})
        c["locs"].append(loc)
        if clase == "vtable":
            c["clase"] = "vtable"

    filas = []
    for v in sorted(candidatas):
        c = candidatas[v]
        if c["clase"] == "switch" and not todas:
            continue
        filas.append((v, c))

    if solo_toml:
        for v, c in filas:
            print(f'  "datos_{v:06X}@0x{v:08X}",')
        return

    resumen = {}
    for v, c in filas:
        resumen[c["clase"]] = resumen.get(c["clase"], 0) + 1
        func = c["func"]
        locs = ",".join(f"0x{l:06x}" for l in c["locs"][:4]) + ("..." if len(c["locs"]) > 4 else "")
        print(f"0x{v:06x}  {c['clase']:7s} en sub_{func:08X}  {contexto_previo(elf, v):18s} desde {locs}")
    print(f"# total {len(filas)}: " + ", ".join(f"{k} {n}" for k, n in sorted(resumen.items())))


if __name__ == "__main__":
    main()
