"""Reparte las muestras sin simbolo de un perfil (codigo del JIT de la VU) por bloques y pares de la VU.

Uso: python -I scripts/jit_mapa.py <mapa PS2X_JIT_MAPA> <crudo PERFIL_CRUDO> [maximo=40]

El mapa (parche 0078) tiene una linea "B <dir> <bytes> <memoria micro> <pc>" por bloque compilado y, tras ella, una
"P <desplazamiento> <pc> <inferior> <superior>" por par (donde empieza su codigo, tras las confirmaciones que vencen
antes de el). El crudo (perfil.exe con PERFIL_CRUDO=<archivo>) tiene "<dir hex> <muestras>" por direccion sin simbolo.
Cada muestra va al par cuyo codigo la contiene (las del prologo, al primero; las del final del bloque, al ultimo).
Saca: % del total de muestras por unidad (VU0: memoria de 4 KB; VU1: 16 KB), por instruccion inferior y superior
(nombre), por par (pc) y por bloque.
"""
import bisect
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vu_dis  # noqa: E402


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    maximo = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    bloques = []  # (inicio, fin, unidad, pc, [(desp, pc, inf, sup)])
    with open(sys.argv[1], encoding='utf-8', errors='replace') as f:
        actual = None
        for linea in f:
            partes = linea.split()
            if not partes:
                continue
            if partes[0] == 'B':
                inicio = int(partes[1], 16)
                actual = (inicio, inicio + int(partes[2]), 'VU0' if int(partes[3]) <= 4096 else 'VU1', int(partes[4]), [])
                bloques.append(actual)
            elif partes[0] == 'P' and actual is not None:
                actual[4].append((int(partes[1]), int(partes[2]), int(partes[3], 16), int(partes[4], 16)))
    bloques.sort()
    inicios = [b[0] for b in bloques]
    total = 0
    muestras = []
    with open(sys.argv[2], encoding='utf-8', errors='replace') as f:
        for linea in f:
            if linea.startswith('#'):
                total = int(linea.split()[1])
                continue
            partes = linea.split()
            if len(partes) == 2:
                muestras.append((int(partes[0], 16), int(partes[1])))
    sin_bloque = 0
    por_unidad, por_inf, por_sup, por_par, por_bloque = {}, {}, {}, {}, {}
    for dir_, n in muestras:
        i = bisect.bisect_right(inicios, dir_) - 1
        if i < 0 or dir_ >= bloques[i][1]:
            sin_bloque += n
            continue
        inicio, _, unidad, pc_bloque, pares = bloques[i]
        desp = dir_ - inicio
        j = max(0, bisect.bisect_right([p[0] for p in pares], desp) - 1)
        _, pc, inf, sup = pares[j] if pares else (0, pc_bloque, 0, 0)
        por_unidad[unidad] = por_unidad.get(unidad, 0) + n
        nombre_inf = vu_dis.lower(inf, pc).split()[0] if pares else '?'
        nombre_sup = vu_dis.upper(sup).split()[0] if pares else '?'
        por_inf[(unidad, nombre_inf)] = por_inf.get((unidad, nombre_inf), 0) + n
        por_sup[(unidad, nombre_sup)] = por_sup.get((unidad, nombre_sup), 0) + n
        clave = (unidad, pc, vu_dis.lower(inf, pc), vu_dis.upper(sup))
        por_par[clave] = por_par.get(clave, 0) + n
        por_bloque[(unidad, pc_bloque)] = por_bloque.get((unidad, pc_bloque), 0) + n
    total = total or sum(n for _, n in muestras)
    pct = lambda n: 100.0 * n / total
    print('%d muestras en total; sin simbolo %d (%.2f %%), fuera de los bloques del mapa %d (%.2f %%)' %
          (total, sum(n for _, n in muestras), pct(sum(n for _, n in muestras)), sin_bloque, pct(sin_bloque)))
    for titulo, tabla in (('Por unidad', por_unidad), ('Por instruccion inferior', por_inf),
                          ('Por instruccion superior', por_sup), ('Por bloque (unidad, pc)', por_bloque),
                          ('Por par (unidad, pc)', por_par)):
        print('\n=== %s' % titulo)
        for clave, n in sorted(tabla.items(), key=lambda kv: -kv[1])[:maximo]:
            texto = clave if isinstance(clave, str) else ' '.join(('0x%04x' % c) if isinstance(c, int) else str(c) for c in clave)
            print('%6.2f %%  %5d  %s' % (pct(n), n, texto))
    return 0


if __name__ == '__main__':
    sys.exit(main())
