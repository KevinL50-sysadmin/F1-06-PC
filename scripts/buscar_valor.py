"""Busca en volcados de la RAM del EE las variables que siguen a un valor conocido en cada volcado.

Pensado para las capturas con PS2X_CAPTURA_RAM=1 (medir-carrera.ps1 guarda la RAM junto a cada PNG, como .png.ram):
se lee en cada captura un dato del HUD (la velocidad, por ejemplo) y se buscan los floats proporcionales a el en todos
los volcados a la vez (la constante de proporcion sale en el resultado: 1 si va en MPH, 1,609 en km/h, 0,447 en m/s...).

Uso: python -I scripts/buscar_valor.py volcado1.ram:valor1 volcado2.ram:valor2 ... [--tol 0.03] [--entero]
     [--rango 0x100000-0x2000000] [--max 60]
  --tol     error relativo admitido (mas medio punto del valor, que en el HUD va redondeado)
  --entero  busca enteros de 32 y 16 bits iguales al valor en vez de floats proporcionales
"""
import array
import math
import sys

RMIN, RMAX = 1e-4, 1e4  # proporciones admitidas (unidades: de m/fotograma a mm/s)


def cargar(ruta):
    datos = open(ruta, 'rb').read()
    return datos


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    tol = 0.03
    entero = False
    rango = (0x100000, 0x2000000)
    maximo = 60
    pares = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--tol':
            tol = float(args[i + 1]); i += 2; continue
        if a == '--entero':
            entero = True; i += 1; continue
        if a == '--rango':
            ini, fin = args[i + 1].split('-'); rango = (int(ini, 0), int(fin, 0)); i += 2; continue
        if a == '--max':
            maximo = int(args[i + 1]); i += 2; continue
        ruta, valor = a.rsplit(':', 1)
        pares.append((ruta, float(valor)))
        i += 1
    if len(pares) < 2:
        print('Hacen falta al menos dos volcados')
        return 1
    volcados = [(cargar(r), v, r) for r, v in pares]
    ini, fin = rango
    if entero:
        for ancho, codigo in ((4, 'i'), (2, 'h')):
            listas = []
            for datos, v, _ in volcados:
                arr = array.array(codigo)
                arr.frombytes(datos[ini:fin])
                listas.append((arr, int(round(v))))
            n = len(listas[0][0])
            hallados = 0
            for k in range(n):
                if all(arr[k] == v for arr, v in listas):
                    print('entero%d en 0x%x' % (ancho * 8, ini + k * ancho))
                    hallados += 1
                    if hallados >= maximo:
                        break
        return 0
    floats = []
    for datos, v, _ in volcados:
        arr = array.array('f')
        arr.frombytes(datos[ini:fin])
        floats.append(arr)
    referencia = max(range(len(volcados)), key=lambda j: abs(volcados[j][1]))
    vref = volcados[referencia][1]
    aref = floats[referencia]
    hallados = 0
    for k in range(len(aref)):
        x = aref[k]
        if x == 0.0 or not math.isfinite(x):
            continue
        r = x / vref
        if not (RMIN <= abs(r) <= RMAX):
            continue
        ok = True
        for j, (datos, v, _) in enumerate(volcados):
            if j == referencia:
                continue
            y = floats[j][k]
            if not math.isfinite(y) or abs(y - r * v) > abs(r) * (0.6 + tol * abs(v)):
                ok = False
                break
        if ok:
            valores = ' '.join('%.3f' % floats[j][k] for j in range(len(volcados)))
            print('float en 0x%x  proporcion %.4f  valores %s' % (ini + k * 4, r, valores))
            hallados += 1
            if hallados >= maximo:
                break
    print('%d candidatos' % hallados)
    return 0


if __name__ == '__main__':
    sys.exit(main())
