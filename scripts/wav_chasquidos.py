# Busca chasquidos en un WAV de 16 bits estereo a 48 kHz (el volcado del SPU2, PS2X_SPU2_WAV, o lo que entrego el
# callback de audio, PS2X_SALIDA_WAV): saltos bruscos entre muestras, huecos rellenos de ceros en medio de la senal y
# muestras repetidas (la correccion de deriva repite una muestra). Resume por tramo y en total.
# Uso: python -I scripts/wav_chasquidos.py <wav> [segundos_por_tramo=5] [desde=0] [hasta=fin] [--lista N]
#      python -I scripts/wav_chasquidos.py --prueba   (comprueba el detector con senales sinteticas)
# Criterios (por canal; un suceso cuenta una vez aunque salga en los dos canales):
#  - salto: |x[n]-x[n-1]| > max(SALTO_MIN, SALTO_VECES * media de |x[n]-x[n-1]| en los 1.024 marcos de alrededor).
#    Un hueco o una muestra quitada en una onda grave da un salto muy por encima de la pendiente habitual.
#  - hueco: >= HUECO_MIN marcos seguidos a cero exacto en los dos canales con senal (rms > 100) justo antes y despues.
#  - repetida: un marco igual al anterior en los dos canales en medio de una pendiente (el canal izquierdo sube o baja
#    mas de REPETIDA_PEND antes y despues, en el mismo sentido): en una cresta las muestras iguales son normales.
import array
import math
import os
import sys

TASA = 48000
SALTO_MIN = 2500
SALTO_VECES = 10.0
HUECO_MIN = 8
REPETIDA_PEND = 40
VENTANA = 1024


def leer(ruta, desde, hasta):
    # La cabecera del volcado se escribe al cerrarse; si el juego se mato antes, se lee en crudo tras los 44 bytes.
    with open(ruta, 'rb') as f:
        f.seek(44 + int(desde * TASA) * 4)
        n = None if hasta is None else max(0, int((hasta - desde) * TASA))
        datos = f.read() if n is None else f.read(n * 4)
    muestras = array.array('h')
    muestras.frombytes(datos[:len(datos) // 4 * 4])
    if sys.byteorder != 'little':
        muestras.byteswap()
    return muestras[0::2], muestras[1::2]


def analizar(izq, der):
    n = len(izq)
    sucesos = []  # (marco, tipo, valor)
    # Pendiente media por bloques de VENTANA/2 (los dos canales), para la escala local.
    medio = VENTANA // 2
    bloques = (n + medio - 1) // medio
    pend = [0.0] * bloques
    for b in range(bloques):
        a, z = max(1, b * medio), min(n, (b + 1) * medio)
        if z <= a:
            continue
        s = 0
        for i in range(a, z):
            s += abs(izq[i] - izq[i - 1]) + abs(der[i] - der[i - 1])
        pend[b] = s / (2.0 * (z - a))

    def escala(i):
        b = i // medio
        vecinos = [pend[k] for k in (b - 1, b, b + 1) if 0 <= k < bloques]
        return sum(vecinos) / len(vecinos)

    ultimo_salto = -10
    i = 1
    while i < n:
        dl = abs(izq[i] - izq[i - 1])
        dr = abs(der[i] - der[i - 1])
        d = max(dl, dr)
        if d > SALTO_MIN and d > SALTO_VECES * escala(i):
            if i - ultimo_salto > 4:
                sucesos.append((i, 'salto', d))
            ultimo_salto = i
        if izq[i] == 0 and der[i] == 0 and (izq[i - 1] != 0 or der[i - 1] != 0):
            j = i
            while j < n and izq[j] == 0 and der[j] == 0:
                j += 1
            largo = j - i
            if largo >= HUECO_MIN and j < n:
                antes = rms(izq, der, max(0, i - 256), i)
                despues = rms(izq, der, j, min(n, j + 256))
                if antes > 100 and despues > 100:
                    sucesos.append((i, 'hueco', largo))
            i = j
            continue
        if (izq[i] == izq[i - 1] and der[i] == der[i - 1] and 2 <= i < n - 1 and
                min(abs(izq[i - 1] - izq[i - 2]), abs(izq[i + 1] - izq[i])) > REPETIDA_PEND and
                (izq[i - 1] - izq[i - 2]) * (izq[i + 1] - izq[i]) > 0):
            sucesos.append((i, 'repetida', 1))
        i += 1
    return sucesos


def rms(izq, der, a, z):
    if z <= a:
        return 0.0
    s = 0
    for i in range(a, z):
        s += izq[i] * izq[i] + der[i] * der[i]
    return math.sqrt(s / (2.0 * (z - a)))


def resumir(ruta, tramo, desde, hasta, lista):
    izq, der = leer(ruta, desde, hasta)
    n = len(izq)
    print(f"{ruta}: {(os.path.getsize(ruta) - 44) / 4 / TASA:.1f} s; analizado {desde:.1f}-{desde + n / TASA:.1f} s")
    sucesos = analizar(izq, der)
    paso = int(tramo * TASA)
    tipos = ('salto', 'hueco', 'repetida')
    print("  tramo            rms  saltos huecos repetidas")
    for a in range(0, n, paso):
        z = min(n, a + paso)
        cuenta = {t: sum(1 for s in sucesos if a <= s[0] < z and s[1] == t) for t in tipos}
        print(f"  {desde + a / TASA:6.1f}-{desde + z / TASA:6.1f} s {rms(izq, der, a, z):7.0f} {cuenta['salto']:7d} "
              f"{cuenta['hueco']:6d} {cuenta['repetida']:9d}")
    total = {t: sum(1 for s in sucesos if s[1] == t) for t in tipos}
    segundos = max(n / TASA, 1e-9)
    print(f"  TOTAL saltos={total['salto']} huecos={total['hueco']} repetidas={total['repetida']} "
          f"({total['salto'] / segundos:.2f} saltos/s, {total['hueco'] / segundos:.2f} huecos/s)")
    for s in sucesos[:lista]:
        print(f"    t={desde + s[0] / TASA:9.4f} s {s[1]} {s[2]}")
    return total


def prueba():
    # Seno de 220 Hz a 8.000 de amplitud: sin sucesos. Con un hueco de 64 ceros, una muestra quitada y una repetida.
    n = TASA
    onda = [int(8000 * math.sin(2 * math.pi * 220 * i / TASA)) for i in range(n)]
    limpio = analizar(array.array('h', onda), array.array('h', onda))
    assert limpio == [], limpio
    roto = list(onda)
    roto[10000:10064] = [0] * 64
    del roto[20000:20055]          # salto de fase: un cuarto de periodo quitado (como un descarte en bloque)
    sucesos = analizar(array.array('h', roto), array.array('h', roto))
    tipos = [s[1] for s in sucesos]
    assert tipos.count('hueco') == 1 and abs(sucesos[tipos.index('hueco')][0] - 10000) <= 1, sucesos
    assert any(t == 'salto' and abs(s - 19999) <= 2 for s, t, _ in sucesos), sucesos
    assert tipos.count('repetida') == 0, sucesos
    # Muestra repetida en un paso por cero (21.818 = 200 semiperiodos), donde la onda va mas deprisa.
    doble = list(onda)
    doble.insert(21818, doble[21817])
    tipos = [s[1] for s in analizar(array.array('h', doble), array.array('h', doble))]
    assert tipos == ['repetida'], tipos
    # Silencio entre notas (ceros con silencio alrededor): no es un hueco.
    quieto = [0] * 2000 + onda[:2000] + [0] * 2000
    assert all(s[1] != 'hueco' for s in analizar(array.array('h', quieto), array.array('h', quieto)))
    print("prueba: OK")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--prueba':
        prueba()
        return
    args = [a for a in sys.argv[1:]]
    lista = 0
    if '--lista' in args:
        k = args.index('--lista')
        lista = int(args[k + 1])
        del args[k:k + 2]
    ruta = args[0]
    tramo = float(args[1]) if len(args) > 1 else 5.0
    desde = float(args[2]) if len(args) > 2 else 0.0
    hasta = float(args[3]) if len(args) > 3 else None
    resumir(ruta, tramo, desde, hasta, lista)


if __name__ == '__main__':
    main()
