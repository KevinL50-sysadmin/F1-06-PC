# Traduce los informes del vigilante ("[vigilante]") y los de fallo (odd/tasks/fallos-con-log.md T3b: "[fallo]" del
# filtro de excepciones, "[excepcion]" del hilo del juego y "[terminate]") de un registro (F1-06.log de un jugador) a
# nombres de funcion con el PDB exacto de esa compilacion. El paquete no lleva el PDB, asi que el informe solo da
# modulo+0xDESPL y la identidad de la compilacion (linea "[<informe>] compilacion: <exe> fecha_pe=.. tam_imagen=..
# pdb=.. guid=.. edad=N base=0x.."; cada marco se traduce con la ultima linea "compilacion:" leida antes que el).
# Cada marco "#i <exe>+0xDESPL" del .exe del juego pasa a "#i <exe>+0xDESPL funcion+0xDESPL [archivo:linea]"; los de
# otros modulos (ntdll.dll, KERNELBASE.dll, ucrtbase.dll...) y el resto del registro salen tal cual.
# El PDB se busca, en este orden, en -Pdb, en <Simbolos>\<version>_<guid>_<edad>\ (por defecto dist\simbolos; lo
# guarda scripts\empaquetar.ps1, con identidad.txt) y en build\runner-dev\ps2xRuntime (la compilacion de desarrollo actual). Solo vale el que tiene el
# mismo GUID y la misma edad que el informe (la edad sube en cada enlazado incremental): si ninguno coincide se dice y
# esos marcos quedan sin traducir (nunca se adivina con otro PDB); codigo de salida 2.
# Se usa dbghelp.dll de Windows (System32): carga el PDB solo, en una base inventada, y busca base+desplazamiento.
# Nombres: las funciones traducidas (sub_XXXXXX) y las del runtime son publicas y salen bien. Las estaticas o de
# espacio de nombres anonimo del codigo compilado sin informacion de depuracion completa salen con el nombre de la
# funcion publica anterior y sin [archivo:linea]: les pasa al runner antes del parche 0208 (v1.1.0 y anteriores; desde
# el 0208 se compila con /Z7) y a las librerias de fuera (raylib, miniaudio). Ante la duda, manda el modulo+0xDESPL.
# -Identidad <ruta>: no traduce nada; escribe la identidad de un .exe (exe, fecha_pe, tam_imagen, pdb, guid, edad) o
# de un .pdb (pdb, guid, edad). La usa scripts\empaquetar.ps1 para guardar y comprobar el PDB de cada version.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\simbolizar.ps1 -Registro 'ruta\F1-06.log'
#        [-Pdb 'ruta\ps2EntryRunner.pdb'] [-Simbolos 'dist\simbolos'] [-Salida 'ruta\traducido.txt']
#      powershell -NoProfile -ExecutionPolicy Bypass -File scripts\simbolizar.ps1 -Identidad 'ruta\F1-06.exe'
# Codigos de salida: 0 todo traducido (o sin informes), 1 uso o archivo incorrecto, 2 algun informe sin PDB que
# coincida.
param(
    [string]$Registro = '',
    [string[]]$Pdb = @(),
    [string]$Salida = '',
    [string]$Simbolos = 'dist\simbolos',
    [string]$Identidad = ''
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class F1Simbolos
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct Modulo
    {
        public uint SizeOfStruct;
        public ulong BaseOfImage;
        public uint ImageSize;
        public uint TimeDateStamp;
        public uint CheckSum;
        public uint NumSyms;
        public int SymType;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string ModuleName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string ImageName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string LoadedImageName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string LoadedPdbName;
        public uint CVSig;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 780)] public string CVData;
        public uint PdbSig;
        public Guid PdbSig70;
        public uint PdbAge;
        public int PdbUnmatched;
        public int DbgUnmatched;
        public int LineNumbers;
        public int GlobalSymbols;
        public int TypeInfo;
        public int SourceIndexed;
        public int Publics;
        public uint MachineType;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct Linea
    {
        public uint SizeOfStruct;
        public IntPtr Key;
        public uint LineNumber;
        public IntPtr FileName;
        public ulong Address;
    }

    [DllImport("dbghelp.dll", SetLastError = true)] static extern uint SymSetOptions(uint opciones);
    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SymInitializeW(IntPtr proceso, string ruta, bool invadir);
    [DllImport("dbghelp.dll", SetLastError = true)] static extern bool SymCleanup(IntPtr proceso);
    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern ulong SymLoadModuleExW(IntPtr proceso, IntPtr archivo, string imagen, string modulo, ulong base_,
        uint tam, IntPtr datos, uint flags);
    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SymGetModuleInfoW64(IntPtr proceso, ulong direccion, ref Modulo info);
    [DllImport("dbghelp.dll", SetLastError = true)]
    static extern bool SymFromAddrW(IntPtr proceso, ulong direccion, out ulong desplazamiento, IntPtr simbolo);
    [DllImport("dbghelp.dll", SetLastError = true)]
    static extern bool SymGetLineFromAddrW64(IntPtr proceso, ulong direccion, out uint desplazamiento, ref Linea linea);

    // Un "proceso" inventado: con invadir = false dbghelp acepta cualquier valor unico.
    static readonly IntPtr Proceso = new IntPtr(0x0F106);
    public const ulong Base = 0x140000000UL;
    static bool iniciado;

    // Carga el PDB solo (sin el .exe) en Base. Devuelve la informacion del modulo (GUID y edad del PDB).
    public static Modulo Cargar(string pdb, uint tamImagen)
    {
        if (iniciado) { SymCleanup(Proceso); iniciado = false; }
        // UNDNAME | LOAD_LINES | FAIL_CRITICAL_ERRORS | NO_PROMPTS
        SymSetOptions(0x2 | 0x10 | 0x200 | 0x80000);
        if (!SymInitializeW(Proceso, null, false))
            throw new Exception("SymInitialize ha fallado (" + Marshal.GetLastWin32Error() + ")");
        iniciado = true;
        ulong b = SymLoadModuleExW(Proceso, IntPtr.Zero, pdb, null, Base, tamImagen == 0 ? 0x10000000u : tamImagen,
            IntPtr.Zero, 0);
        if (b == 0)
            throw new Exception("dbghelp no pudo cargar " + pdb + " (" + Marshal.GetLastWin32Error() + ")");
        Modulo m = new Modulo();
        m.SizeOfStruct = (uint)Marshal.SizeOf(typeof(Modulo));
        if (!SymGetModuleInfoW64(Proceso, Base, ref m))
            throw new Exception("SymGetModuleInfo ha fallado (" + Marshal.GetLastWin32Error() + ")");
        return m;
    }

    public static void Liberar()
    {
        if (iniciado) { SymCleanup(Proceso); iniciado = false; }
    }

    // "funcion+0xDESPL [archivo:linea]" del desplazamiento dentro del modulo, o null si no hay simbolo.
    public static string Nombre(ulong desplazamientoModulo, out string archivo, out uint numLinea)
    {
        archivo = null;
        numLinea = 0;
        const int maxNombre = 2000;
        IntPtr buf = Marshal.AllocHGlobal(88 + maxNombre * 2);
        try
        {
            for (int i = 0; i < 88; i += 4) Marshal.WriteInt32(buf, i, 0);
            Marshal.WriteInt32(buf, 0, 88);          // SYMBOL_INFOW.SizeOfStruct
            Marshal.WriteInt32(buf, 80, maxNombre);  // MaxNameLen
            ulong desp;
            ulong dir = Base + desplazamientoModulo;
            if (!SymFromAddrW(Proceso, dir, out desp, buf))
                return null;
            int largo = Marshal.ReadInt32(buf, 76);
            string nombre = Marshal.PtrToStringUni(new IntPtr(buf.ToInt64() + 84), Math.Min(largo, maxNombre));
            Linea l = new Linea();
            l.SizeOfStruct = (uint)Marshal.SizeOf(typeof(Linea));
            uint dl;
            if (SymGetLineFromAddrW64(Proceso, dir, out dl, ref l))
            {
                archivo = Marshal.PtrToStringUni(l.FileName);
                numLinea = l.LineNumber;
            }
            return nombre + "+0x" + desp.ToString("X");
        }
        finally { Marshal.FreeHGlobal(buf); }
    }
}
'@

# Identidad de un .exe: fecha y tamano del PE y el registro CodeView RSDS (GUID, edad y nombre del PDB).
function Identidad-Exe([string]$ruta) {
    $fs = [IO.File]::Open($ruta, 'Open', 'Read', 'ReadWrite')
    try {
        $br = New-Object IO.BinaryReader($fs)
        $fs.Position = 0x3C; $pe = $br.ReadInt32()
        $fs.Position = $pe
        if ($br.ReadUInt32() -ne 0x4550) { throw "$ruta no es un PE" }
        $fs.Position = $pe + 6; $numSecciones = $br.ReadUInt16()
        $fs.Position = $pe + 8; $fecha = $br.ReadUInt32()
        $fs.Position = $pe + 20; $tamOpcional = $br.ReadUInt16()
        $opt = $pe + 24
        $fs.Position = $opt
        if ($br.ReadUInt16() -ne 0x20B) { throw "$ruta no es un PE de 64 bits" }
        $fs.Position = $opt + 56; $tamImagen = $br.ReadUInt32()
        $fs.Position = $opt + 112 + 6 * 8; $rvaDepuracion = $br.ReadUInt32(); $tamDepuracion = $br.ReadUInt32()
        $secciones = $opt + $tamOpcional
        $r = [ordered]@{ exe = [IO.Path]::GetFileName($ruta); fecha_pe = $fecha.ToString('X8'); tam_imagen = $tamImagen.ToString('X8') }
        for ($i = 0; $i -lt $tamDepuracion / 28; $i++) {
            # La entrada del directorio de depuracion: RVA -> posicion en el archivo por su seccion.
            $rva = $rvaDepuracion + 28 * $i
            $pos = -1
            for ($s = 0; $s -lt $numSecciones; $s++) {
                $fs.Position = $secciones + 40 * $s + 8
                $tamVirtual = $br.ReadUInt32(); $va = $br.ReadUInt32(); $null = $br.ReadUInt32(); $crudo = $br.ReadUInt32()
                if ($rva -ge $va -and $rva -lt $va + $tamVirtual) { $pos = $crudo + ($rva - $va); break }
            }
            if ($pos -lt 0) { break }
            $fs.Position = $pos + 12; $tipo = $br.ReadUInt32(); $tamDatos = $br.ReadUInt32()
            $fs.Position = $pos + 24; $posDatos = $br.ReadUInt32()
            if ($tipo -ne 2 -or $tamDatos -lt 24) { continue }
            $fs.Position = $posDatos
            if ([Text.Encoding]::ASCII.GetString($br.ReadBytes(4)) -ne 'RSDS') { continue }
            $guid = New-Object Guid (, $br.ReadBytes(16))
            $edad = $br.ReadUInt32()
            $nombre = [Text.Encoding]::UTF8.GetString($br.ReadBytes($tamDatos - 24)).TrimEnd([char]0)
            $r.pdb = [IO.Path]::GetFileName($nombre)
            $r.guid = $guid.ToString('N').ToUpper()
            $r.edad = $edad
            break
        }
        return $r
    } finally { $fs.Dispose() }
}

function Identidad-Pdb([string]$ruta, [uint32]$tamImagen = 0) {
    $m = [F1Simbolos]::Cargar($ruta, $tamImagen)
    [ordered]@{ pdb = [IO.Path]::GetFileName($ruta); guid = $m.PdbSig70.ToString('N').ToUpper(); edad = $m.PdbAge
        lineas = $m.LineNumbers }
}

function Texto([System.Collections.IDictionary]$id) {
    (@($id.Keys | Where-Object { $_ -ne 'lineas' } | ForEach-Object { "$_=$($id[$_])" })) -join ' '
}

if ($Identidad) {
    if (-not (Test-Path -LiteralPath $Identidad)) { Write-Error "No existe $Identidad" -ErrorAction Continue; exit 1 }
    $ruta = (Resolve-Path -LiteralPath $Identidad).Path
    if ($ruta -match '\.pdb$') { $id = Identidad-Pdb $ruta; [F1Simbolos]::Liberar() } else { $id = Identidad-Exe $ruta }
    Write-Output (Texto $id)
    exit 0
}

if (-not $Registro) {
    Write-Error "Uso: simbolizar.ps1 -Registro 'ruta\F1-06.log' [-Pdb ruta.pdb] [-Salida ruta] | -Identidad ruta" -ErrorAction Continue
    exit 1
}
if (-not (Test-Path -LiteralPath $Registro)) { Write-Error "No existe $Registro" -ErrorAction Continue; exit 1 }
$lineas = [IO.File]::ReadAllLines((Resolve-Path -LiteralPath $Registro).Path, [Text.Encoding]::UTF8)

# Candidatos: -Pdb, los guardados al empaquetar (identidad.txt dice su GUID y edad) y el de desarrollo.
function Candidatos([string]$nombrePdb, [string]$guid, [string]$edad) {
    foreach ($p in $Pdb) { [IO.Path]::Combine((Get-Location).Path, $p) }
    $carpetaSimbolos = [IO.Path]::Combine($raiz, $Simbolos)
    if (Test-Path $carpetaSimbolos) {
        foreach ($t in Get-ChildItem $carpetaSimbolos -Recurse -Filter 'identidad.txt' -File) {
            $texto = Get-Content -LiteralPath $t.FullName -Raw
            if ($texto -match "guid=$guid\b" -and $texto -match "edad=$edad\b") {
                Get-ChildItem $t.DirectoryName -Filter '*.pdb' -File | ForEach-Object FullName
            }
        }
    }
    Join-Path $raiz ("build\runner-dev\ps2xRuntime\" + $nombrePdb)
}

$cargado = ''     # guid/edad del PDB cargado en dbghelp
$sinPdb = @{}     # guid/edad sin PDB que coincida (ya avisado)
$compilacion = $null
$traducidos = 0
$sinTraducir = 0
$salidaLineas = New-Object System.Collections.Generic.List[string]
$informes = '(?:vigilante|fallo|excepcion|terminate)'
$reCompilacion = '\[' + $informes + '\] compilacion: (?<exe>\S+) .*?tam_imagen=(?<tam>[0-9A-Fa-f]+) pdb=(?<pdb>\S+) guid=(?<guid>[0-9A-Fa-f]{32}) edad=(?<edad>\d+)'
$reMarco = '^(?<pre>.*\[' + $informes + '\]\s+#\d+ )(?<mod>[^\s+]+)\+0x(?<off>[0-9A-Fa-f]+)\s*$'

foreach ($linea in $lineas) {
    if ($linea -match $reCompilacion) {
        $compilacion = @{ exe = $Matches.exe; tam = [Convert]::ToUInt32($Matches.tam, 16); pdb = $Matches.pdb
            guid = $Matches.guid.ToUpper(); edad = $Matches.edad }
        $clave = "guid=$($compilacion.guid) edad=$($compilacion.edad)"
        if ($cargado -ne $clave -and -not $sinPdb.ContainsKey($clave)) {
            $cargado = ''
            $vistos = @{}
            foreach ($c in Candidatos $compilacion.pdb $compilacion.guid $compilacion.edad) {
                if (-not (Test-Path -LiteralPath $c) -or $vistos.ContainsKey($c)) { continue }
                $vistos[$c] = 1
                $id = Identidad-Pdb $c $compilacion.tam
                if ("guid=$($id.guid) edad=$($id.edad)" -eq $clave) {
                    $cargado = $clave
                    $rel = $c; if ($c.StartsWith($raiz + '\')) { $rel = $c.Substring($raiz.Length + 1) }
                    [Console]::Error.WriteLine("[simbolizar] $($compilacion.exe) $clave -> $rel")
                    $salidaLineas.Add("[simbolizar] PDB: $rel ($clave)")
                    break
                }
            }
            if (-not $cargado) {
                [F1Simbolos]::Liberar()
                $sinPdb[$clave] = 1
                $mensaje = "[simbolizar] NINGUN PDB coincide con $($compilacion.exe) pdb=$($compilacion.pdb) $clave " +
                    "(buscado en -Pdb, $Simbolos y build\runner-dev): los marcos se quedan sin traducir"
                [Console]::Error.WriteLine($mensaje)
                $salidaLineas.Add($mensaje)
            }
        }
        $salidaLineas.Add($linea)
        continue
    }
    if ($compilacion -and $linea -match $reMarco -and $Matches.mod -ieq $compilacion.exe) {
        $clave = "guid=$($compilacion.guid) edad=$($compilacion.edad)"
        if ($cargado -eq $clave) {
            $archivo = $null; $num = [uint32]0
            $nombre = [F1Simbolos]::Nombre([Convert]::ToUInt64($Matches.off, 16), [ref]$archivo, [ref]$num)
            if ($nombre) {
                $sufijo = " $nombre"
                if ($archivo) {
                    if ($archivo.StartsWith($raiz + '\', [StringComparison]::OrdinalIgnoreCase)) {
                        $archivo = $archivo.Substring($raiz.Length + 1)
                    }
                    $sufijo += " [${archivo}:$num]"
                }
                $salidaLineas.Add($linea.TrimEnd() + $sufijo)
                $traducidos++
                continue
            }
        }
        $sinTraducir++
    }
    $salidaLineas.Add($linea)
}
[F1Simbolos]::Liberar()

if ($Salida) {
    [IO.File]::WriteAllLines([IO.Path]::Combine((Get-Location).Path, $Salida), $salidaLineas,
        (New-Object Text.UTF8Encoding($false)))
} else {
    $salidaLineas | Write-Output
}
[Console]::Error.WriteLine("[simbolizar] marcos traducidos: $traducidos; sin traducir: $sinTraducir")
if ($sinPdb.Count) { exit 2 }
exit 0
