#!/usr/bin/env python3
"""Desensambla funciones Lua 5.0 cargadas en un volcado de RAM del EE (logs/ee_ram.bin). Solo lectura.

El front-end de F1 06 es Lua 5.0 (VM en 0x29xxxx-0x2axxxx). Los scripts vienen comprimidos en los WAD, pero en RAM
estan ya cargados como objetos Proto. Este script busca las cadenas (TString), las constantes (TObject) que las
usan, los Proto que contienen esas constantes, y los desensambla.

Uso:
  python -I scripts/lua50_ram.py logs/ee_ram.bin CADENA [CADENA...]   funciones que usan esas cadenas
  python -I scripts/lua50_ram.py logs/ee_ram.bin --proto 0xDIR         desensambla un Proto concreto
  python -I scripts/lua50_ram.py logs/ee_ram.bin --campo NOMBRE        funciones guardadas con esa clave en una
                                                                       tabla (globales, metodos de clases...)
  python -I scripts/lua50_ram.py --archivo TROZO [--crudo]             un trozo volcado con F1_LUA_VOLCAR (bytecode):
                                                                       pseudo-Lua legible (o el desensamblado crudo)

Trozos volcados (F1_LUA_VOLCAR): cabecera de 14 bytes ESC "Lua" 0x51, endianidad 1, tamanos de int, size_t,
Instruction y lua_Number (4 cada uno) y el numero de prueba 31415926 en float (sin los SIZE_OP/A/B/C de la 5.0); despues
la funcion como en luaU_dump de la 5.0: fuente, lineDefined, nups, numparams, is_vararg, maxstacksize, lineas, locales,
upvalues, constantes (tipo 0 nil, 1 booleano, 3 numero float, 4 cadena con su tamano + 1 y el 0), hijas y codigo. Los
trozos del juego van sin depuracion (fuente "@X", sin lineas ni nombres de locales).

Estructuras (32 bits, lua_Number = float):
  TString: next(4) tt,marked,reserved(4) hash(4) len(4) puntero a los caracteres(4) y los caracteres (en esta
           version de Lua hay ese puntero extra; el texto suele ir justo detras, en +20).
  TObject: tt(4) valor(4); tt 0 nil, 1 booleano, 2 userdata ligero, 3 numero, 4 cadena, 5 tabla, 6 funcion.
  Proto: next(4) tt,marked(4) k code p lineinfo locvars upvalues source sizeupvalues sizek sizecode
         sizelineinfo sizep sizelocvars lineDefined gclist nups numparams is_vararg maxstacksize.
"""
import struct
import sys

# Los 36 codigos de la VM del juego (tabla de saltos de luaV_execute en 0x4594D0): los de la 5.0 con FORPREP en el 29
# (como en la 5.1-work; CALL/TAILCALL y SETLIST/SETLISTO comparten manejador, TFORPREP sigue en el de JMP).
OPS = ["MOVE", "LOADK", "LOADBOOL", "LOADNIL", "GETUPVAL", "GETGLOBAL", "GETTABLE", "SETGLOBAL", "SETUPVAL",
       "SETTABLE", "NEWTABLE", "SELF", "ADD", "SUB", "MUL", "DIV", "POW", "UNM", "NOT", "CONCAT", "JMP", "EQ",
       "LT", "LE", "TEST", "CALL", "TAILCALL", "RETURN", "FORLOOP", "FORPREP", "TFORLOOP", "TFORPREP", "SETLIST",
       "SETLISTO", "CLOSE", "CLOSURE"]
MAXSTACK = 250
RAM_MASK = 0x1FFFFFF


class Ram:
    def __init__(self, ruta):
        self.d = open(ruta, "rb").read()

    def u32(self, dir_):
        dir_ &= RAM_MASK
        return struct.unpack_from("<I", self.d, dir_)[0]

    def f32(self, dir_):
        return struct.unpack_from("<f", self.d, dir_ & RAM_MASK)[0]

    def tstring(self, dir_):
        """Texto de un TString (o None si no lo parece)."""
        if not dir_ or dir_ & 3 or (dir_ & RAM_MASK) + 20 > len(self.d):
            return None
        tt = self.d[(dir_ + 4) & RAM_MASK]
        longitud = self.u32(dir_ + 12)
        if tt != 4 or longitud > 4096:
            return None
        ini = self.u32(dir_ + 16) & RAM_MASK
        if ini + longitud > len(self.d):
            return None
        return self.d[ini:ini + longitud].decode("latin-1")


def constante(ram, k, i):
    tt = ram.u32(k + 8 * i)
    v = ram.u32(k + 8 * i + 4)
    if tt == 4:
        s = ram.tstring(v)
        return repr(s) if s is not None else f"<cadena? 0x{v:x}>"
    if tt == 3:
        return f"{ram.f32(k + 8 * i + 4):g}"
    if tt == 1:
        return "true" if v else "false"
    if tt == 0:
        return "nil"
    return f"<tt={tt} 0x{v:x}>"


def leer_proto(ram, p):
    campos = struct.unpack_from("<IIIIIIIIIIIIIIIIIIBBBB", ram.d, p & RAM_MASK)
    return {
        "k": campos[2], "code": campos[3], "p": campos[4], "upvalues": campos[7], "source": campos[8],
        "sizeupvalues": campos[9], "sizek": campos[10], "sizecode": campos[11], "sizep": campos[13],
        "lineDefined": campos[15], "numparams": campos[19],
    }


def parece_proto(ram, p):
    if p & 3 or (p & RAM_MASK) + 72 > len(ram.d) or ram.d[(p + 4) & RAM_MASK] != 9:
        return False
    pr = leer_proto(ram, p)
    return 0 < pr["sizecode"] < 100000 and pr["sizek"] < 100000 and pr["code"] and pr["code"] & 3 == 0


def rk(ram, pr, x):
    if x >= MAXSTACK:
        return constante(ram, pr["k"], x - MAXSTACK)
    return f"R{x}"


def desensamblar(ram, p, nivel=0):
    pr = leer_proto(ram, p)
    fuente = ram.tstring(pr["source"]) or "?"
    sangria = "  " * nivel
    print(f"{sangria}== Proto 0x{p:x} fuente={fuente} linea={pr['lineDefined']} params={pr['numparams']} "
          f"instrucciones={pr['sizecode']} constantes={pr['sizek']} hijas={pr['sizep']}")
    for pc in range(pr["sizecode"]):
        ins = ram.u32(pr["code"] + 4 * pc)
        op = ins & 0x3F
        c = (ins >> 6) & 0x1FF
        b = (ins >> 15) & 0x1FF
        a = (ins >> 24) & 0xFF
        bx = (ins >> 6) & 0x3FFFF
        sbx = bx - 131071
        nombre = OPS[op] if op < len(OPS) else f"OP{op}"
        if nombre in ("LOADK", "GETGLOBAL", "SETGLOBAL"):
            texto = f"R{a} {constante(ram, pr['k'], bx)}"
        elif nombre in ("JMP",):
            texto = f"-> {pc + 1 + sbx}"
        elif nombre in ("FORLOOP", "FORPREP", "TFORPREP"):
            texto = f"R{a} -> {pc + 1 + sbx}"
        elif nombre in ("GETTABLE", "SELF"):
            texto = f"R{a} R{b} [{rk(ram, pr, c)}]"
        elif nombre == "SETTABLE":
            texto = f"R{a}[{rk(ram, pr, b)}] = {rk(ram, pr, c)}"
        elif nombre in ("ADD", "SUB", "MUL", "DIV", "POW", "EQ", "LT", "LE"):
            texto = f"{a} {rk(ram, pr, b)} {rk(ram, pr, c)}"
        elif nombre == "CLOSURE":
            texto = f"R{a} hija {bx}"
        else:
            texto = f"{a} {b} {c}"
        print(f"{sangria}  {pc:4d} {nombre:10s} {texto}")
    for i in range(pr["sizep"]):
        hija = ram.u32(pr["p"] + 4 * i)
        if parece_proto(ram, hija):
            desensamblar(ram, hija, nivel + 1)


def buscar(ram, cadenas):
    d = ram.d
    tstrings = {}
    for cadena in cadenas:
        objetivo = cadena.encode("latin-1") + b"\0"
        pos = 0
        while True:
            pos = d.find(objetivo, pos)
            if pos < 0:
                break
            ts = pos - 20
            if ts >= 0 and ram.tstring(ts) == cadena:
                tstrings[ts] = cadena
            pos += 1
    print("TString:", ", ".join(f"{s}=0x{t:x}" for t, s in tstrings.items()))
    # TObject {4, tstring}
    usos = []
    for ts in tstrings:
        patron = struct.pack("<II", 4, ts)
        pos = 0
        while True:
            pos = d.find(patron, pos)
            if pos < 0:
                break
            if pos % 4 == 0:
                usos.append(pos)
            pos += 4
    # Proto cuyo k contiene alguno de esos TObject
    protos = set()
    for p in range(0, len(d) - 72, 4):
        if d[p + 4] != 9:
            continue
        if not parece_proto(ram, p):
            continue
        pr = leer_proto(ram, p)
        k = pr["k"] & RAM_MASK
        for u in usos:
            if k <= u < k + 8 * pr["sizek"] and (u - k) % 8 == 0:
                protos.add(p)
    for p in sorted(protos):
        desensamblar(ram, p)


def buscar_campo(ram, nombre):
    """Funciones guardadas en una tabla con esa clave (globales o campos): Node = clave(TObject) valor(TObject) next."""
    d = ram.d
    objetivo = nombre.encode("latin-1") + b"\0"
    pos = 0
    tstrings = []
    while True:
        pos = d.find(objetivo, pos)
        if pos < 0:
            break
        if pos >= 20 and ram.tstring(pos - 20) == nombre:
            tstrings.append(pos - 20)
        pos += 1
    for ts in tstrings:
        patron = struct.pack("<II", 4, ts)
        pos = 0
        while True:
            pos = d.find(patron, pos)
            if pos < 0:
                break
            if pos % 4 == 0 and pos + 16 <= len(d) and ram.u32(pos + 8) == 6:
                cierre = ram.u32(pos + 12)
                es_c = d[(cierre + 6) & RAM_MASK]
                if es_c:
                    print(f"{nombre}: funcion C en el nodo 0x{pos:x} (cierre 0x{cierre:x}, "
                          f"funcion 0x{ram.u32(cierre + 12):x})")
                else:
                    proto = ram.u32(cierre + 12)
                    print(f"{nombre}: nodo 0x{pos:x} cierre 0x{cierre:x} proto 0x{proto:x}")
                    if parece_proto(ram, proto):
                        desensamblar(ram, proto)
            pos += 4


# ---- Trozos volcados (bytecode en archivo) ----

class ProtoArchivo:
    def __init__(self):
        self.fuente = None
        self.linea = 0
        self.nups = 0
        self.params = 0
        self.vararg = 0
        self.k = []
        self.hijas = []
        self.code = []


class LectorTrozo:
    def __init__(self, datos):
        self.d = datos
        self.pos = 0

    def leer(self, formato):
        valores = struct.unpack_from("<" + formato, self.d, self.pos)
        self.pos += struct.calcsize("<" + formato)
        return valores[0] if len(valores) == 1 else valores

    def cadena(self):
        n = self.leer("I")
        if n == 0:
            return None
        texto = self.d[self.pos:self.pos + n - 1].decode("latin-1")
        self.pos += n
        return texto

    def funcion(self):
        p = ProtoArchivo()
        p.fuente = self.cadena()
        p.linea = self.leer("i")
        p.nups, p.params, p.vararg, _maxstack = self.leer("BBBB")
        lineas = self.leer("I")
        self.pos += 4 * lineas
        for _ in range(self.leer("I")):                # locales: nombre, startpc, endpc
            self.cadena()
            self.pos += 8
        for _ in range(self.leer("I")):                # nombres de upvalues
            self.cadena()
        for _ in range(self.leer("I")):
            tipo = self.leer("B")
            if tipo == 3:
                p.k.append(self.leer("f"))
            elif tipo == 4:
                p.k.append(self.cadena())
            elif tipo == 1:
                p.k.append(bool(self.leer("B")))
            elif tipo == 0:
                p.k.append(None)
            else:
                raise ValueError(f"constante de tipo {tipo} en 0x{self.pos:x}")
        for _ in range(self.leer("I")):
            p.hijas.append(self.funcion())
        n = self.leer("I")
        p.code = list(struct.unpack_from(f"<{n}I", self.d, self.pos))
        self.pos += 4 * n
        return p


def leer_trozo(ruta):
    d = open(ruta, "rb").read()
    if d[:4] != b"\x1bLua":
        raise ValueError(f"{ruta}: no es bytecode (empieza por {d[:8]!r})")
    if d[4] != 0x51 or d[5] != 1 or d[6:10] != b"\x04\x04\x04\x04":
        raise ValueError(f"{ruta}: cabecera inesperada {d[:14].hex(' ')}")
    lector = LectorTrozo(d)
    lector.pos = 14
    p = lector.funcion()
    if lector.pos != len(d):
        print(f"-- aviso: {len(d) - lector.pos} bytes sin leer al final")
    return p


def texto_k(v):
    if v is None:
        return "nil"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return f"{v:g}"
    return repr(v)


def campos_ins(ins):
    return ins & 0x3F, (ins >> 24) & 0xFF, (ins >> 15) & 0x1FF, (ins >> 6) & 0x1FF, (ins >> 6) & 0x3FFFF


def crudo_archivo(p, ruta="0", nivel=0):
    sangria = "  " * nivel
    print(f"{sangria}== F{ruta} params={p.params} upvalues={p.nups} instrucciones={len(p.code)} "
          f"constantes={len(p.k)} hijas={len(p.hijas)}")

    def rk(x):
        return texto_k(p.k[x - MAXSTACK]) if x >= MAXSTACK else f"R{x}"

    for pc, ins in enumerate(p.code):
        op, a, b, c, bx = campos_ins(ins)
        nombre = OPS[op] if op < len(OPS) else f"OP{op}"
        if nombre in ("LOADK", "GETGLOBAL", "SETGLOBAL"):
            texto = f"R{a} {texto_k(p.k[bx])}"
        elif nombre == "JMP":
            texto = f"-> {pc + 1 + bx - 131071}"
        elif nombre in ("FORLOOP", "FORPREP", "TFORPREP"):
            texto = f"R{a} -> {pc + 1 + bx - 131071}"
        elif nombre in ("GETTABLE", "SELF"):
            texto = f"R{a} R{b} [{rk(c)}]"
        elif nombre == "SETTABLE":
            texto = f"R{a}[{rk(b)}] = {rk(c)}"
        elif nombre in ("ADD", "SUB", "MUL", "DIV", "POW", "EQ", "LT", "LE"):
            texto = f"{a} {rk(b)} {rk(c)}"
        elif nombre == "CLOSURE":
            texto = f"R{a} F{ruta}.{bx}"
        else:
            texto = f"{a} {b} {c}"
        print(f"{sangria}  {pc:4d} {nombre:10s} {texto}")
    for i, hija in enumerate(p.hijas):
        crudo_archivo(hija, f"{ruta}.{i}", nivel + 1)


BINARIOS = {"ADD": "+", "SUB": "-", "MUL": "*", "DIV": "/", "POW": "^"}
COMPARA = {"EQ": ("==", "~="), "LT": ("<", ">="), "LE": ("<=", ">")}


def es_nombre(s):
    return isinstance(s, str) and s.isidentifier()


def pseudo_archivo(p, ruta="0", nivel=0):
    """Pseudo-Lua: las expresiones se siguen por registro y se escriben las sentencias (asignaciones a tablas y
    globales, llamadas, saltos). Sin nombres de locales (trozos sin depuracion): pN son los parametros, rN los
    registros. Es una ayuda de lectura, no un descompilador: los saltos salen como goto a la instruccion."""
    sangria = "  " * nivel
    params = ", ".join([f"p{i}" for i in range(p.params)] + (["..."] if p.vararg else []))
    print(f"{sangria}function F{ruta}({params})  -- {len(p.code)} instrucciones, {p.nups} upvalues")
    reg = {i: f"p{i}" for i in range(p.params)}
    destinos = set()
    for pc, ins in enumerate(p.code):
        op, a, b, c, bx = campos_ins(ins)
        if OPS[op] in ("JMP", "FORLOOP", "FORPREP", "TFORPREP"):
            destinos.add(pc + 1 + bx - 131071)
        elif OPS[op] in ("EQ", "LT", "LE", "TEST") and pc + 1 < len(p.code):
            destinos.add(pc + 2)

    def r(x):
        return reg.get(x, f"r{x}")

    def rk(x):
        return texto_k(p.k[x - MAXSTACK]) if x >= MAXSTACK else r(x)

    def indice(base, x):
        if x >= MAXSTACK and es_nombre(p.k[x - MAXSTACK]):
            return f"{base}.{p.k[x - MAXSTACK]}"
        return f"{base}[{rk(x)}]"

    def decir(pc, texto):
        print(f"{sangria}  {pc:4d}  {texto}")

    def volcar_pendientes(pc):
        # Lo calculado en una rama que llega a una etiqueta o a un salto: se escribe (si no, se perderia).
        for x in sorted(pendientes):
            if reg.get(x, f"r{x}") != f"r{x}":
                decir(pc, f"r{x} = {reg[x]}")
        pendientes.clear()

    pendientes = set()
    tras_tailcall = False
    saltar = 0
    pc = 0
    while pc < len(p.code):
        if pc in destinos:
            volcar_pendientes(pc)
            print(f"{sangria}  ::{pc}::")
            reg = {i: f"p{i}" for i in range(p.params)}  # tras una etiqueta no se sabe que hay en cada registro
        if saltar:
            saltar -= 1
            pc += 1
            continue
        op, a, b, c, bx = campos_ins(p.code[pc])
        nombre = OPS[op] if op < len(OPS) else f"OP{op}"
        sbx = bx - 131071
        if nombre in ("MOVE", "LOADK", "LOADBOOL", "LOADNIL", "GETUPVAL", "GETGLOBAL", "GETTABLE", "UNM", "NOT",
                      "CONCAT", "CLOSURE") or nombre in BINARIOS:
            pendientes.add(a)
        if tras_tailcall and nombre == "RETURN":
            tras_tailcall = False
            pc += 1
            continue
        tras_tailcall = nombre == "TAILCALL"
        # Lo que se consume aqui ya no hace falta escribirlo al llegar a una etiqueta.
        if nombre in ("CALL", "TAILCALL", "RETURN", "SETLIST", "SETLISTO"):
            pendientes.difference_update({x for x in pendientes if x >= a})
        elif nombre in ("SETGLOBAL", "SETUPVAL"):
            pendientes.discard(a)
        elif nombre in ("SETTABLE", "EQ", "LT", "LE"):
            pendientes.difference_update({b, c})
        elif nombre == "TEST":
            pendientes.discard(b)
        if nombre == "MOVE":
            reg[a] = r(b)
        elif nombre == "LOADK":
            reg[a] = texto_k(p.k[bx])
        elif nombre == "LOADBOOL":
            reg[a] = "true" if b else "false"
            if c:
                decir(pc, f"r{a} = {reg[a]}; goto {pc + 2}")
                reg[a] = f"r{a}"
        elif nombre == "LOADNIL":
            for i in range(a, b + 1):
                reg[i] = "nil"
        elif nombre == "GETUPVAL":
            reg[a] = f"U{b}"
        elif nombre == "GETGLOBAL":
            reg[a] = p.k[bx] if es_nombre(p.k[bx]) else f"_G[{texto_k(p.k[bx])}]"
        elif nombre == "GETTABLE":
            reg[a] = indice(r(b), c)
        elif nombre == "SETGLOBAL":
            decir(pc, f"{p.k[bx]} = {r(a)}")
        elif nombre == "SETUPVAL":
            decir(pc, f"U{b} = {r(a)}")
        elif nombre == "SETTABLE":
            decir(pc, f"{indice(r(a), b)} = {rk(c)}")
        elif nombre == "NEWTABLE":
            decir(pc, f"r{a} = {{}}")
            reg[a] = f"r{a}"
            pendientes.discard(a)
        elif nombre == "SELF":
            reg[a + 1] = r(b)
            reg[a] = f"{r(b)}:{p.k[c - MAXSTACK]}" if c >= MAXSTACK and es_nombre(p.k[c - MAXSTACK]) \
                else f"{r(b)}:[{rk(c)}]"
        elif nombre in BINARIOS:
            reg[a] = f"({rk(b)} {BINARIOS[nombre]} {rk(c)})"
        elif nombre == "UNM":
            reg[a] = f"-{r(b)}"
        elif nombre == "NOT":
            reg[a] = f"not {r(b)}"
        elif nombre == "CONCAT":
            reg[a] = "(" + " .. ".join(r(i) for i in range(b, c + 1)) + ")"
        elif nombre == "JMP":
            volcar_pendientes(pc)
            decir(pc, f"goto {pc + 1 + sbx}")
        elif nombre in COMPARA:
            destino = pc + 2
            siguiente = p.code[pc + 1] if pc + 1 < len(p.code) else 0
            if OPS[siguiente & 0x3F] == "JMP":
                destino = pc + 2 + ((siguiente >> 6) & 0x3FFFF) - 131071
                saltar = 1
            simbolo = COMPARA[nombre][0 if a else 1]
            decir(pc, f"if {rk(b)} {simbolo} {rk(c)} then goto {destino}")
        elif nombre == "TEST":
            destino = pc + 2
            siguiente = p.code[pc + 1] if pc + 1 < len(p.code) else 0
            if OPS[siguiente & 0x3F] == "JMP":
                destino = pc + 2 + ((siguiente >> 6) & 0x3FFFF) - 131071
                saltar = 1
            copia = f"r{a} = {r(b)}; " if a != b else ""
            decir(pc, f"if {'' if c else 'not '}{r(b)} then {copia}goto {destino}")
        elif nombre in ("CALL", "TAILCALL"):
            funcion = r(a)
            if b == 0:
                args = [r(i) for i in range(a + 1, a + 4) if i in reg] + ["..."]
            else:
                args = [r(i) for i in range(a + 1, a + b)]
            if funcion.count(":") == 1 and args and ":" in funcion:
                args = args[1:]  # SELF ya puso el objeto como primer argumento
            llamada = f"{funcion}({', '.join(args)})"
            if nombre == "TAILCALL":
                decir(pc, f"return {llamada}")
            elif c == 1:
                decir(pc, llamada)
            else:
                resultados = [f"r{i}" for i in range(a, a + c - 1)] if c else [f"r{a}..."]
                decir(pc, f"{', '.join(resultados)} = {llamada}")
                for i in range(a, a + max(c - 1, 1)):
                    reg[i] = f"r{i}"
                    pendientes.discard(i)
        elif nombre == "RETURN":
            valores = [r(i) for i in range(a, a + b - 1)] if b else [f"{r(a)}..."]
            decir(pc, "return" + (" " + ", ".join(valores) if valores else ""))
        elif nombre == "FORLOOP":
            decir(pc, f"for r{a + 3} = r{a}, r{a + 1}, r{a + 2}: goto {pc + 1 + sbx}")
        elif nombre == "FORPREP":
            decir(pc, f"for r{a} = {r(a)}, {r(a + 1)}, {r(a + 2)}: goto {pc + 1 + sbx}")
        elif nombre == "TFORPREP":
            decir(pc, f"for (iterador r{a}): goto {pc + 1 + sbx}")
        elif nombre == "TFORLOOP":
            decir(pc, f"r{a + 2}.. = r{a}(r{a + 1}, r{a + 2}) ({c} valores); si nil, sale")
        elif nombre in ("SETLIST", "SETLISTO"):
            n = (bx % 32) + 1
            primero = bx - (bx % 32) + 1
            elementos = ", ".join(r(a + 1 + i) for i in range(n))
            decir(pc, f"r{a}[{primero}..] = {{{elementos}{', ...' if nombre == 'SETLISTO' else ''}}}")
        elif nombre == "CLOSURE":
            hija = p.hijas[bx]
            capturas = []
            for i in range(hija.nups):
                o, _, bb, _, _ = campos_ins(p.code[pc + 1 + i])
                capturas.append(r(bb) if OPS[o] == "MOVE" else f"U{bb}")
            reg[a] = f"F{ruta}.{bx}" + (f"[{', '.join(capturas)}]" if capturas else "")
            saltar = hija.nups
        elif nombre == "CLOSE":
            pass
        else:
            decir(pc, f"{nombre} {a} {b} {c}")
        pc += 1
    for i, hija in enumerate(p.hijas):
        print()
        pseudo_archivo(hija, f"{ruta}.{i}", nivel + 1)


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--archivo":
        proto = leer_trozo(sys.argv[2])
        if "--crudo" in sys.argv[3:]:
            crudo_archivo(proto)
        else:
            pseudo_archivo(proto)
        return 0
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    ram = Ram(sys.argv[1])
    if sys.argv[2] == "--proto":
        desensamblar(ram, int(sys.argv[3], 16))
    elif sys.argv[2] == "--campo":
        for nombre in sys.argv[3:]:
            buscar_campo(ram, nombre)
    else:
        buscar(ram, sys.argv[2:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
