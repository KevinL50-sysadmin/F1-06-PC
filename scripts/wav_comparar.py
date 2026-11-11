# Compara dos WAV de 16 bits estereo a 48 kHz del mismo sonido grabados en sitios distintos (p. ej. lo que entrego el
# anfitrion a su dispositivo, PS2X_SALIDA_WAV, y lo que entrego el invitado del juego en red, PS2X_INVITADO_WAV):
#  - los alinea por la envolvente (RMS cada 10 ms) y dice cuanto empieza despues el segundo;
#  - niveles por tramo de los dos y su cociente;
#  - huecos (>= 20 ms de silencio exacto entre sonido) de cada uno, en el tiempo del primero, y cuales del segundo no
#    tienen uno del primero cerca (cortes propios del segundo);
#  - el desfase fino (envolvente cada 1 ms) en puntos repartidos: si se mantiene, el segundo no se desvia del primero.
# Los volcados escriben la cabecera de 44 bytes a cero: se leen en crudo tras ella.
# Uso: python -I scripts/wav_comparar.py <primero.wav> <segundo.wav> [segundos_por_tramo=10] [desfase_max_s=40]
import array
import math
import sys

TASA = 48000


def leer_mono(ruta):
    with open(ruta, 'rb') as f:
        f.seek(44)
        datos = f.read()
    muestras = array.array('h')
    muestras.frombytes(datos[:len(datos) // 4 * 4])
    izq = muestras[0::2]
    der = muestras[1::2]
    return array.array('i', (a + b for a, b in zip(izq, der)))  # suma de canales (sin dividir)


def envolvente(mono, bloque):
    n = len(mono) // bloque
    out = [0.0] * n
    for i in range(n):
        s = 0
        for v in mono[i * bloque:(i + 1) * bloque]:
            s += v * v
        out[i] = math.sqrt(s / bloque)
    return out


def correlacion(a, b, desfase, desde, hasta):
    # a[i] frente a b[i - desfase], i en [desde, hasta)
    num = 0.0
    na = 0.0
    nb = 0.0
    for i in range(desde, hasta):
        j = i - desfase
        if 0 <= j < len(b):
            x, y = a[i], b[j]
            num += x * y
            na += x * x
            nb += y * y
    return num / math.sqrt(na * nb) if na > 0 and nb > 0 else 0.0


def huecos(mono, minimo):
    # Tramos de >= minimo muestras a cero exacto con sonido antes y despues (RMS de 10 ms > 50).
    out = []
    n = len(mono)
    i = 0
    while i < n:
        if mono[i] != 0:
            i += 1
            continue
        j = i
        while j < n and mono[j] == 0:
            j += 1
        if j - i >= minimo and i > 480 and j + 480 < n:
            antes = math.sqrt(sum(v * v for v in mono[i - 480:i]) / 480)
            despues = math.sqrt(sum(v * v for v in mono[j:j + 480]) / 480)
            if antes > 50 and despues > 50:
                out.append((i / TASA, (j - i) / TASA))
        i = j
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__ if __doc__ else 'uso: wav_comparar.py <primero.wav> <segundo.wav> [tramo] [desfase_max]')
        return 1
    tramo = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
    maximo = float(sys.argv[4]) if len(sys.argv) > 4 else 40.0
    a = leer_mono(sys.argv[1])
    b = leer_mono(sys.argv[2])
    print('%s: %.1f s; %s: %.1f s' % (sys.argv[1], len(a) / TASA, sys.argv[2], len(b) / TASA))

    # Alineacion gruesa (100 ms) y luego a 10 ms.
    ga, gb = envolvente(a, 4800), envolvente(b, 4800)
    mejor, mejor_c = 0, -1.0
    for d in range(int(maximo * 10) + 1):
        c = correlacion(ga, gb, d, 0, len(ga))
        if c > mejor_c:
            mejor, mejor_c = d, c
    ea, eb = envolvente(a, 480), envolvente(b, 480)
    fino, fino_c = mejor * 10, -1.0
    for d in range(mejor * 10 - 15, mejor * 10 + 16):
        c = correlacion(ea, eb, d, 0, len(ea))
        if c > fino_c:
            fino, fino_c = d, c
    desfase = fino * 480  # muestras: b[i - desfase] es a[i]
    print('alineacion: el segundo va %.2f s por delante en su archivo (correlacion de envolventes %.3f)' %
          (desfase / TASA, fino_c))

    print('  tramo (tiempo del primero)   rms1    rms2   cociente')
    t = 0.0
    while t < len(a) / TASA:
        i0, i1 = int(t * TASA), int(min(len(a) / TASA, t + tramo) * TASA)
        s1 = s2 = 0.0
        n = 0
        for i in range(i0, i1, 4):
            j = i - desfase
            if 0 <= j < len(b):
                s1 += a[i] * a[i]
                s2 += b[j] * b[j]
                n += 1
        if n:
            r1, r2 = math.sqrt(s1 / n) / 2, math.sqrt(s2 / n) / 2
            print('  %7.1f-%7.1f s        %6.0f  %6.0f   %s' %
                  (t, t + tramo, r1, r2, ('%.3f' % (r2 / r1)) if r1 > 20 else '  -'))
        t += tramo

    ha = huecos(a, 960)
    hb = [(t + desfase / TASA, d) for t, d in huecos(b, 960)]
    propios = [h for h in hb if not any(abs(h[0] - x[0]) < 0.6 for x in ha)]
    print('huecos >= 20 ms: primero %d, segundo %d (%d sin uno del primero a menos de 0,6 s)' %
          (len(ha), len(hb), len(propios)))
    print('  primero: ' + ', '.join('%.1f s (%.0f ms)' % (t, d * 1000) for t, d in ha))
    print('  segundo: ' + ', '.join('%.1f s (%.0f ms)%s' % (t, d * 1000, ' *' if (t, d) in propios else '')
                                    for t, d in hb))

    # Desfase fino a 1 ms en ventanas de 2 s cada `tramo` segundos (donde hay sonido en los dos).
    ma, mb = envolvente(a, 48), envolvente(b, 48)
    base = desfase // 48
    print('desfase fino (ms, respecto a la alineacion):')
    linea = []
    t = 0.0
    while t + 2.0 < len(a) / TASA:
        i0 = int(t * 1000)
        i1 = i0 + 2000
        if max(ma[i0:i1] or [0]) > 200:
            mejor_d, mejor_cc = 0, -1.0
            for d in range(-60, 61):
                c = correlacion(ma, mb, base + d, i0, i1)
                if c > mejor_cc:
                    mejor_d, mejor_cc = d, c
            if mejor_cc > 0.8:
                linea.append('%.0f s: %+d' % (t, mejor_d))
        t += tramo
    print('  ' + ', '.join(linea))
    return 0


if __name__ == '__main__':
    sys.exit(main())
