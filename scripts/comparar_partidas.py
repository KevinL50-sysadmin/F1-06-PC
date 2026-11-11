"""Compara la velocidad de varias partidas sin A/B (p. ej. dos ejecutables distintos, en partidas alternas).

Uso: python -I scripts/comparar_partidas.py [--desde 90] [--hasta 410] logs/medida-a-1.out.log logs/medida-b-1.out.log ...

Para cada log: media de paquetes/s de [f1-06:vif1fps] (geometrica) y de ghz, cpuEE y esperaGS en las ventanas entre
--desde y --hasta segundos tras graph.outcome("game"). Al final, agrupando por el nombre sin el ultimo "-<n>" (p. ej.
exe-base-1 y exe-base-2 -> exe-base), la media geometrica de cada grupo y su cociente frente al primero.
Ojo: entre partidas la carga del PC cambia; alternar los ejecutables y repetir.
"""
import math
import os
import re
import sys


def media_geo(v):
    return math.exp(sum(math.log(x) for x in v) / len(v)) if v else float('nan')


def main():
    args = sys.argv[1:]
    desde, hasta = 90.0, 410.0
    if '--desde' in args:
        i = args.index('--desde')
        desde = float(args[i + 1])
        del args[i:i + 2]
    if '--hasta' in args:
        i = args.index('--hasta')
        hasta = float(args[i + 1])
        del args[i:i + 2]
    if not args:
        print(__doc__)
        return 1
    grupos = {}
    for ruta in args:
        texto = open(ruta, encoding='utf-8', errors='replace').read()
        m = re.search(r'\[f1-06:lua\] t=([0-9.]+) graph\.outcome\("game"\)', texto)
        if not m:
            print('%s: sin game' % ruta)
            continue
        t_game = float(m.group(1))
        paquetes, ghz, cpu, espera = [], [], [], []
        for m in re.finditer(r'\[f1-06:vif1fps\] t=([0-9.]+) paquetes/s=([0-9.]+)([^\n]*)', texto):
            t = float(m.group(1)) - t_game
            if t < desde or t > hasta:
                continue
            paquetes.append(float(m.group(2)))
            resto = m.group(3)
            for lista, campo in ((ghz, 'ghz'), (cpu, 'cpuEE'), (espera, 'esperaGS')):
                c = re.search(r'\b%s=([0-9.]+)' % campo, resto)
                if c:
                    lista.append(float(c.group(1)))
        nombre = os.path.basename(ruta).replace('medida-', '').replace('.out.log', '')
        media = media_geo(paquetes)
        print('%-24s %3d ventanas  paquetes/s=%6.2f  ghz=%.2f  cpuEE=%.1f  esperaGS=%.1f' %
              (nombre, len(paquetes), media, sum(ghz) / max(len(ghz), 1), sum(cpu) / max(len(cpu), 1),
               sum(espera) / max(len(espera), 1)))
        grupo = re.sub(r'-[0-9]+$', '', nombre)
        grupos.setdefault(grupo, []).append(media)
    if len(grupos) > 1:
        print()
        primero = None
        for grupo, medias in grupos.items():
            g = media_geo(medias)
            if primero is None:
                primero = g
            print('%-24s %d partidas  paquetes/s=%6.2f  frente al primero: %+.1f %%' %
                  (grupo, len(medias), g, 100.0 * (g / primero - 1.0)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
