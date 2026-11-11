#!/usr/bin/env python3
"""Referencias cruzadas en el codigo del EE de F1 06 (solo lectura).

Usa el listado que sale de los comentarios del codigo traducido (analisis/listado-ee-crudo.txt) y el ELF del juego.
Para regenerar el listado tras recompilar.cmd:
  cd third_party/PS2Recomp/ps2xRuntime/src/runner
  cat sub_*.cpp entry_*.cpp | grep -o "// 0x[0-9a-f]*: 0x[0-9a-f]*  .*" | sed 's#^// ##' | sort -u > ../../../../../analisis/listado-ee-crudo.txt

Uso (desde la raiz del proyecto):
  python -I scripts/ee_xref.py rango INI FIN        instrucciones entre dos direcciones
  python -I scripts/ee_xref.py funcion DIR          la funcion que contiene DIR (segun los archivos sub_*)
  python -I scripts/ee_xref.py llamadores DIR       instrucciones jal/j a DIR
  python -I scripts/ee_xref.py refs VALOR [VALOR2]  instrucciones que calculan una direccion en [VALOR, VALOR2)
                                                    (lui + addiu/ori o lui + desplazamiento de load/store)
  python -I scripts/ee_xref.py punteros VALOR [VALOR2]  palabras del ELF con un valor en [VALOR, VALOR2]
  python -I scripts/ee_xref.py leer DIR [N]         N palabras del ELF desde DIR
  python -I scripts/ee_xref.py cadena DIR           cadena terminada en cero del ELF
"""
import bisect
import os
import re
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LISTADO = os.path.join(RAIZ, "analisis", "listado-ee-crudo.txt")
ELF = os.path.join(RAIZ, "juego", "SCES_539.50")
RUNNER = os.path.join(RAIZ, "third_party", "PS2Recomp", "ps2xRuntime", "src", "runner")

CARGAS_GUARDADOS = {0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37, 0x28, 0x29, 0x2B, 0x3F, 0x1E, 0x1F, 0x31, 0x39,
                    0x22, 0x26, 0x2A, 0x2E, 0x1A, 0x1B, 0x2C, 0x2D, 0x36, 0x3E}


def cargar_listado():
    instr = {}
    patron = re.compile(r"^0x([0-9a-f]+): 0x([0-9a-f]+)  (.*)$")
    with open(LISTADO, encoding="latin-1") as f:
        for linea in f:
            m = patron.match(linea.rstrip("\n"))
            if m:
                instr[int(m.group(1), 16)] = (int(m.group(2), 16), m.group(3))
    return instr


def cargar_elf():
    with open(ELF, "rb") as f:
        datos = f.read()
    phoff, = struct.unpack_from("<I", datos, 28)
    phentsize, phnum = struct.unpack_from("<HH", datos, 42)
    segmentos = []
    for i in range(phnum):
        p_type, p_off, vaddr, _, filesz, memsz, _, _ = struct.unpack_from("<8I", datos, phoff + i * phentsize)
        if p_type == 1:
            segmentos.append((vaddr, p_off, filesz, memsz))
    return datos, segmentos


def leer_palabra(elf, direccion):
    datos, segmentos = elf
    for vaddr, off, filesz, memsz in segmentos:
        if vaddr <= direccion < vaddr + memsz:
            if direccion + 4 <= vaddr + filesz:
                return struct.unpack_from("<I", datos, off + direccion - vaddr)[0]
            return 0
    return None


def inicios_funciones():
    inicios = []
    for nombre in os.listdir(RUNNER):
        m = re.match(r"^sub_([0-9A-Fa-f]{8})_", nombre)
        if m:
            inicios.append(int(m.group(1), 16))
    return sorted(set(inicios))


def con_signo(imm):
    return imm - 0x10000 if imm & 0x8000 else imm


def escribir(instr, direccion):
    if direccion in instr:
        palabra, texto = instr[direccion]
        print(f"0x{direccion:06x}: {palabra:08x}  {texto}")


def cmd_rango(instr, ini, fin):
    for d in range(ini & ~3, fin, 4):
        escribir(instr, d)


def cmd_funcion(instr, direccion):
    inicios = inicios_funciones()
    i = bisect.bisect_right(inicios, direccion) - 1
    ini = inicios[i] if i >= 0 else direccion
    fin = inicios[i + 1] if i + 1 < len(inicios) else direccion + 4
    print(f"; funcion 0x{ini:06x} - 0x{fin:06x}")
    cmd_rango(instr, ini, fin)


def cmd_llamadores(instr, destino):
    inicios = inicios_funciones()
    for d in sorted(instr):
        palabra = instr[d][0]
        op = palabra >> 26
        if op in (2, 3) and (((palabra & 0x3FFFFFF) << 2) | ((d + 4) & 0xF0000000)) == destino:
            i = bisect.bisect_right(inicios, d) - 1
            print(f"0x{d:06x} (en 0x{inicios[i]:06x}): {instr[d][1]}")


def cmd_refs(instr, ini, fin):
    inicios = set(inicios_funciones())
    lui = {}
    for d in sorted(instr):
        if d in inicios:
            lui = {}
        palabra = instr[d][0]
        op = palabra >> 26
        rs = (palabra >> 21) & 31
        rt = (palabra >> 16) & 31
        imm = palabra & 0xFFFF
        valor = None
        if op == 0x0F:
            lui[rt] = imm << 16
            if ini <= (imm << 16) < fin:
                valor = imm << 16
            if valor is None:
                continue
        elif op in (0x09, 0x19) and rs in lui:  # addiu / daddiu
            valor = (lui[rs] + con_signo(imm)) & 0xFFFFFFFF
        elif op == 0x0D and rs in lui:  # ori
            valor = lui[rs] | imm
        elif op in CARGAS_GUARDADOS and rs in lui:
            valor = (lui[rs] + con_signo(imm)) & 0xFFFFFFFF
        if valor is not None and ini <= valor < fin and op != 0x0F:
            print(f"0x{d:06x}: {instr[d][1]}    ; = 0x{valor:x}")
        # Una escritura en rt invalida su lui (salvo los guardados, que no escriben rt).
        if op not in (0x28, 0x29, 0x2B, 0x3F, 0x1F, 0x39, 0x2A, 0x2E, 0x2C, 0x2D, 0x3E) and op != 0x0F:
            if op == 0:
                rd = (palabra >> 11) & 31
                lui.pop(rd, None)
            elif op not in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17, 0x10, 0x11, 0x12):
                lui.pop(rt, None)


def cmd_punteros(elf, ini, fin):
    datos, segmentos = elf
    for vaddr, off, filesz, _ in segmentos:
        for i in range(0, filesz - 3, 4):
            v = struct.unpack_from("<I", datos, off + i)[0]
            if ini <= v <= fin:
                print(f"0x{vaddr + i:06x}: 0x{v:08x}")


def cmd_leer(elf, direccion, n):
    for i in range(n):
        d = direccion + 4 * i
        v = leer_palabra(elf, d)
        print(f"0x{d:06x}: " + ("--------" if v is None else f"0x{v:08x}"))


def cmd_cadena(elf, direccion):
    texto = bytearray()
    while len(texto) < 256:
        v = leer_palabra(elf, direccion & ~3)
        if v is None:
            break
        b = (v >> (8 * (direccion & 3))) & 0xFF
        if b == 0:
            break
        texto.append(b)
        direccion += 1
    print(texto.decode("latin-1"))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    comando = sys.argv[1]
    numeros = [int(a, 16) if a.lower().startswith("0x") else int(a, 0) for a in sys.argv[2:]]
    if comando in ("rango", "funcion", "llamadores", "refs"):
        instr = cargar_listado()
        if comando == "rango":
            cmd_rango(instr, numeros[0], numeros[1])
        elif comando == "funcion":
            cmd_funcion(instr, numeros[0])
        elif comando == "llamadores":
            cmd_llamadores(instr, numeros[0])
        else:
            cmd_refs(instr, numeros[0], numeros[1] if len(numeros) > 1 else numeros[0] + 1)
    else:
        elf = cargar_elf()
        if comando == "punteros":
            cmd_punteros(elf, numeros[0], numeros[1] if len(numeros) > 1 else numeros[0])
        elif comando == "leer":
            cmd_leer(elf, numeros[0], numeros[1] if len(numeros) > 1 else 16)
        elif comando == "cadena":
            cmd_cadena(elf, numeros[0])
        else:
            print(__doc__)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
