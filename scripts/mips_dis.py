#!/usr/bin/env python3
"""Desensamblador MIPS I (R3000A) para modulos IRX del IOP. Solo lectura.

Uso (desde la raiz del proyecto):
  python -I scripts/mips_dis.py juego/IOP/CORE.IRX [INI [FIN]] [--base 0x10d00]
  python -I scripts/mips_dis.py juego/IOP/CORE.IRX --imports      tabla de importaciones (stub -> libreria:ordinal)
  python -I scripts/mips_dis.py juego/IOP/CORE.IRX --llamadas lib:ord   quien llama a una importacion
  python -I scripts/mips_dis.py juego/IOP/CORE.IRX --cadenas      cadenas de .rodata/.data con su direccion

Las direcciones son desplazamientos dentro del modulo (base 0) salvo que se pase --base con la direccion de carga
que imprime el runtime ("[IOP] loaded IRX id=2 entry=0x117c8 base=0x10d00" para CORE.IRX). Como se desensambla
con base 0, no hace falta aplicar las reubicaciones: los destinos de jal y los pares lui/addiu ya son relativos.
Las llamadas a los stubs de importacion se anotan como libreria:ordinal (con nombre si se conoce).
"""
import struct
import sys

REG = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
       "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]

NOMBRES = {
    "sysmem": {4: "AllocSysMemory", 5: "FreeSysMemory"},
    "intrman": {4: "RegisterIntrHandler", 5: "ReleaseIntrHandler", 6: "EnableIntr", 7: "DisableIntr",
                17: "CpuSuspendIntr", 18: "CpuResumeIntr", 23: "QueryIntrContext"},
    "ioman": {4: "open", 5: "close", 6: "read", 7: "write", 8: "lseek"},
    "loadcore": {6: "RegisterLibraryEntries"},
    "sifcmd": {4: "sceSifInitCmd", 5: "sceSifExitCmd", 6: "sceSifGetSreg", 7: "sceSifSetSreg",
               8: "sceSifSetCmdBuffer", 9: "sceSifSetSysCmdBuffer", 10: "sceSifAddCmdHandler",
               11: "sceSifRemoveCmdHandler", 12: "sceSifSendCmd", 13: "isceSifSendCmd", 14: "sceSifInitRpc",
               17: "sceSifRegisterRpc", 22: "sceSifRpcLoop"},
    "sifman": {5: "sceSifInit", 6: "sceSifSetDChain", 7: "sceSifSetDma", 8: "sceSifDmaStat"},
    "stdio": {4: "printf"},
    "sysclib": {4: "setjmp", 5: "longjmp", 6: "toupper", 7: "tolower", 12: "memcpy", 13: "memmove", 14: "memset",
                19: "sprintf", 20: "strcat", 22: "strcmp", 23: "strcpy", 27: "strlen", 30: "strncpy", 36: "strtol",
                40: "wmemcopy", 41: "wmemset"},
    "thbase": {4: "CreateThread", 5: "DeleteThread", 6: "StartThread", 7: "StartThreadArgs", 8: "ExitThread",
               9: "ExitDeleteThread", 10: "TerminateThread", 11: "iTerminateThread", 14: "ChangeThreadPriority",
               15: "iChangeThreadPriority", 16: "RotateThreadReadyQueue", 17: "iRotateThreadReadyQueue",
               18: "ReleaseWaitThread", 19: "iReleaseWaitThread", 20: "GetThreadId", 22: "ReferThreadStatus",
               24: "SleepThread", 25: "WakeupThread", 26: "iWakeupThread", 27: "CancelWakeupThread",
               33: "DelayThread", 34: "GetSystemTime", 39: "USec2SysClock", 40: "SysClock2USec"},
    "thsemap": {4: "CreateSema", 5: "DeleteSema", 6: "SignalSema", 7: "iSignalSema", 8: "WaitSema", 9: "PollSema"},
    "thevent": {4: "CreateEventFlag", 5: "DeleteEventFlag", 6: "SetEventFlag", 7: "iSetEventFlag",
                8: "ClearEventFlag", 9: "iClearEventFlag", 10: "WaitEventFlag", 11: "PollEventFlag"},
    "cdvdman": {4: "sceCdInit", 5: "sceCdStandby", 6: "sceCdRead", 7: "sceCdSeek", 8: "sceCdGetError",
                9: "sceCdGetToc", 10: "sceCdSearchFile", 11: "sceCdSync", 12: "sceCdGetDiskType",
                13: "sceCdDiskReady", 14: "sceCdTrayReq", 15: "sceCdStop", 37: "sceCdCallback"},
}


class Irx:
    def __init__(self, ruta):
        d = open(ruta, "rb").read()
        self.datos = d
        phoff = struct.unpack_from("<I", d, 28)[0]
        phnum = struct.unpack_from("<H", d, 44)[0]
        self.imagen = bytearray()
        for i in range(phnum):
            tipo, off, va, _, fs, ms, _, _ = struct.unpack_from("<8I", d, phoff + 32 * i)
            if tipo == 1:
                self.imagen = bytearray(d[off:off + fs]) + bytearray(ms - fs)
        shoff = struct.unpack_from("<I", d, 32)[0]
        shnum = struct.unpack_from("<H", d, 48)[0]
        shstr = struct.unpack_from("<H", d, 50)[0]
        secs = [struct.unpack_from("<10I", d, shoff + 40 * i) for i in range(shnum)]
        nombres = secs[shstr][4]
        self.secciones = {}
        for s in secs:
            nombre = d[nombres + s[0]:d.index(b"\0", nombres + s[0])].decode()
            self.secciones[nombre] = (s[3], s[5])
        self.stubs = {}
        pos = 0
        img = bytes(self.imagen)
        while True:
            pos = img.find(struct.pack("<I", 0x41E00000), pos)
            if pos < 0:
                break
            lib = img[pos + 12:pos + 20].split(b"\0")[0].decode("ascii", "replace")
            p = pos + 20
            while p + 8 <= len(img):
                a, b = struct.unpack_from("<II", img, p)
                if a == 0 and b == 0:
                    break
                if a == 0x03E00008 and (b >> 16) == 0x2400:
                    ordinal = b & 0xFFFF
                    nombre = NOMBRES.get(lib, {}).get(ordinal, "")
                    self.stubs[p] = "%s:%d%s" % (lib, ordinal, (" " + nombre) if nombre else "")
                p += 8
            pos += 4

    def palabra(self, direccion):
        return struct.unpack_from("<I", self.imagen, direccion)[0]


def desensamblar(w, pc, stubs, base):
    op = w >> 26
    rs, rt, rd, sa, fn = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
    imm = w & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    R = REG
    if w == 0:
        return "nop"
    if op == 0:
        especiales = {0: "sll", 2: "srl", 3: "sra"}
        if fn in especiales:
            return "%-7s %s, %s, %d" % (especiales[fn], R[rd], R[rt], sa)
        variables = {4: "sllv", 6: "srlv", 7: "srav"}
        if fn in variables:
            return "%-7s %s, %s, %s" % (variables[fn], R[rd], R[rt], R[rs])
        if fn == 8:
            return "jr      %s" % R[rs]
        if fn == 9:
            return "jalr    %s, %s" % (R[rd], R[rs])
        if fn == 12:
            return "syscall"
        if fn == 13:
            return "break"
        if fn in (16, 18):
            return "%-7s %s" % ({16: "mfhi", 18: "mflo"}[fn], R[rd])
        if fn in (17, 19):
            return "%-7s %s" % ({17: "mthi", 19: "mtlo"}[fn], R[rs])
        if fn in (24, 25, 26, 27):
            return "%-7s %s, %s" % ({24: "mult", 25: "multu", 26: "div", 27: "divu"}[fn], R[rs], R[rt])
        aritmeticas = {32: "add", 33: "addu", 34: "sub", 35: "subu", 36: "and", 37: "or", 38: "xor", 39: "nor",
                       42: "slt", 43: "sltu"}
        if fn in aritmeticas:
            if fn == 37 and rt == 0:
                return "move    %s, %s" % (R[rd], R[rs])
            return "%-7s %s, %s, %s" % (aritmeticas[fn], R[rd], R[rs], R[rt])
        return ".word   0x%08x" % w
    if op == 1:
        nombre = {0: "bltz", 1: "bgez", 16: "bltzal", 17: "bgezal"}.get(rt, "regimm?")
        return "%-7s %s, 0x%x" % (nombre, R[rs], base + pc + 4 + (simm << 2))
    if op in (2, 3):
        destino = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        texto = "%-7s 0x%x" % ("j" if op == 2 else "jal", base + destino)
        if destino in stubs:
            texto += "   <%s>" % stubs[destino]
        return texto
    if op in (4, 5):
        if op == 4 and rs == 0 and rt == 0:
            return "b       0x%x" % (base + pc + 4 + (simm << 2))
        return "%-7s %s, %s, 0x%x" % ("beq" if op == 4 else "bne", R[rs], R[rt], base + pc + 4 + (simm << 2))
    if op in (6, 7):
        return "%-7s %s, 0x%x" % ("blez" if op == 6 else "bgtz", R[rs], base + pc + 4 + (simm << 2))
    if op in (8, 9, 10, 11):
        nombre = {8: "addi", 9: "addiu", 10: "slti", 11: "sltiu"}[op]
        if op == 9 and rs == 0:
            return "li      %s, %d" % (R[rt], simm)
        return "%-7s %s, %s, %d" % (nombre, R[rt], R[rs], simm)
    if op in (12, 13, 14):
        nombre = {12: "andi", 13: "ori", 14: "xori"}[op]
        return "%-7s %s, %s, 0x%x" % (nombre, R[rt], R[rs], imm)
    if op == 15:
        return "lui     %s, 0x%x" % (R[rt], imm)
    if op == 16:
        if rs == 0:
            return "mfc0    %s, $%d" % (R[rt], rd)
        if rs == 4:
            return "mtc0    %s, $%d" % (R[rt], rd)
        if fn == 16:
            return "rfe"
        return "cop0    0x%x" % w
    memoria = {32: "lb", 33: "lh", 34: "lwl", 35: "lw", 36: "lbu", 37: "lhu", 38: "lwr", 40: "sb", 41: "sh", 42: "swl",
               43: "sw", 46: "swr"}
    if op in memoria:
        return "%-7s %s, %d(%s)" % (memoria[op], R[rt], simm, R[rs])
    return ".word   0x%08x" % w


def main():
    args = [a for a in sys.argv[1:]]
    base = 0
    if "--base" in args:
        i = args.index("--base")
        base = int(args[i + 1], 0)
        del args[i:i + 2]
    irx = Irx(args[0])
    if "--imports" in args:
        for direccion, nombre in sorted(irx.stubs.items()):
            print("0x%x  %s" % (base + direccion, nombre))
        return
    if "--cadenas" in args:
        for seccion in (".rodata", ".data"):
            if seccion not in irx.secciones:
                continue
            ini, tam = irx.secciones[seccion]
            p = ini
            while p < ini + tam:
                fin = p
                while fin < ini + tam and 0x20 <= irx.imagen[fin] < 0x7F:
                    fin += 1
                if fin - p >= 4 and fin < len(irx.imagen) and irx.imagen[fin] == 0:
                    print("0x%x  %s" % (base + p, irx.imagen[p:fin].decode("ascii")))
                    p = fin + 1
                else:
                    p += 1
        return
    texto_ini, texto_tam = irx.secciones[".text"]
    if "--llamadas" in args:
        objetivo = args[args.index("--llamadas") + 1]
        for direccion in range(texto_ini, texto_ini + texto_tam, 4):
            w = irx.palabra(direccion)
            if w >> 26 == 3:
                destino = (w & 0x3FFFFFF) << 2
                if destino in irx.stubs and irx.stubs[destino].startswith(objetivo):
                    print("0x%x  jal <%s>" % (base + direccion, irx.stubs[destino]))
        return
    ini = int(args[1], 0) - base if len(args) > 1 else texto_ini
    fin = int(args[2], 0) - base if len(args) > 2 else texto_ini + texto_tam
    for direccion in range(ini & ~3, fin, 4):
        w = irx.palabra(direccion)
        print("0x%05x: %08x  %s" % (base + direccion, w, desensamblar(w, direccion, irx.stubs, base)))


if __name__ == "__main__":
    main()
