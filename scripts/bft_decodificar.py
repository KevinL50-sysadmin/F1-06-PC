#!/usr/bin/env python3
"""Lee una letra .bft de F1 06 (la copia que saca el juego con F1_BFT_VOLCAR=<carpeta>) y la dibuja (solo lectura).

Formato (Aspecto T2, del lector del juego func_264888 y de los bytes leidos):
  u32 0, u32 alto de linea (px)
  pagina principal: 7 u32 (n, n, primero=2, fin, 0x41, -1, fin), (n + 2) u32 (punteros de la herramienta, sin uso),
    (n + 2) glifos de 0x48 bytes: u32 codigo (16 bits bajos: el caracter Unicode), 8 float (ancho, alto, u0, u1,
    v0, v1, avance, desplazamiento x), 8 int32 (+0x24: pagina de ajustes entre pares o -1), u32 (+0x44, byte bajo:
    0 en un glifo, 1 en los dos centinelas del principio)
  por cada glifo con pagina de ajustes: 7 u32 (n, ...), (n + 2) u32, (n + 2) registros de 0x28 bytes
  textura: paquete GIF (A+D: BITBLTBUF, TRXPOS, TRXREG, TRXDIR; IMAGE) de la imagen (PSMT4 subida como CT32) y la
    paleta (16 colores CT32, 8x2).
El mismo formato lo lee el juego anfitrion en ps2xRuntime/src/lib/ps2_game_font.cpp (con sus pruebas).
Uso: python -I scripts/bft_decodificar.py <archivo.bft> <salida.png> [texto]
  salida.png: el atlas y el texto (por defecto JUEGO 50 FPS) dibujado con la letra, a 3x.
"""
import struct
import sys
import zlib

BLOCK32 = [[0, 1, 4, 5, 16, 17, 20, 21], [2, 3, 6, 7, 18, 19, 22, 23], [8, 9, 12, 13, 24, 25, 28, 29],
           [10, 11, 14, 15, 26, 27, 30, 31]]
COL32 = [[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15], [16, 17, 20, 21, 24, 25, 28, 29],
         [18, 19, 22, 23, 26, 27, 30, 31], [32, 33, 36, 37, 40, 41, 44, 45], [34, 35, 38, 39, 42, 43, 46, 47],
         [48, 49, 52, 53, 56, 57, 60, 61], [50, 51, 54, 55, 58, 59, 62, 63]]
BLOCK4 = [[0, 2, 8, 10], [1, 3, 9, 11], [4, 6, 12, 14], [5, 7, 13, 15], [16, 18, 24, 26], [17, 19, 25, 27],
          [20, 22, 28, 30], [21, 23, 29, 31]]


def leer_tabla_runtime():
    import os
    import re
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    txt = open(os.path.join(raiz, 'third_party', 'PS2Recomp', 'ps2xRuntime', 'include', 'runtime', 'gs',
                            'ps2_gs_psmt4.h')).read()
    cuerpo = txt[txt.index('columnTable4'):]
    filas = re.findall(r'\{([0-9, ]+)\}', cuerpo)[:16]
    return [list(map(int, f.split(','))) for f in filas]


COL4 = leer_tabla_runtime()


def addr32(bw, x, y):
    page = (y >> 5) * bw + (x >> 6)
    block = BLOCK32[(y >> 3) & 3][(x >> 3) & 7]
    return (page << 13) + block * 256 + COL32[y & 7][x & 7] * 4


def addr4(tbw, x, y):
    ppr = max(1, tbw >> 1)
    page = (y >> 7) * ppr + (x >> 7)
    block = BLOCK4[(y >> 4) & 7][(x >> 5) & 3]
    return (page << 14) + block * 512 + COL4[y & 15][x & 31]


def png(ruta, ancho, alto, rgba):
    filas = b''.join(b'\0' + bytes(rgba[y * ancho * 4:(y + 1) * ancho * 4]) for y in range(alto))
    def trozo(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    open(ruta, 'wb').write(b'\x89PNG\r\n\x1a\n' + trozo(b'IHDR', struct.pack('>IIBBBBB', ancho, alto, 8, 6, 0, 0, 0))
                           + trozo(b'IDAT', zlib.compress(filas)) + trozo(b'IEND', b''))


def main():
    b = open(sys.argv[1], 'rb').read()
    alto_linea = struct.unpack_from('<I', b, 4)[0]
    h = struct.unpack_from('<7I', b, 8)
    n = h[0] + 2
    o = 8 + 28 + n * 4
    glifos = [struct.unpack_from('<I8f8iI', b, o + i * 0x48) for i in range(n)]
    o += n * 0x48
    glifos = [g for g in glifos if g[17] & 0xff == 0]  # +0x44: 1 en los dos centinelas
    for g in glifos:
        if g[9] != -1:
            k = struct.unpack_from('<7I', b, o)
            o += 28 + (k[0] + 2) * 4 + (k[0] + 2) * 0x28
    print('alto de linea', alto_linea, 'cabecera', h, 'glifos', n, 'textura en', hex(o), 'resto', len(b) - o)
    # Subidas: buscar las etiquetas GIF IMAGE tras TRXDIR (A+D 0x53).
    subidas = []
    i = o
    while True:
        i = b.find(bytes([0x53, 0, 0, 0, 0, 0, 0, 0]), i + 1)
        if i < 0:
            break
        trxreg = struct.unpack_from('<Q', b, i - 24)[0]
        tag = struct.unpack_from('<Q', b, i + 8)[0]
        nloop = tag & 0x7fff
        datos = b[i + 24:i + 24 + nloop * 16]
        subidas.append((trxreg & 0xfff, (trxreg >> 32) & 0xfff, datos, i))
    print('subidas', [(w, hh, len(d), hex(at)) for w, hh, d, at in subidas])
    (w, hh, datos, _), (cw, ch, clut, _) = subidas[0], subidas[1]
    mem = bytearray(0x10000)
    bw = max(1, w // 64)
    for y in range(hh):
        for x in range(w):
            a = addr32(bw, x, y)
            mem[a:a + 4] = datos[(y * w + x) * 4:(y * w + x) * 4 + 4]
    paleta = [struct.unpack_from('<I', clut, k * 4)[0] for k in range(16)]
    tw, th = w * 2, hh * 4
    tbw = tw // 64
    rgba = bytearray(tw * th * 4)
    for y in range(th):
        for x in range(tw):
            na = addr4(tbw, x, y)
            v = mem[na >> 1]
            idx = (v >> 4) if (na & 1) else (v & 15)
            c = paleta[idx]
            r, g, bl, al = c & 255, (c >> 8) & 255, (c >> 16) & 255, (c >> 24) & 255
            rgba[(y * tw + x) * 4:(y * tw + x) * 4 + 4] = bytes([r, g, bl, min(255, al * 2)])
    print('textura', tw, 'x', th, 'paleta', [hex(p) for p in paleta])
    texto = sys.argv[3] if len(sys.argv) > 3 else 'JUEGO 50 FPS'
    por_codigo = {g[0] & 0xffff: g for g in glifos}
    # Lienzo: el atlas arriba y el texto abajo, a 2x sobre gris.
    lw = max(tw, 8 * len(texto) * 2)
    lh = th + alto_linea * 3
    lienzo = bytearray(lw * lh * 4)
    for k in range(lw * lh):
        lienzo[k * 4:k * 4 + 4] = bytes([60, 60, 90, 255])
    def poner(x, y, px):
        if 0 <= x < lw and 0 <= y < lh and px[3]:
            a = px[3] / 255
            k = (y * lw + x) * 4
            for c in range(3):
                lienzo[k + c] = int(lienzo[k + c] * (1 - a) + px[c] * a)
    for y in range(th):
        for x in range(tw):
            poner(x, y, rgba[(y * tw + x) * 4:(y * tw + x) * 4 + 4])
    pluma = 2
    for ch_ in texto.encode('latin-1'):
        g = por_codigo.get(ch_)
        if g is None:
            print('sin glifo', ch_)
            continue
        ancho, alto, u0, u1, v0, v1, avance, dx = g[1:9]
        x0, x1 = round(u0 * tw), round(u1 * tw)
        y0, y1 = round((1 - v1) * th), round((1 - v0) * th)
        print(chr(ch_), 'rect', x0, y0, x1 - x0, y1 - y0, 'ancho', ancho, 'alto', alto, 'avance', avance, 'dx', dx,
              'ints', g[9:17], hex(g[0]))
        for yy in range(y0, y1):
            for xx in range(x0, x1):
                poner(pluma + int(dx) + xx - x0, th + 4 + yy - y0, rgba[(yy * tw + xx) * 4:(yy * tw + xx) * 4 + 4])
        pluma += int(avance)
    escala = 3
    grande = bytearray()
    for y in range(lh * escala):
        fila = lienzo[(y // escala) * lw * 4:(y // escala + 1) * lw * 4]
        for x in range(lw):
            grande += fila[x * 4:x * 4 + 4] * escala
    png(sys.argv[2], lw * escala, lh * escala, grande)


if __name__ == '__main__':
    main()
