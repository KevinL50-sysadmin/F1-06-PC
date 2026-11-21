# Comprueba scripts/paquete/recomplauncher.json (odd/tasks/recomplauncher-json.md T3): el esquema oficial de
# RecompLauncher, que cada clave y valor de settings exista en ps2_options.cpp (y que defaults sean los del jugador),
# los argumentos de prepare y start, y (opcional) que un paquete hecho lleve el archivo junto a F1-06.exe.
# No descarga nada: el esquema se baja aparte (https://recomplauncher.com/schema/recomplauncher.json) a
# build/rl-schema/recomplauncher.schema.json. Necesita jsonschema; sin instalarlo en el Python del sistema:
#   python -I -m venv build/rl-venv && build/rl-venv/Scripts/python.exe -I -m pip install jsonschema
# Uso: build/rl-venv/Scripts/python.exe -I scripts/comprobar-recomplauncher.py [--json <ruta>] [--esquema <ruta>]
#        [--paquete <carpeta con F1-06.exe>] [--zip <paquete.zip>]
# Sale con 0 si todo esta bien y con 1 si algo falla (cada fallo en una linea "FALLO: ...").
import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
OPCIONES_CPP = RAIZ / "third_party/PS2Recomp/ps2xRuntime/src/lib/ps2_options.cpp"
EXE = "F1-06.exe"
CLAVES_BOTON = {"boton_acelerar", "boton_frenar", "boton_subir_marcha", "boton_bajar_marcha", "boton_mirar_atras"}
PREPARE_ARGS = ["--extraer-iso", "{game}"]
DONE = ["{install}/data/F1-06-disco.txt"]

fallos = []


def fallo(texto):
    fallos.append(texto)
    print("FALLO: " + texto)


def opciones_del_juego(texto):
    """{clave: (valor por defecto del jugador, valores admitidos o None, minimo, maximo)} de optionSpecs()."""
    specs = {}
    # The value list is either inline ({{"0", "0"}, ...}) or a helper call (drivingButtonChoices()), read from its body.
    patron = re.compile(r'\{"(\w+)",\s*"PS2X_\w+",\s*"([^"]*)",\s*(?:\{(.*?)\}|(\w+)\(\)),\s*(-?\d+),\s*(-?\d+),',
                        re.S)
    for m in patron.finditer(texto):
        lista = m.group(3)
        if m.group(4):
            cuerpo = re.search(r'\b' + m.group(4) + r'\(\)\s*\{\s*return\s*\{(.*?)\};', texto, re.S)
            lista = cuerpo.group(1) if cuerpo else ""
        valores = re.findall(r'\{"([^"]+)",\s*(?:"[^"]*"|nullptr)\}', lista)
        specs[m.group(1)] = (m.group(2), valores or None, int(m.group(5)), int(m.group(6)))
    return specs


def valor_admitido(spec, valor):
    _, valores, minimo, maximo = spec
    if valores is not None:
        return valor in valores
    return valor.isdigit() and len(valor) <= 9 and minimo <= int(valor) <= maximo


def comprobar_esquema(datos, ruta_esquema):
    try:
        import jsonschema
    except ImportError:
        fallo("falta jsonschema (ver la cabecera de este script: venv en build/rl-venv)")
        return
    if not ruta_esquema.is_file():
        fallo(f"falta el esquema {ruta_esquema} (bajalo de https://recomplauncher.com/schema/recomplauncher.json)")
        return
    esquema = json.loads(ruta_esquema.read_text(encoding="utf-8"))
    validador = jsonschema.Draft7Validator(esquema)
    errores = sorted(validador.iter_errors(datos), key=lambda e: list(e.path))
    for e in errores:
        fallo("esquema: " + "/".join(str(p) for p in e.path) + ": " + e.message)
    if not errores:
        print(f"esquema: valido ({ruta_esquema.name}, draft-07)")


def comprobar_ajustes(datos):
    antes = len(fallos)
    specs = opciones_del_juego(OPCIONES_CPP.read_text(encoding="utf-8"))
    if len(specs) < 20:
        fallo(f"solo se leyeron {len(specs)} opciones de {OPCIONES_CPP.name}: ¿cambio el formato de optionSpecs()?")
        return
    for ajuste in datos.get("settings", []):
        nombre = ajuste.get("id")
        if ajuste.get("in", "main") != "main":
            fallo(f"settings {nombre}: in = {ajuste.get('in')} (todo va en settings.ini, main)")
        for elegido in ajuste.get("choices", []):
            for clave, valor in elegido.get("set", {}).items():
                if clave not in specs:
                    fallo(f"settings {nombre}: la clave {clave} no esta en {OPCIONES_CPP.name}")
                elif not valor_admitido(specs[clave], valor):
                    fallo(f"settings {nombre}: {clave} = {valor} no es un valor admitido")
        for clave, valor in ajuste.get("defaults", {}).items():
            if clave not in specs:
                fallo(f"settings {nombre}: defaults con la clave desconocida {clave}")
            elif valor != specs[clave][0]:
                fallo(f"settings {nombre}: defaults {clave} = {valor}, pero el juego pone {specs[clave][0]} al jugador")
        for clave in ajuste.get("when", {}):
            if clave not in specs:
                fallo(f"settings {nombre}: when con la clave desconocida {clave}")
        # Driving buttons: the launcher writes each key on its own, so a choice that sets them must set all five
        # with no button used twice (the game would reset all of them to the original buttons).
        for elegido in ajuste.get("choices", []):
            botones = {c: v for c, v in elegido.get("set", {}).items() if c in CLAVES_BOTON}
            if botones and (set(botones) != CLAVES_BOTON or len(set(botones.values())) != len(botones)):
                fallo(f"settings {nombre}: {elegido.get('label')} debe poner las {len(CLAVES_BOTON)} acciones "
                      f"con botones distintos: {botones}")
        claves = {c for e in ajuste.get("choices", []) for c in e.get("set", {})}
        if claves - set(ajuste.get("defaults", {})):
            fallo(f"settings {nombre}: faltan en defaults {sorted(claves - set(ajuste.get('defaults', {})))}")
    for entrada in datos.get("config", []):
        for nombre in ("always", "new", "ensure"):
            for clave, valor in entrada.get(nombre, {}).items():
                if clave not in specs or not valor_admitido(specs[clave], valor):
                    fallo(f"config {nombre}: {clave} = {valor} no es una opcion valida del juego")
    if len(fallos) == antes:
        print(f"settings: {len(datos.get('settings', []))} ajustes bien contra {len(specs)} opciones de "
              f"{OPCIONES_CPP.name}")


def comprobar_arranque(datos):
    antes = len(fallos)
    prepare = datos.get("prepare", {})
    start = datos.get("start", {})
    if prepare.get("program") != [EXE] or start.get("program") != [EXE]:
        fallo(f"prepare.program y start.program deben ser [\"{EXE}\"]")
    if prepare.get("args") != PREPARE_ARGS:
        fallo(f"prepare.args = {prepare.get('args')}; el .exe solo admite {PREPARE_ARGS} (sin --ventana: sin ventana)")
    if prepare.get("done") != DONE:
        fallo(f"prepare.done = {prepare.get('done')}; debe ser {DONE}")
    if any("{game}" in a for a in start.get("args", [])):
        fallo("start.args lleva {game}: la ISO ganaria a data (main.cpp, findBootSource)")
    for nombre in start.get("env", {}):
        if nombre.upper().startswith("PS2X_"):
            fallo(f"start.env {nombre}: las PS2X_* mandan sobre settings.ini y bloquean el menu del juego")
    if datos.get("game", {}).get("check", {}).get("iso9660") != "SCES_539.50":
        fallo("game.check.iso9660 debe ser SCES_539.50 (BOOT2 de SCES-53950)")
    if len(fallos) == antes:
        print("prepare/start: argumentos correctos")


def comprobar_paquete(fuente, carpeta):
    exe, json_paquete = carpeta / EXE, carpeta / "recomplauncher.json"
    if not exe.is_file():
        fallo(f"paquete: falta {exe}")
    if not json_paquete.is_file():
        fallo(f"paquete: falta {json_paquete} junto a {EXE}")
    elif json_paquete.read_bytes() != fuente.read_bytes():
        fallo(f"paquete: {json_paquete} no es igual que {fuente}")
    else:
        print(f"paquete: {json_paquete} junto a {EXE}, igual que la fuente")


def comprobar_zip(fuente, ruta_zip):
    with zipfile.ZipFile(ruta_zip) as z:
        nombres = set(z.namelist())
        exes = [n for n in nombres if n.endswith("/" + EXE) or n == EXE]
        if len(exes) != 1:
            fallo(f"zip: {len(exes)} {EXE} en {ruta_zip}")
            return
        entrada = exes[0][: -len(EXE)] + "recomplauncher.json"
        if entrada not in nombres:
            fallo(f"zip: falta {entrada} junto a {exes[0]}")
        elif z.read(entrada) != fuente.read_bytes():
            fallo(f"zip: {entrada} no es igual que {fuente}")
        else:
            print(f"zip: {entrada} junto a {exes[0]}, igual que la fuente")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--json", type=Path, default=RAIZ / "scripts/paquete/recomplauncher.json")
    p.add_argument("--esquema", type=Path, default=RAIZ / "build/rl-schema/recomplauncher.schema.json")
    p.add_argument("--paquete", type=Path)
    p.add_argument("--zip", type=Path)
    a = p.parse_args()
    datos = json.loads(a.json.read_text(encoding="utf-8"))
    comprobar_esquema(datos, a.esquema)
    comprobar_ajustes(datos)
    comprobar_arranque(datos)
    if a.paquete:
        comprobar_paquete(a.json, a.paquete)
    if a.zip:
        comprobar_zip(a.json, a.zip)
    print("RESULTADO: " + ("bien" if not fallos else f"{len(fallos)} fallos"))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
