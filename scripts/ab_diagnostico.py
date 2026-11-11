"""Diagnostico de una medida A/B (F1_AB) en carrera: para cada modo B, como cambian los campos de [f1-06:vif1fps]
(parche 0069: cpuEE, esperaGS, vu1, nsPar, ghz, ghzMax, aparcados, despertares) frente a la media de sus dos ventanas vecinas del
modo 0, como en ab_resumen.py.

Uso: python -I scripts/ab_diagnostico.py logs/medida-X.out.log [--desde 0] [--hasta segundos tras game]
(sin --hasta, hasta el final de la carrera: el primer graph.outcome("InstantRace1P") tras game)

paquetes y nsPar: cociente B/A en % (media geometrica); los porcentajes y aparcados: diferencia B-A en puntos; ghz:
cociente en %. Tambien la media de cada campo en el modo 0.
"""
import math
import re
import sys

CAMPOS = ['paquetes', 'cpuEE', 'esperaGS', 'vu1', 'nsPar', 'ghz', 'ghzMax', 'aparcados', 'despertares', 'esperaVu1']
COCIENTE = {'paquetes', 'nsPar', 'ghz', 'ghzMax'}


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    texto = open(args[0], encoding='utf-8', errors='replace').read()
    desde = float(args[args.index('--desde') + 1]) if '--desde' in args else 0.0
    m = re.search(r'\[f1-06:lua\] t=([0-9.]+) graph\.outcome\("game"\)', texto)
    t_game = float(m.group(1)) if m else 0.0
    hasta = float(args[args.index('--hasta') + 1]) + t_game if '--hasta' in args else float('inf')
    if '--hasta' not in args:
        for m in re.finditer(r'\[f1-06:lua\] t=([0-9.]+) graph\.outcome\("InstantRace1P"\)', texto):
            if float(m.group(1)) > t_game:
                hasta = float(m.group(1))
                break
    ventanas = []
    for linea in texto.splitlines():
        m = re.search(r'\[f1-06:vif1fps\] t=([0-9.]+) paquetes/s=([0-9.]+) modo=([0-9]+)(.*)', linea)
        if not m:
            continue
        campos = {'paquetes': float(m.group(2))}
        for nombre, valor in re.findall(r'(\w+)=([0-9.]+)%?', m.group(4)):
            campos[nombre] = float(valor)
        ventanas.append((float(m.group(1)), int(m.group(3)), campos))
    validas, duraciones = [], []
    for i in range(1, len(ventanas)):
        if ventanas[i - 1][0] >= t_game + desde and ventanas[i][0] <= hasta:
            validas.append(ventanas[i])
            duraciones.append(ventanas[i][0] - ventanas[i - 1][0])
    if not validas:
        print('Sin ventanas tras "game"')
        return 1
    nominal = sorted(duraciones)[len(duraciones) // 2]
    larga = [d > 1.3 * nominal for d in duraciones]
    cambios = {}
    for i, (t, modo, campos) in enumerate(validas):
        if modo == 0 or larga[i]:
            continue
        vecinas = [validas[j][2] for j in (i - 1, i + 1)
                   if 0 <= j < len(validas) and validas[j][1] == 0 and not larga[j]]
        if not vecinas:
            continue
        por_modo = cambios.setdefault(modo, {c: [] for c in CAMPOS})
        for c in CAMPOS:
            if c not in campos or any(c not in v for v in vecinas):
                continue
            ref = sum(v[c] for v in vecinas) / len(vecinas)
            if c in COCIENTE:
                if ref > 0 and campos[c] > 0:
                    por_modo[c].append(math.log(campos[c] / ref))
            else:
                por_modo[c].append(campos[c] - ref)
    ceros = [v[2] for i, v in enumerate(validas) if v[1] == 0 and not larga[i]]
    print('modo 0 (%d ventanas), medias: %s' % (len(ceros), ' '.join(
        '%s=%.2f' % (c, sum(v.get(c, 0.0) for v in ceros) / max(1, len(ceros))) for c in CAMPOS)))
    for modo in sorted(cambios):
        partes = []
        for c in CAMPOS:
            xs = cambios[modo][c]
            if not xs:
                continue
            media = sum(xs) / len(xs)
            dt = math.sqrt(sum((x - media) ** 2 for x in xs) / max(1, len(xs) - 1)) / math.sqrt(len(xs))
            if c in COCIENTE:
                partes.append('%s %+.1f%%±%.1f' % (c, 100 * (math.exp(media) - 1), 100 * dt))
            else:
                partes.append('%s %+.2f±%.2f' % (c, media, dt))
        print('modo %d (%d pares): %s' % (modo, len(cambios[modo]['paquetes']), ', '.join(partes)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
