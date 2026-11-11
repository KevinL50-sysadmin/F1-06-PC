"""Lista las funciones (ordinales) que importa un IRX, por librería. Uso: irx_importa.py archivo.irx"""
import struct, sys

datos = open(sys.argv[1], 'rb').read()
pos = 0
while True:
    pos = datos.find(struct.pack('<I', 0x41E00000), pos)
    if pos < 0:
        break
    nombre = datos[pos + 12:pos + 20].split(b'\0')[0].decode('ascii', 'replace')
    p = pos + 20
    ordinales = []
    while p + 8 <= len(datos):
        a, b = struct.unpack_from('<II', datos, p)
        if a == 0 and b == 0:
            break
        if a == 0x03E00008 and (b >> 16) == 0x2400:
            ordinales.append(b & 0xFFFF)
        p += 8
    print('%-10s %s' % (nombre, ' '.join(str(o) for o in ordinales)))
    pos += 4
