"""Resumen de una medida A/B (F1_AB) en carrera: compara cada ventana con la anterior y la siguiente de otro modo.

Uso: python -I scripts/ab_resumen.py logs/medida-X.txt [--desde 0]
     (o el .out.log del juego)

La escena de la carrera cambia mucho con el tiempo (12-25 paquetes/s en la misma partida), asi que comparar medias de
cada modo engana. Aqui cada ventana del modo B se compara con la media de sus dos vecinas del modo 0 (que la rodean en
el tiempo), y se da la media geometrica de esos cocientes y su dispersion. Solo cuentan las ventanas tras
graph.outcome("game") + --desde segundos.
"""
import math
import re
import sys


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    ruta = args[0]
    desde = 0.0
    if '--desde' in args:
        desde = float(args[args.index('--desde') + 1])
    texto = open(ruta, encoding='utf-8', errors='replace').read()
    m = re.search(r'\[f1-06:lua\] t=([0-9.]+) graph\.outcome\("game"\)', texto)
    t_game = float(m.group(1)) if m else 0.0
    ventanas = []
    for m in re.finditer(r'\[f1-06:vif1fps\] t=([0-9.]+) paquetes/s=([0-9.]+) modo=([0-9]+)', texto):
        t, v, modo = float(m.group(1)), float(m.group(2)), int(m.group(3))
        ventanas.append((t, v, modo))
    # Una ventana informa al final: empieza en el informe anterior.
    # Una ventana mucho mas larga que las demas es una parada de todo el proceso (el perfilador suspende el hilo del EE;
    # la captura) y no del modo: se descarta con sus vecinas (--todas: no descartar nada).
    validas = []
    duraciones = []
    for i in range(1, len(ventanas)):
        inicio = ventanas[i - 1][0]
        if inicio >= t_game + desde:
            validas.append(ventanas[i])
            duraciones.append(ventanas[i][0] - inicio)
    if not validas:
        print('Sin ventanas tras "game"')
        return 1
    nominal = sorted(duraciones)[len(duraciones) // 2]
    larga = [d > 1.3 * nominal and '--todas' not in args for d in duraciones]
    print('game a los %.0f s; %d ventanas desde game+%.0f s; %d alargadas (paradas del proceso) descartadas' %
          (t_game, len(validas), desde, sum(larga)))
    cocientes = {}
    for i, (t, v, modo) in enumerate(validas):
        if modo == 0:
            continue
        if larga[i]:
            print('  t=%6.0f modo=%d descartada: %.1f s' % (t - t_game, modo, duraciones[i]))
            continue
        vecinas = [validas[j][1] for j in (i - 1, i + 1)
                   if 0 <= j < len(validas) and validas[j][2] == 0 and not larga[j]]
        if not vecinas:
            continue
        ref = sum(vecinas) / len(vecinas)
        cocientes.setdefault(modo, []).append(v / ref)
        print('  t=%6.0f modo=%d %6.2f frente a %6.2f (modo 0 alrededor): %.3f' % (t - t_game, modo, v, ref, v / ref))
    for modo, lista in sorted(cocientes.items()):
        logs = [math.log(c) for c in lista]
        media = sum(logs) / len(logs)
        desv = math.sqrt(sum((x - media) ** 2 for x in logs) / (len(logs) - 1)) if len(logs) > 1 else 0.0
        error = desv / math.sqrt(len(logs)) if len(logs) > 1 else 0.0
        print('modo %d frente a modo 0: %.3f (x%.3f; %d pares; error tipico x%.3f)' %
              (modo, math.exp(media), math.exp(media), len(lista), math.exp(error)))
        print('  => el modo 0 va %+.1f %% respecto al modo %d' % (100.0 * (math.exp(-media) - 1.0), modo))
    return 0


if __name__ == '__main__':
    sys.exit(main())
