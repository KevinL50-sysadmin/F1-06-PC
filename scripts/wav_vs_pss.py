# Compara el audio de un video .pss (PCM de su flujo privado 1, cabecera SShd/SSbd) con un volcado WAV del SPU2
# (PS2X_SPU2_WAV, 48 kHz estereo): busca donde empieza el video en el WAV por la envolvente (RMS cada 50 ms) y da la
# correlacion de la envolvente por tramos, la deriva de tiempo (si el WAV va mas rapido o lento) y si L y R son iguales.
# Uso: python -I scripts/wav_vs_pss.py juego/DATA2006/MOVIES/PAL/INTRO.PSS logs/x.wav [desde_s=0] [hasta_s=fin]
import math
import struct
import sys


def pcm_del_pss(ruta):
    d = open(ruta, 'rb').read()
    datos = bytearray()
    cabecera = None
    i = 0
    while True:
        i = d.find(b'\x00\x00\x01\xbd', i)
        if i < 0 or i + 9 > len(d):
            break
        largo = struct.unpack('>H', d[i + 4:i + 6])[0]
        carga = d[i + 9 + d[i + 8]:i + 6 + largo][4:]  # fuera la cabecera PES y los 4 bytes del subflujo
        if cabecera is None and carga[:4] == b'SShd':
            tam = struct.unpack('<I', carga[4:8])[0]
            cabecera = struct.unpack('<6i', carga[8:8 + 24])
            carga = carga[8 + tam + 8:]  # SShd + SSbd
        datos += carga
        i += 6 + largo
    tipo, tasa, canales, intercalado = cabecera[0], cabecera[1], cabecera[2], cabecera[3]
    return tipo, tasa, canales, intercalado, datos


def envolvente(muestras, tasa, paso_s):
    n = max(1, int(tasa * paso_s))
    return [math.sqrt(sum(x * x for x in muestras[k:k + n]) / n) for k in range(0, len(muestras) - n + 1, n)]


def correlacion(a, b):
    n = min(len(a), len(b))
    if n < 4:
        return 0.0
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    da = math.sqrt(sum((x - ma) ** 2 for x in a))
    db = math.sqrt(sum((y - mb) ** 2 for y in b))
    return num / (da * db) if da and db else 0.0


def main():
    tipo, tasa, canales, intercalado, pcm = pcm_del_pss(sys.argv[1])
    print(f"pss: tipo={tipo} tasa={tasa} canales={canales} intercalado={intercalado:#x} bytes={len(pcm)}"
          f" ({len(pcm) / 2 / canales / tasa:.1f} s)")
    if canales != 1:
        print("solo se compara audio mono")
        return
    ref = struct.unpack(f'<{len(pcm) // 2}h', pcm[:len(pcm) // 2 * 2])
    paso = 0.05
    env_ref = envolvente(ref, tasa, paso)

    desde = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
    hasta = float(sys.argv[4]) if len(sys.argv) > 4 else 1e9
    f = open(sys.argv[2], 'rb')
    f.seek(44 + int(desde * 48000) * 4)
    crudo = f.read(int(min(hasta - desde, 1e6) * 48000) * 4)
    est = struct.unpack(f'<{len(crudo) // 2}h', crudo[:len(crudo) // 4 * 4])
    izq, der = est[0::2], est[1::2]
    distintas = sum(1 for x, y in zip(izq, der) if x != y)
    print(f"wav: {len(izq) / 48000:.1f} s desde {desde:.1f} s; muestras con L != R: {distintas}")
    mono = [(x + y) / 2 for x, y in zip(izq, der)]
    env_wav = envolvente(mono, 48000, paso)

    # Inicio del video en el WAV: mejor correlacion de los primeros 20 s de la referencia.
    ventana = env_ref[:int(20 / paso)]
    mejor = max(range(0, max(1, len(env_wav) - len(ventana))), key=lambda k: correlacion(ventana, env_wav[k:k + len(ventana)]))
    print(f"el video empieza en el WAV a los {desde + mejor * paso:.2f} s (correlacion {correlacion(ventana, env_wav[mejor:mejor + len(ventana)]):.3f})")
    # Por tramos de 10 s: desplazamiento local (+-1 s) y correlacion.
    tramo = int(10 / paso)
    for t0 in range(0, len(env_ref) - tramo + 1, tramo):
        trozo = env_ref[t0:t0 + tramo]
        candidatos = range(max(0, mejor + t0 - 20), min(len(env_wav) - tramo, mejor + t0 + 20) + 1)
        if not candidatos:
            break
        k = max(candidatos, key=lambda c: correlacion(trozo, env_wav[c:c + tramo]))
        nivel = sum(env_wav[k:k + tramo]) / tramo / max(1e-9, sum(trozo) / tramo)
        print(f"  ref {t0 * paso:5.1f} s: desfase {(k - mejor - t0) * paso:+.2f} s correlacion {correlacion(trozo, env_wav[k:k + tramo]):.3f}"
              f" nivel wav/ref {nivel:.2f}")


main()
