# Recorta un tramo de un volcado del SPU2 (PS2X_SPU2_WAV; 48 kHz estereo 16 bits, aunque le falte la cabecera porque
# el juego se cerro a la fuerza) y lo guarda como WAV valido para escucharlo.
# Uso: python -I scripts/wav_recortar.py logs/spu2-g.wav desde_s hasta_s logs/salida.wav
import sys
import wave


def main():
    origen, desde, hasta, destino = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    with open(origen, 'rb') as f:
        f.seek(44 + int(desde * 48000) * 4)
        datos = f.read(int((hasta - desde) * 48000) * 4)
    with wave.open(destino, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(datos)
    print(f"{destino}: {len(datos) / 4 / 48000:.1f} s")


if __name__ == '__main__':
    main()
