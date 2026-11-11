"""Extrae de un 'git diff' solo los bloques (hunks) que contienen alguna de las palabras dadas.
Uso: git diff -- archivos | python filtrar_diff.py palabra1 [palabra2 ...] > parche.patch
Con '-' delante de una palabra, descarta los bloques que la contienen."""
import sys

incluir = [p for p in sys.argv[1:] if not p.startswith('-')]
excluir = [p[1:] for p in sys.argv[1:] if p.startswith('-')]
texto = sys.stdin.read().split('\n')
salida, cabecera, bloque = [], [], []

def cerrar_bloque():
    global bloque
    if bloque:
        cuerpo = '\n'.join(bloque)
        if any(p in cuerpo for p in incluir) and not any(p in cuerpo for p in excluir):
            if cabecera:
                salida.extend(cabecera)
                cabecera.clear()
            salida.extend(bloque)
    bloque = []

for linea in texto:
    if linea.startswith('diff --git'):
        cerrar_bloque()
        cabecera[:] = [linea]
    elif linea.startswith('@@'):
        cerrar_bloque()
        bloque = [linea]
    elif bloque:
        bloque.append(linea)
    else:
        cabecera.append(linea)
cerrar_bloque()
sys.stdout.write('\n'.join(salida) + '\n')
