#!/usr/bin/env python3
"""Lee una textura .pct de F1 06 (la copia que saca el juego con F1_PCT_VOLCAR=<carpeta>) y la dibuja (solo lectura).

Formato (Aspecto T4, de los bytes leidos):
  cabecera de 13 bytes: u8 (?), u8 bits por pixel (8 o 4), u16 (?), u16 alto, u16 ancho, u16 0, u16 (?), u8 (?)
  y despues los paquetes GS que la suben, igual que la textura de las letras .bft: A+D (BITBLTBUF, TRXPOS, TRXREG,
  TRXDIR) y una etiqueta GIF IMAGE con la imagen (PSMT8 o PSMT4 enviada como CT32, w x h: la textura es 2w x 2h en
  PSMT8 y 2w x 4h en PSMT4), y lo mismo con la paleta CT32 (16 x 16 = 256 colores en PSMT8, con el orden CSM1; 8 x 2
  = 16 en PSMT4). El alfa del GS va de 0 a 0x80 (opaco).
El mismo formato lo lee el anfitrion en ps2xRuntime/src/lib/ps2_game_graphics.cpp (con sus pruebas).
Uso: python -I scripts/pct_decodificar.py <archivo.pct> <salida.png> [escala]
  salida.png: la textura sobre un fondo gris a cuadros, ampliada (por defecto 4x).
"""
import os
import re
import struct
import sys
import zlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GS = os.path.join(RAIZ, 'third_party', 'PS2Recomp', 'ps2xRuntime', 'include', 'runtime', 'gs')


def tabla(archivo, nombre, filas):
    txt = open(os.path.join(GS, archivo)).read()
    cuerpo = txt[txt.index(nombre):]
    return [list(map(int, f.split(','))) for f in re.findall(r'\{([0-9, ]+)\}', cuerpo)[:filas]]


BLOCK32 = tabla('ps2_gs_psmct32.h', 'blockTable32', 4)
COL32 = tabla('ps2_gs_psmct32.h', 'columnTable32', 8)
BLOCK8 = tabla('ps2_gs_psmt8.h', 'blockTable8', 4)
COL8 = tabla('ps2_gs_psmt8.h', 'columnTable8', 16)
BLOCK4 = tabla('ps2_gs_psmt4.h', 'blockTable4', 8)
COL4 = tabla('ps2_gs_psmt4.h', 'columnTable4', 16)


def addr32(bw, x, y):
    page = (y >> 5) * bw + (x >> 6)
    return (page << 13) + BLOCK32[(y >> 3) & 3][(x >> 3) & 7] * 256 + COL32[y & 7][x & 7] * 4


def addr8(tbw, x, y):
    page = (y >> 6) * max(1, tbw >> 1) + (x >> 7)
    return (page << 13) + BLOCK8[(y >> 4) & 3][(x >> 4) & 7] * 256 + COL8[y & 15][x & 15]


def addr4(tbw, x, y):
    page = (y >> 7) * max(1, tbw >> 1) + (x >> 7)
    return (page << 14) + BLOCK4[(y >> 4) & 7][(x >> 5) & 3] * 512 + COL4[y & 15][x & 31]


def png(ruta, ancho, alto, rgba):
    filas = b''.join(b'\0' + bytes(rgba[y * ancho * 4:(y + 1) * ancho * 4]) for y in range(alto))

    def trozo(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    open(ruta, 'wb').write(b'\x89PNG\r\n\x1a\n' + trozo(b'IHDR', struct.pack('>IIBBBBB', ancho, alto, 8, 6, 0, 0, 0))
                           + trozo(b'IDAT', zlib.compress(filas)) + trozo(b'IEND', b''))


def subidas(b):
    out = []
    i = 0
    while True:
        i = b.find(bytes([0x53, 0, 0, 0, 0, 0, 0, 0]), i + 1)
        if i < 24:
            break
        if struct.unpack_from('<Q', b, i - 16)[0] != 0x52:
            continue
        trxreg = struct.unpack_from('<Q', b, i - 24)[0]
        tag = struct.unpack_from('<Q', b, i + 8)[0]
        nloop = tag & 0x7fff
        out.append((trxreg & 0xfff, (trxreg >> 32) & 0xfff, b[i + 24:i + 24 + nloop * 16], i + 24))
    return out


def decodificar(b):
    (w, h, datos, _), (cw, ch, clut, _) = subidas(b)[:2]
    colores = cw * ch
    tw, th = (w * 2, h * 2) if colores == 256 else (w * 2, h * 4)
    bw = (w + 63) // 64
    paginas_alto = (h + 31) // 32
    mem = bytearray(bw * paginas_alto * 8192)
    for y in range(h):
        for x in range(w):
            a = addr32(bw, x, y)
            mem[a:a + 4] = datos[(y * w + x) * 4:(y * w + x) * 4 + 4]
    paleta = []
    for i in range(colores):
        if colores == 256:  # CSM1: las entradas 8-15 y 16-23 de cada 32 cambiadas
            x = (i & 7) | ((i & 0x10) >> 1)
            y = ((i >> 5) << 1) | ((i & 8) >> 3)
            k = y * 16 + x
        else:
            k = i
        c = struct.unpack_from('<I', clut, k * 4)[0]
        a = c >> 24
        paleta.append((c & 255, (c >> 8) & 255, (c >> 16) & 255, 255 if a >= 0x80 else a * 2))
    rgba = bytearray(tw * th * 4)
    tbw = 2 * bw
    for y in range(th):
        for x in range(tw):
            if colores == 256:
                idx = mem[addr8(tbw, x, y)]
            else:
                na = addr4(tbw, x, y)
                v = mem[na >> 1]
                idx = (v >> 4) if (na & 1) else (v & 15)
            rgba[(y * tw + x) * 4:(y * tw + x) * 4 + 4] = bytes(paleta[idx])
    return tw, th, rgba


def main():
    b = open(sys.argv[1], 'rb').read()
    escala = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    print('cabecera', b[:13].hex(' '), 'bytes', len(b))
    print('subidas', [(w, h, len(d), hex(at)) for w, h, d, at in subidas(b)])
    tw, th, rgba = decodificar(b)
    print('textura', tw, 'x', th)
    lw, lh = tw * escala, th * escala
    out = bytearray(lw * lh * 4)
    for y in range(lh):
        for x in range(lw):
            fondo = 110 if ((x // 8 + y // 8) & 1) else 150
            k = ((y // escala) * tw + x // escala) * 4
            r, g, bl, a = rgba[k:k + 4]
            f = a / 255
            out[(y * lw + x) * 4:(y * lw + x) * 4 + 4] = bytes(
                [int(r * f + fondo * (1 - f)), int(g * f + fondo * (1 - f)), int(bl * f + fondo * (1 - f)), 255])
    png(sys.argv[2], lw, lh, out)


if __name__ == '__main__':
    main()
