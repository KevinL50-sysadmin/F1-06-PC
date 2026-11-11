"""A/B (F1_AB) en carrera por coste del hilo del EE: millones de ciclos por fotograma en cada ventana.

Uso: python -I scripts/ab_ciclos.py logs/medida-X.out.log [--desde 20] [--hasta 400]
     (el .out.log del juego: el .txt de f6-t6-medir.ps1 no lleva cpuEE ni mciclosFot)

ab_resumen.py compara paquetes/s, que con el PC libre se queda en 50 (el juego espera al VBLANK) y entonces no ve
nada. Aqui la cifra es lo que gasta el hilo del EE por fotograma, tope o no: mciclosFot de cada ventana
[f1-06:vif1fps] (QueryThreadCycleTime del hilo / paquetes, desde la 1.4 T2; en este PC cuenta a ritmo fijo, ~2,1 GHz,
asi que es tiempo de CPU preciso y no ciclos del nucleo: sirve si la frecuencia no cambia entre ventanas vecinas) o, en
logs anteriores, la estimacion cpuEE x ghz x 1000 / paquetes/s (ruidosa: ghz es una frecuencia muestreada). Las dos
cifras no son comparables entre si (la segunda sale ~2,3 veces mayor). Solo cuentan las ventanas que empiezan
tras graph.outcome("game") + --desde segundos (como ab_resumen.py) y acaban antes de game + --hasta (p. ej.
el -PerfilTras de f6-t6-medir.ps1: el perfilador infla las ventanas). Cada ventana del modo B se compara con la media de
sus dos vecinas del modo 0 (la escena cambia con el tiempo) y se da la media geometrica de los cocientes y su error
tipico. Menos de 1 = el modo B gasta menos que el modo 0.
"""
import math
import re
import sys

LINEA = re.compile(r"\[f1-06:vif1fps\] t=([\d.]+) paquetes/s=([\d.]+) modo=(\d+)[^\n]*?cpuEE=([\d.]+)%[^\n]*?ghz=([\d.]+)"
                   r"(?:[^\n]*?mciclosFot=([\d.]+))?")
GAME = re.compile(r'\[f1-06:lua\] t=([\d.]+) graph\.outcome\("game"\)')


def main():
    args = sys.argv[1:]
    desde, hasta = 20.0, math.inf
    if "--desde" in args:
        i = args.index("--desde")
        desde = float(args[i + 1])
        del args[i:i + 2]
    if "--hasta" in args:
        i = args.index("--hasta")
        hasta = float(args[i + 1])
        del args[i:i + 2]
    if not args:
        print(__doc__)
        return 2
    texto = open(args[0], encoding="utf-8", errors="replace").read()
    m = GAME.search(texto)
    t_game = float(m.group(1)) if m else 0.0
    # Una ventana informa al final: empieza en el informe anterior.
    todas = []
    for m in LINEA.finditer(texto):
        t, fps, modo = float(m.group(1)), float(m.group(2)), int(m.group(3))
        if m.group(6) is not None:
            ciclos = float(m.group(6))
        else:
            ciclos = float(m.group(4)) * float(m.group(5)) * 10.0 / fps if fps > 0.0 else 0.0  # cpu en %
        todas.append((t, modo, ciclos, m.group(6) is not None))
    ventanas = [todas[i] for i in range(1, len(todas)) if todas[i - 1][0] >= t_game + desde and todas[i][0] <= t_game + hasta and todas[i][2] > 0.0]
    if not ventanas:
        print('Sin ventanas tras "game"')
        return 1
    directas = sum(1 for v in ventanas if v[3])
    print(f"game a los {t_game:.0f} s; {len(ventanas)} ventanas desde game+{desde:.0f} s; "
          f"{'mciclosFot' if directas == len(ventanas) else 'cpuEE x ghz' if directas == 0 else 'mezcla'}")
    logs = []
    for i in range(1, len(ventanas) - 1):
        t, modo, ciclos, _ = ventanas[i]
        a, b = ventanas[i - 1], ventanas[i + 1]
        if modo == 0 or a[1] != 0 or b[1] != 0:
            continue
        vecinas = (a[2] + b[2]) / 2.0
        logs.append(math.log(ciclos / vecinas))
        print(f"  t={t - t_game:6.0f} modo={modo}  {ciclos:6.1f} frente a {vecinas:6.1f} Mciclos/fot (modo 0 alrededor): "
              f"{ciclos / vecinas:.3f}")
    if len(logs) < 2:
        print("pocas ventanas")
        return 1
    media = sum(logs) / len(logs)
    desv = math.sqrt(sum((x - media) ** 2 for x in logs) / (len(logs) - 1))
    error = desv / math.sqrt(len(logs))
    print(f"modo B frente a modo 0: x{math.exp(media):.3f} ({len(logs)} ventanas; error tipico x{math.exp(error):.3f})")
    print(f"  => el modo 0 gasta {(1.0 / math.exp(media) - 1.0) * 100.0:+.1f} % de ciclos por fotograma respecto al modo B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
