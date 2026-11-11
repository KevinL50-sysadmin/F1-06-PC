# Copia el texto que queda en el bufer de la consola de un proceso en marcha (sin pararlo ni tocarlo) a un archivo.
# Sirve cuando el juego se abrio sin redirigir la salida: solo recupera lo que la consola aun guarda (las ultimas
# lineas que caben en su bufer), no todo lo que se escribio desde el arranque.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\leer-consola.ps1 -Proceso F1-06 -Salida logs\x.log
param(
    [string]$Proceso = 'F1-06',
    [Parameter(Mandatory = $true)][string]$Salida
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ConsolaAjena {
    [StructLayout(LayoutKind.Sequential)] public struct COORD { public short X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct SMALL_RECT { public short Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct CSBI {
        public COORD dwSize; public COORD dwCursorPosition; public short wAttributes;
        public SMALL_RECT srWindow; public COORD dwMaximumWindowSize; }
    [DllImport("kernel32.dll")] public static extern bool FreeConsole();
    [DllImport("kernel32.dll")] public static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sa, uint disp, uint flags, IntPtr tpl);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out CSBI info);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern bool ReadConsoleOutputCharacterW(IntPtr h, StringBuilder buf, uint len, COORD at, out uint read);
    public static string Leer(uint pid) {
        FreeConsole();
        if (!AttachConsole(pid)) throw new Exception("AttachConsole fallo (" + Marshal.GetLastWin32Error() + ")");
        try {
            IntPtr h = CreateFileW("CONOUT$", 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
            if (h == new IntPtr(-1)) throw new Exception("CONOUT$ no se abre");
            try {
                CSBI info; GetConsoleScreenBufferInfo(h, out info);
                int ancho = info.dwSize.X, alto = info.dwCursorPosition.Y + 1;
                var texto = new StringBuilder();
                var linea = new StringBuilder(ancho);
                for (short y = 0; y < alto; y++) {
                    linea.Clear(); linea.EnsureCapacity(ancho); uint leidos;
                    COORD c; c.X = 0; c.Y = y;
                    ReadConsoleOutputCharacterW(h, linea, (uint)ancho, c, out leidos);
                    texto.AppendLine(linea.ToString(0, (int)Math.Min(leidos, (uint)linea.Length)).TrimEnd());
                }
                return texto.ToString();
            } finally { CloseHandle(h); }
        } finally { FreeConsole(); }
    }
}
'@
$p = Get-Process -Name $Proceso | Select-Object -First 1
$texto = [ConsolaAjena]::Leer([uint32]$p.Id)
$ruta = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) $Salida))
[System.IO.File]::WriteAllText($ruta, $texto)
"{0} lineas en {1}" -f ($texto -split "`n").Count, $ruta
