#!/usr/bin/env python3
"""Informe de un ejecutable de PS2 (ELF del EE) o de un modulo IRX del IOP. Solo lectura.

Uso:
  python -I -X utf8 elf_ps2.py <archivo> [<archivo> ...]

Para el ELF del EE: secciones, simbolos (.symtab, .mdebug, DWARF), compilador, librerias de Sony
(cadenas "PsII..."), rutas de archivos, funciones estimadas y microprogramas de VU subidos con MPG.
Para un IRX: nombre y version del modulo, y librerias que importa y exporta.
"""
import hashlib
import re
import struct
import sys
import zlib
from collections import Counter

SHT_NAMES = {
    0: "NULL", 1: "PROGBITS", 2: "SYMTAB", 3: "STRTAB", 4: "RELA", 8: "NOBITS", 9: "REL",
    0x70000005: "MIPS_DEBUG", 0x70000006: "MIPS_REGINFO", 0x7000001E: "MIPS_DWARF",
    0x70000080: "SCE_IOPMOD", 0x70000090: "SCE_EEMOD",
}
ET_NAMES = {1: "reubicable", 2: "ejecutable", 0xFF80: "IRX", 0xFF81: "IRX v2", 0xFF90: "ERX", 0xFF91: "ERX v2"}
SHT_SYMTAB, SHT_NOBITS, SHT_REL, SHT_MIPS_DEBUG, SHT_IOPMOD = 2, 8, 9, 0x70000005, 0x70000080
SHF_EXECINSTR = 0x4

VU_LOWER_NOP = 0x8000033C  # NOP del lado inferior de una pareja de instrucciones de VU
VU_UPPER_NOP = 0x000002FF  # NOP del lado superior
VU_E_BIT = 0x40000000      # bit E (fin de microprograma) en la palabra superior


def cstr(data, off):
    end = data.find(b"\x00", off)
    return data[off:end if end >= 0 else len(data)].decode("latin-1")


class Elf:
    def __init__(self, data):
        if data[:4] != b"\x7fELF":
            raise ValueError("no es un ELF")
        if data[4] != 1 or data[5] != 1:
            raise ValueError("no es un ELF de 32 bits little-endian")
        (self.type, self.machine, _, self.entry, phoff, shoff, self.flags, _, phentsize, phnum,
         shentsize, shnum, shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        self.data = data
        self.segments = [struct.unpack_from("<8I", data, phoff + i * phentsize) for i in range(phnum)]
        raw = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)] if shoff else []
        names_off = raw[shstrndx][4] if raw and shstrndx < len(raw) else None
        self.sections = [dict(name=cstr(data, names_off + s[0]) if names_off is not None else "",
                              type=s[1], flags=s[2], addr=s[3], offset=s[4], size=s[5], link=s[6])
                         for s in raw]

    def content(self, s):
        return b"" if s["type"] == SHT_NOBITS else self.data[s["offset"]: s["offset"] + s["size"]]


def symtab_symbols(elf):
    """(nombre, valor, tamano, tipo) de .symtab. Tipo 2 = funcion, 1 = dato."""
    out = []
    for s in elf.sections:
        if s["type"] != SHT_SYMTAB:
            continue
        strtab = elf.sections[s["link"]]
        raw = elf.content(s)
        for i in range(0, len(raw) - 15, 16):
            st_name, value, size, info, _, _ = struct.unpack_from("<IIIBBH", raw, i)
            if st_name:
                out.append((cstr(elf.data, strtab["offset"] + st_name), value, size, info & 0xF))
    return out


HDRR_KEYS = ["ilineMax", "cbLine", "cbLineOffset", "idnMax", "cbDnOffset", "ipdMax", "cbPdOffset", "isymMax",
             "cbSymOffset", "ioptMax", "cbOptOffset", "iauxMax", "cbAuxOffset", "issMax", "cbSsOffset",
             "issExtMax", "cbSsExtOffset", "ifdMax", "cbFdOffset", "crfd", "cbRfdOffset", "iextMax", "cbExtOffset"]
ST_PROC, ST_STATIC_PROC = 6, 14


def mdebug_info(elf):
    """Lee la tabla de simbolos ECOFF (.mdebug): ficheros fuente y procedimientos con nombre."""
    sec = next((s for s in elf.sections if s["type"] == SHT_MIPS_DEBUG), None)
    if not sec:
        return None
    data = elf.data
    magic = struct.unpack_from("<h", data, sec["offset"])[0]
    if magic != 0x7009:
        return {"error": f"cabecera desconocida 0x{magic & 0xFFFF:04X}"}
    h = dict(zip(HDRR_KEYS, struct.unpack_from("<23i", data, sec["offset"] + 4)))

    def readable(base):
        """Comprueba si los desplazamientos son absolutos (base 0) o relativos a la seccion."""
        names = []
        for i in range(min(h["ifdMax"], 20)):
            off = base + h["cbFdOffset"] + i * 72
            if off + 72 > len(data):
                return False
            iss_base, rss = struct.unpack_from("<ii", data, off + 8)[0], struct.unpack_from("<i", data, off + 4)[0]
            if rss >= 0:
                p = base + h["cbSsOffset"] + iss_base + rss
                if not 0 <= p < len(data):
                    return False
                names.append(cstr(data, p))
        return bool(names) and all(n and n.isprintable() for n in names)

    base = 0 if readable(0) else sec["offset"] if readable(sec["offset"]) else None
    info = {"cabecera": h, "base": base}
    if base is None:
        return info
    files, procs = [], []
    for i in range(h["ifdMax"]):
        off = base + h["cbFdOffset"] + i * 72
        rss, iss_base, _, isym_base, csym = struct.unpack_from("<iiiii", data, off + 4)
        if rss >= 0:
            files.append(cstr(data, base + h["cbSsOffset"] + iss_base + rss))
        for j in range(csym):
            soff = base + h["cbSymOffset"] + (isym_base + j) * 12
            iss, value, bits = struct.unpack_from("<iII", data, soff)
            if bits & 0x3F in (ST_PROC, ST_STATIC_PROC) and iss >= 0:
                procs.append((cstr(data, base + h["cbSsOffset"] + iss_base + iss), value))
    ext_procs = []
    for i in range(h["iextMax"]):
        off = base + h["cbExtOffset"] + i * 16
        iss, value, bits = struct.unpack_from("<iII", data, off + 4)
        if bits & 0x3F == ST_PROC and iss >= 0:
            ext_procs.append((cstr(data, base + h["cbSsExtOffset"] + iss), value))
    info.update(ficheros=files, procs=procs, ext_procs=ext_procs)
    return info


def strings(data, minlen=5):
    return [m.group().decode("ascii") for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, data)]


def jal_targets(elf):
    """Destinos distintos de JAL dentro del codigo: estimacion del numero de funciones."""
    code = [s for s in elf.sections if s["flags"] & SHF_EXECINSTR and s["type"] != SHT_NOBITS]
    lo = min((s["addr"] for s in code), default=0)
    hi = max((s["addr"] + s["size"] for s in code), default=0)
    targets = set()
    for s in code:
        raw = elf.content(s)
        for i, (w,) in enumerate(struct.iter_unpack("<I", raw[: len(raw) // 4 * 4])):
            if w >> 26 == 3:  # JAL
                t = ((s["addr"] + i * 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                if lo <= t < hi:
                    targets.add(t)
    return targets


def vu_microprograms(data):
    """Bloques subidos con el VIFcode MPG que tienen aspecto de microcodigo de VU."""
    found = []
    for off in range(4, len(data) - 8, 8):  # MPG va en palabra impar: el codigo queda alineado a 8
        w = struct.unpack_from("<I", data, off)[0]
        if (w >> 24) & 0x7F != 0x4A:
            continue
        num = (w >> 16) & 0xFF or 256
        addr = w & 0xFFFF
        start, end = off + 4, off + 4 + num * 8
        if addr + num > 2048 or end > len(data):
            continue
        nops = ends = 0
        for lower, upper in struct.iter_unpack("<II", data[start:end]):
            nops += lower == VU_LOWER_NOP or upper == VU_UPPER_NOP
            ends += bool(upper & VU_E_BIT)
        if num >= 4 and nops * 4 >= num and ends:
            found.append((off, addr, num, hashlib.sha1(data[start:end]).hexdigest()[:12]))
    return found


def iop_libs(data, magic):
    """Tablas de import (0x41E00000) o export (0x41C00000) de un modulo del IOP."""
    libs = []
    pattern = struct.pack("<I", magic)
    for m in re.finditer(re.escape(pattern), data):
        off = m.start()
        if off % 4 or off + 20 > len(data):
            continue
        ver = struct.unpack_from("<H", data, off + 8)[0]
        name = data[off + 12: off + 20].split(b"\x00")[0].decode("ascii", "replace")
        if name and name.isprintable():
            libs.append(f"{name} {ver >> 8}.{ver & 0xFF}")
    return libs


def report_irx(path, elf):
    sec = next((s for s in elf.sections if s["type"] == SHT_IOPMOD), None)
    name, version = "?", 0
    if sec:
        raw = elf.content(sec)
        version = struct.unpack_from("<H", raw, 24)[0]
        name = cstr(raw, 26)
    imports = sorted(set(iop_libs(elf.data, 0x41E00000)))
    exports = sorted(set(iop_libs(elf.data, 0x41C00000)))
    print(f"{path}: modulo '{name}' v{version >> 8}.{version & 0xFF}")
    print(f"    exporta: {', '.join(exports) or '-'}")
    print(f"    importa: {', '.join(imports) or '-'}")


def report_ee(path, elf):
    data = elf.data
    print(f"Archivo: {path}")
    print(f"Tamano: {len(data):,} bytes  CRC32 {zlib.crc32(data):08x}  SHA-1 {hashlib.sha1(data).hexdigest()}")
    print(f"Tipo: {ET_NAMES.get(elf.type, hex(elf.type))}  maquina {elf.machine}  flags 0x{elf.flags:08X}"
          f"  entrada 0x{elf.entry:08X}")

    print("\n--- Segmentos cargables ---")
    for p_type, p_off, vaddr, _, filesz, memsz, flags, _ in elf.segments:
        if p_type == 1:
            print(f"0x{vaddr:08X}-0x{vaddr + memsz:08X}  en archivo {filesz:>10,}  en memoria {memsz:>10,}"
                  f"  {'R' if flags & 4 else '-'}{'W' if flags & 2 else '-'}{'X' if flags & 1 else '-'}")

    print("\n--- Secciones ---")
    for s in elf.sections:
        if s["type"]:
            kind = SHT_NAMES.get(s["type"], hex(s["type"]))
            print(f"{s['name']:<16} {kind:<13} 0x{s['addr']:08X} {s['size']:>10,}")

    print("\n--- Simbolos e informacion de depuracion ---")
    syms = symtab_symbols(elf)
    funcs = [x for x in syms if x[3] == 2]
    print(f".symtab: {len(syms):,} simbolos, {len(funcs):,} funciones")
    for name, value, _, _ in sorted(funcs, key=lambda x: x[1])[:12]:
        print(f"    0x{value:08X} {name}")
    dwarf = [s["name"] for s in elf.sections if s["name"].startswith(".debug")]
    print(f"DWARF: {', '.join(dwarf) if dwarf else 'no'}")
    rels = [s for s in elf.sections if s["type"] == SHT_REL]
    print(f"Relocaciones: {sum(s['size'] // 8 for s in rels):,} en {len(rels)} secciones" if rels else "Relocaciones: no")
    md = mdebug_info(elf)
    if md is None:
        print(".mdebug: no")
    elif "error" in md:
        print(f".mdebug: {md['error']}")
    else:
        h = md["cabecera"]
        print(f".mdebug: {h['ifdMax']:,} ficheros fuente, {h['ipdMax']:,} procedimientos, "
              f"{h['isymMax']:,} simbolos locales, {h['iextMax']:,} externos")
        if md["base"] is None:
            print("    (no se han podido interpretar los desplazamientos)")
        else:
            print(f"    procedimientos con nombre: {len(md['procs']):,} locales, {len(md['ext_procs']):,} globales")
            dirs = Counter(re.split(r"[\\/]", f)[0] if re.search(r"[\\/]", f) else "(sin ruta)" for f in md["ficheros"])
            print(f"    raices de ficheros fuente: {dict(dirs.most_common(8))}")
            for f in md["ficheros"][:15]:
                print(f"    {f}")
            for name, value in sorted(md["ext_procs"], key=lambda x: x[1])[:12]:
                print(f"    0x{value:08X} {name}")

    targets = jal_targets(elf)
    print(f"\nFunciones estimadas (destinos de JAL distintos): {len(targets):,}")

    strs = strings(data)
    print("\n--- Compilador y herramientas ---")
    for s in sorted({s for s in strs if re.search(r"GCC: |SN Systems|ProDG|Metrowerks|CodeWarrior|MWCC|SNC ", s)})[:10]:
        print(f"    {s}")
    print("\n--- Librerias de Sony enlazadas (cadenas PsII) ---")
    libs = sorted({re.sub(r"\s+", " ", m) for s in strs for m in re.findall(r"PsII\w+\s+\d+", s)})
    print("    " + (", ".join(libs) if libs else "(ninguna)"))
    print("\n--- Rutas de archivo y dispositivos ---")
    paths = sorted({s for s in strs if re.search(r"(?i)\b(cdrom0?|host\d?|mc\d|rom\d|pfs\d|hdd\d|usb\w*):", s)})
    for s in paths[:60]:
        print(f"    {s}")
    if len(paths) > 60:
        print(f"    ... y {len(paths) - 60} mas")
    exts = Counter(m.upper() for s in strs for m in re.findall(r"\.([A-Za-z0-9]{2,4})\b", s)
                   if re.search(r"[\\/]|\.(?i:wad|irx|ipf|awb|pss|elf|erx|img|bin|idx)\b", s))
    print(f"Extensiones citadas en rutas: {dict(exts.most_common(15))}")

    print("\n--- Microprogramas de VU (VIFcode MPG) ---")
    mpg = vu_microprograms(data)
    unique = {}
    for off, addr, num, digest in mpg:
        unique.setdefault(digest, (off, addr, num))
    print(f"Subidas encontradas: {len(mpg)}  distintas: {len(unique)}  "
          f"instrucciones distintas: {sum(n for _, _, n in unique.values()):,}")
    for digest, (off, addr, num) in sorted(unique.items(), key=lambda x: x[1][0])[:40]:
        print(f"    archivo 0x{off:07X}  destino {addr:>4}  {num:>3} instrucciones  {digest}")
    pairs = sum(1 for lo, up in struct.iter_unpack("<II", data[: len(data) // 8 * 8])
                if lo == VU_LOWER_NOP and up == VU_UPPER_NOP)
    print(f"Parejas NOP|NOP de VU en todo el archivo: {pairs:,}")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            data = f.read()
        try:
            elf = Elf(data)
        except (ValueError, struct.error) as e:
            print(f"{path}: {e}")
            continue
        if elf.type in (0xFF80, 0xFF81):
            report_irx(path, elf)
        else:
            report_ee(path, elf)


if __name__ == "__main__":
    main()
