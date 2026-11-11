# Decodifica los paquetes GIF volcados con F1_GS_VOLCAR_TIJERA (lineas "[gs:tijera]" del .err.log del juego): una
# linea por escritura de registro, con PRIM, RGBAQ, UV, ST y XYZ2 legibles (XYZ menos el desplazamiento, por defecto
# 1792 como el XYOFFSET de F1 06) y los registros A+D con su nombre.
# Uso: python -I scripts/gif_decodificar.py logs/explorar-X.err.log [--desplazamiento 1792] [--desde N] [--hasta M]
import argparse
import re
import sys

NOMBRES = {
    0x00: 'PRIM', 0x01: 'RGBAQ', 0x02: 'ST', 0x03: 'UV', 0x04: 'XYZF2', 0x05: 'XYZ2', 0x06: 'TEX0_1', 0x07: 'TEX0_2',
    0x08: 'CLAMP_1', 0x09: 'CLAMP_2', 0x0A: 'FOG', 0x0C: 'XYZF3', 0x0D: 'XYZ3', 0x14: 'TEX1_1', 0x15: 'TEX1_2',
    0x16: 'TEX2_1', 0x17: 'TEX2_2', 0x18: 'XYOFFSET_1', 0x19: 'XYOFFSET_2', 0x1A: 'PRMODECONT', 0x1B: 'PRMODE',
    0x1C: 'TEXCLUT', 0x22: 'SCANMSK', 0x34: 'MIPTBP1_1', 0x35: 'MIPTBP1_2', 0x36: 'MIPTBP2_1', 0x37: 'MIPTBP2_2',
    0x3B: 'TEXA', 0x3D: 'FOGCOL', 0x3F: 'TEXFLUSH', 0x40: 'SCISSOR_1', 0x41: 'SCISSOR_2', 0x42: 'ALPHA_1',
    0x43: 'ALPHA_2', 0x44: 'DIMX', 0x45: 'DTHE', 0x46: 'COLCLAMP', 0x47: 'TEST_1', 0x48: 'TEST_2', 0x49: 'PABE',
    0x4A: 'FBA_1', 0x4B: 'FBA_2', 0x4C: 'FRAME_1', 0x4D: 'FRAME_2', 0x4E: 'ZBUF_1', 0x4F: 'ZBUF_2',
    0x50: 'BITBLTBUF', 0x51: 'TRXPOS', 0x52: 'TRXREG', 0x53: 'TRXDIR', 0x54: 'HWREG', 0x60: 'SIGNAL', 0x61: 'FINISH',
    0x62: 'LABEL',
}
TIPOS = ['punto', 'linea', 'tira_lineas', 'triangulo', 'tira_tri', 'abanico', 'sprite', 'prohibido']


def prim(valor):
    t = valor & 7
    banderas = [n for b, n in ((3, 'IIP'), (4, 'TME'), (5, 'FGE'), (6, 'ABE'), (7, 'AA1'), (8, 'FST'), (9, 'CTXT'),
                               (10, 'FIX')) if (valor >> b) & 1]
    return TIPOS[t] + ' ' + ','.join(banderas)


def registro(direccion, valor):
    nombre = NOMBRES.get(direccion, 'reg%02x' % direccion)
    if direccion in (0x40, 0x41):
        return '%s (%d,%d)-(%d,%d)' % (nombre, valor & 0x7FF, (valor >> 32) & 0x7FF, (valor >> 16) & 0x7FF,
                                       (valor >> 48) & 0x7FF)
    if direccion in (0x06, 0x07):
        return ('%s tbp=%d tbw=%d psm=0x%x tw=%d th=%d tcc=%d tfx=%d cbp=%d' %
                (nombre, valor & 0x3FFF, (valor >> 14) & 63, (valor >> 20) & 63, (valor >> 26) & 15,
                 (valor >> 30) & 15, (valor >> 34) & 1, (valor >> 35) & 3, (valor >> 37) & 0x3FFF))
    if direccion == 0x00:
        return 'PRIM ' + prim(valor)
    return '%s 0x%x' % (nombre, valor)


def decodificar(palabras, desplazamiento):
    """palabras: lista de (hi, lo) de 64 bits por cuadruple palabra."""
    i = 0
    salida = []
    while i < len(palabras):
        hi, lo = palabras[i]
        i += 1
        nloop = lo & 0x7FFF
        eop = (lo >> 15) & 1
        pre = (lo >> 46) & 1
        flg = (lo >> 58) & 3
        nreg = (lo >> 60) & 15 or 16
        regs = [(hi >> (4 * r)) & 15 for r in range(nreg)]
        cabecera = 'GIFTAG nloop=%d nreg=%d flg=%d eop=%d' % (nloop, nreg, flg, eop)
        if pre:
            cabecera += ' PRE ' + prim((lo >> 47) & 0x7FF)
        salida.append(cabecera)
        if flg == 0:
            for _ in range(nloop):
                for r in regs:
                    if i >= len(palabras):
                        return salida
                    h, l = palabras[i]
                    i += 1
                    if r == 0x0E:
                        salida.append('  ' + registro(h & 0xFF, l))
                    elif r == 0x01:
                        salida.append('  RGBAQ %d/%d/%d/%d' % (l & 0xFF, (l >> 32) & 0xFF, h & 0xFF, (h >> 32) & 0xFF))
                    elif r == 0x03:
                        salida.append('  UV %.2f/%.2f' % ((l & 0x3FFF) / 16, ((l >> 32) & 0x3FFF) / 16))
                    elif r == 0x02:
                        salida.append('  ST raw 0x%x' % l)
                    elif r in (0x04, 0x05):
                        salida.append('  XYZ%s %.4g,%.4g z=0x%x' % ('F2' if r == 4 else '2',
                                                               (l & 0xFFFF) / 16 - desplazamiento,
                                                               ((l >> 32) & 0xFFFF) / 16 - desplazamiento, h & 0xFFFFFFFF))
                    elif r == 0x0F:
                        pass
                    else:
                        salida.append('  desc%x hi=0x%x lo=0x%x' % (r, h, l))
        elif flg == 1:
            n = nloop * nreg
            mitades = []
            for _ in range((n + 1) // 2):
                if i >= len(palabras):
                    break
                h, l = palabras[i]
                i += 1
                mitades += [l, h]
            for k in range(n):
                salida.append('  (reglist) ' + registro(regs[k % nreg], mitades[k]))
        else:
            salida.append('  (imagen %d qw)' % nloop)
            i += nloop
    return salida


def main():
    p = argparse.ArgumentParser()
    p.add_argument('log')
    p.add_argument('--desplazamiento', type=float, default=1792)
    p.add_argument('--desde', type=int, default=1)
    p.add_argument('--hasta', type=int, default=10 ** 9)
    a = p.parse_args()
    paquetes = []
    with open(a.log, encoding='utf-8', errors='replace') as f:
        for linea in f:
            m = re.search(r'\[gs:tijera\] paquete(.*) de (\d+) bytes', linea)
            if m:
                paquetes.append((m.group(1).strip(), []))
                continue
            m = re.search(r'\[gs:tijera\]\s+\d+ ([0-9a-f]{16}) ([0-9a-f]{16})', linea)
            if m and paquetes:
                paquetes[-1][1].append((int(m.group(1), 16), int(m.group(2), 16)))
    for n, (etiqueta, palabras) in enumerate(paquetes, 1):
        if a.desde <= n <= a.hasta:
            print('== paquete %d %s (%d bytes)' % (n, etiqueta, 16 * len(palabras)))
            for linea in decodificar(palabras, a.desplazamiento):
                print(linea)


if __name__ == '__main__':
    sys.exit(main())
