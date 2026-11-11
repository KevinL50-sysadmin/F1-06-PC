"""Desensambla el codigo x64 que el JIT de la VU genero para un bloque (diagnostico, parche 0083).

Uso: python -I scripts/jit_desensamblar.py <mapa> <pc del bloque, p. ej. 0x2a18> [unidad VU1|VU0] [variante=0]

El mapa sale con PS2X_JIT_MAPA=<archivo> y PS2X_JIT_MAPA_BYTES=1 (lineas "X <bytes en hex>" tras cada bloque). Para
no lanzar el juego, basta el banco: PS2X_JIT_MAPA=... PS2X_JIT_MAPA_BYTES=1 scripts\\prueba_vu1.cmd <referencia> 1 con
volcados de carrera en logs\\vu1. Escribe los bytes como "db" en un .asm, lo ensambla con ml64 y lo desensambla con
dumpbin /disasm (los dos del MSVC portable, entorno.cmd), y marca donde empieza cada par (pc, inferior y superior con
scripts/vu_dis.py).
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vu_dis  # noqa: E402


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    mapa, pc = sys.argv[1], int(sys.argv[2], 0)
    unidad = sys.argv[3] if len(sys.argv) > 3 else 'VU1'
    variante = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    bloques = []
    actual = None
    for linea in open(mapa, encoding='utf-8', errors='replace'):
        partes = linea.split()
        if not partes:
            continue
        if partes[0] == 'B':
            actual = {'unidad': 'VU0' if int(partes[3]) <= 4096 else 'VU1', 'pc': int(partes[4]), 'pares': [],
                      'bytes': None}
            bloques.append(actual)
        elif partes[0] == 'P' and actual is not None:
            actual['pares'].append((int(partes[1]), int(partes[2]), int(partes[3], 16), int(partes[4], 16)))
        elif partes[0] == 'X' and actual is not None:
            actual['bytes'] = bytes.fromhex(partes[1])
    elegidos = [b for b in bloques if b['unidad'] == unidad and b['pc'] == pc and b['bytes']]
    if not elegidos:
        print('No hay ese bloque con bytes en el mapa')
        return 1
    print('%d variantes; la %d' % (len(elegidos), variante))
    b = elegidos[min(variante, len(elegidos) - 1)]
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with tempfile.TemporaryDirectory() as tmp:
        asm = os.path.join(tmp, 'b.asm')
        with open(asm, 'w') as f:
            f.write('_TEXT SEGMENT\nbloque PROC\n')
            datos = b['bytes']
            for i in range(0, len(datos), 16):
                f.write('db ' + ','.join('0%02xh' % x for x in datos[i:i + 16]) + '\n')
            f.write('bloque ENDP\n_TEXT ENDS\nEND\n')
        orden = 'call "%s" >nul && ml64 /nologo /c /Fo"%s" "%s" >nul && dumpbin /nologo /disasm "%s"' % (
            os.path.join(raiz, 'entorno.cmd'), os.path.join(tmp, 'b.obj'), asm, os.path.join(tmp, 'b.obj'))
        r = subprocess.run('cmd /c "' + orden + '"', capture_output=True, text=True, shell=False)
        salida = r.stdout
        if 'bloque' not in salida:
            print(r.stdout, r.stderr)
    inicios = {p[0]: p for p in b['pares']}
    for linea in salida.splitlines():
        partes = linea.strip().split(':', 1)
        if len(partes) == 2:
            try:
                desp = int(partes[0], 16)
            except ValueError:
                desp = None
            if desp is not None and desp in inicios:
                _, ppc, inf, sup = inicios[desp]
                print('; ---- par 0x%04x: %s | %s' % (ppc, vu_dis.lower(inf, ppc), vu_dis.upper(sup)))
        print(linea)
    return 0


if __name__ == '__main__':
    sys.exit(main())
