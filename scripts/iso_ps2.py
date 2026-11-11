#!/usr/bin/env python3
"""Lee la ISO de un juego de PS2 sin montarla ni modificarla (ISO9660, solo lectura).

Uso:
  python -I -X utf8 iso_ps2.py info    <iso>
  python -I -X utf8 iso_ps2.py listar  <iso>
  python -I -X utf8 iso_ps2.py huella  <iso>
  python -I -X utf8 iso_ps2.py extraer <iso> <destino> <patron> [<patron> ...]

Patrones: comodines sin distinguir mayusculas sobre la ruta dentro del disco
("SYSTEM.CNF", "*.IRX", "MODULES/*"). "@arranque" es el ejecutable de BOOT2 en SYSTEM.CNF.
"""
import fnmatch
import hashlib
import os
import re
import struct
import sys
import zlib
from collections import Counter

SECTOR = 2048

# Tipos de ELF que aparecen en los discos de PS2 (campo e_type)
ELF_TYPES = {
    1: "ELF reubicable",
    2: "ELF ejecutable (EE)",
    0xFF80: "IRX (modulo del IOP)",
    0xFF81: "IRX v2 (modulo del IOP)",
    0xFF90: "ERX (modulo reubicable del EE)",
    0xFF91: "ERX v2 (modulo reubicable del EE)",
}


def parse_record(buf, off):
    """Registro de directorio ISO9660 -> (lba, tamano, flags, nombre)."""
    lba = struct.unpack_from("<I", buf, off + 2)[0]
    size = struct.unpack_from("<I", buf, off + 10)[0]
    flags = buf[off + 25]
    name_len = buf[off + 32]
    return lba, size, flags, bytes(buf[off + 33: off + 33 + name_len])


class Iso:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        pvd = self.read(16, SECTOR)
        if len(pvd) < SECTOR or pvd[0] != 1 or pvd[1:6] != b"CD001":
            sys.exit(f"{path}: no parece una imagen ISO9660 (no hay descriptor primario en el sector 16)")
        self.volume_id = pvd[40:72].decode("ascii", "replace").strip()
        self.blocks = struct.unpack_from("<I", pvd, 80)[0]
        self.root = parse_record(pvd, 156)

    def read(self, lba, size):
        self.f.seek(lba * SECTOR)
        return self.f.read(size)

    def files(self, rec=None, prefix=""):
        """Recorre el disco y devuelve (ruta, lba, tamano) de cada archivo."""
        lba, size, _, _ = rec or self.root
        data = self.read(lba, size)
        off = 0
        while off < len(data):
            length = data[off]
            if length == 0:  # resto del sector sin registros: pasa al siguiente
                off = (off // SECTOR + 1) * SECTOR
                continue
            child = parse_record(data, off)
            off += length
            if child[3] in (b"\x00", b"\x01"):  # entradas "." y ".."
                continue
            name = child[3].decode("ascii", "replace").split(";")[0].rstrip(".")
            if child[2] & 0x02:
                yield from self.files(child, prefix + name + "/")
            else:
                if child[2] & 0x80:
                    print(f"AVISO: {prefix + name} usa varias extensiones; solo se lee la primera", file=sys.stderr)
                yield prefix + name, child[0], child[1]

    def system_cnf(self, entries):
        for path, lba, size in entries:
            if path.upper() == "SYSTEM.CNF":
                return self.read(lba, size).decode("ascii", "replace")
        return None


def boot_path(cnf):
    """Ruta del ejecutable de arranque segun BOOT2 (p. ej. 'SCES_123.45')."""
    m = re.search(r"BOOT2\s*=\s*cdrom0?:\\?([^\s;]+)", cnf or "", re.I)
    return m.group(1).replace("\\", "/") if m else None


def elf_kind(iso, lba, size):
    if size < 52:
        return None
    head = iso.read(lba, 20)
    if head[:4] != b"\x7fELF":
        return None
    e_type = struct.unpack_from("<H", head, 16)[0]
    return ELF_TYPES.get(e_type, f"ELF tipo 0x{e_type:04X}")


def cmd_info(iso):
    entries = list(iso.files())
    total = sum(size for _, _, size in entries)
    print(f"Volumen: {iso.volume_id}")
    print(f"Imagen: {iso.blocks} sectores ({iso.blocks * SECTOR / 2**20:,.1f} MB)")
    print(f"Archivos: {len(entries)} ({total / 2**20:,.1f} MB)")
    cnf = iso.system_cnf(entries)
    print("\n--- SYSTEM.CNF ---")
    print(cnf.strip() if cnf else "(no encontrado)")
    boot = boot_path(cnf)
    print(f"\nEjecutable de arranque: {boot}")

    print("\n--- Ejecutables y modulos (detectados por su cabecera ELF) ---")
    for path, lba, size in entries:
        kind = elf_kind(iso, lba, size)
        if kind:
            print(f"{size:>12,}  {kind:<32} {path}")

    print("\n--- Extensiones ---")
    exts = Counter()
    ext_size = Counter()
    for path, _, size in entries:
        ext = os.path.splitext(path)[1].upper() or "(sin)"
        exts[ext] += 1
        ext_size[ext] += size
    for ext, n in exts.most_common():
        print(f"{ext:<8} {n:>5} archivos {ext_size[ext] / 2**20:>10,.1f} MB")

    print("\n--- 15 archivos mas grandes ---")
    for path, _, size in sorted(entries, key=lambda e: -e[2])[:15]:
        print(f"{size / 2**20:>10,.1f} MB  {path}")


def cmd_listar(iso):
    for path, lba, size in iso.files():
        print(f"{lba:>9} {size:>12} {path}")


def cmd_huella(path):
    md5, sha1, crc = hashlib.md5(), hashlib.sha1(), 0
    with open(path, "rb") as f:
        while block := f.read(8 << 20):
            md5.update(block)
            sha1.update(block)
            crc = zlib.crc32(block, crc)
    print(f"Tamano: {os.path.getsize(path):,} bytes")
    print(f"CRC32: {crc:08x}")
    print(f"MD5:   {md5.hexdigest()}")
    print(f"SHA-1: {sha1.hexdigest()}")


def cmd_extraer(iso, dest, patterns):
    entries = list(iso.files())
    boot = boot_path(iso.system_cnf(entries))
    wanted = []
    for path, lba, size in entries:
        for pat in patterns:
            if (pat == "@arranque" and boot and path.upper() == boot.upper()) or \
               fnmatch.fnmatch(path.upper(), pat.upper()):
                wanted.append((path, lba, size))
                break
    if not wanted:
        sys.exit("Ningun archivo coincide con los patrones")
    for path, lba, size in wanted:
        out = os.path.join(dest, *path.split("/"))
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        iso.f.seek(lba * SECTOR)
        left = size
        with open(out, "wb") as f:
            while left:
                block = iso.f.read(min(left, 8 << 20))
                if not block:
                    sys.exit(f"La ISO se acaba antes de tiempo leyendo {path}")
                f.write(block)
                left -= len(block)
        print(f"{size:>12,}  {path}")
    print(f"Extraidos {len(wanted)} archivos en {dest}")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 3 or sys.argv[1] not in ("info", "listar", "huella", "extraer"):
        sys.exit(__doc__)
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "huella":
        cmd_huella(path)
        return
    iso = Iso(path)
    if cmd == "info":
        cmd_info(iso)
    elif cmd == "listar":
        cmd_listar(iso)
    else:
        if len(sys.argv) < 5:
            sys.exit(__doc__)
        cmd_extraer(iso, sys.argv[3], sys.argv[4:])


if __name__ == "__main__":
    main()
