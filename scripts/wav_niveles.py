# Niveles de un WAV de 16 bits estereo (p. ej. el volcado del SPU2, PS2X_SPU2_WAV): RMS y pico por tramo, y la
# frecuencia dominante de cada tramo (para distinguir musica, motor o silencio sin escucharlo).
# Uso: python -I scripts/wav_niveles.py logs/spu2-a.wav [segundos_por_tramo=2] [desde=0] [hasta=fin]
import math
import struct
import sys


def main():
    ruta = sys.argv[1]
    tramo = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
    desde = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
    hasta = float(sys.argv[4]) if len(sys.argv) > 4 else 1e9
    # El volcado del SPU2 escribe la cabecera al cerrarse; si el juego se mato antes, se lee en crudo (48 kHz estereo).
    f = open(ruta, 'rb')
    f.seek(44 + int(desde * 48000) * 4)
    tasa = 48000
    import os
    print(f"{ruta}: {(os.path.getsize(ruta) - 44) / 4 / tasa:.1f} s")
    if True:
        t = desde
        n = int(tramo * tasa)
        while t < hasta:
            datos = f.read(n * 4)
            if len(datos) < 4 * 64:
                break
            muestras = struct.unpack(f"<{len(datos) // 2}h", datos)
            mono = [(muestras[i] + muestras[i + 1]) / 2 for i in range(0, len(muestras) - 1, 2)]
            rms = math.sqrt(sum(x * x for x in mono) / len(mono))
            pico = max(abs(x) for x in muestras)
            # Frecuencia dominante: DFT de 4096 muestras del centro del tramo, por bins de ~11,7 Hz.
            dominante = 0.0
            if rms > 30:
                trozo = mono[len(mono) // 2 - 2048: len(mono) // 2 + 2048]
                mejor = 0.0
                for k in range(2, 400, 1):
                    re = im = 0.0
                    ang = 2 * math.pi * k / len(trozo)
                    for i in range(0, len(trozo), 2):
                        re += trozo[i] * math.cos(ang * i)
                        im += trozo[i] * math.sin(ang * i)
                    p = re * re + im * im
                    if p > mejor:
                        mejor = p
                        dominante = k * tasa / len(trozo)
            print(f"{t:7.1f} s  rms={rms:7.0f}  pico={pico:6d}  dominante={dominante:6.0f} Hz")
            t += tramo


if __name__ == '__main__':
    main()
