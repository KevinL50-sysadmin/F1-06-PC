// Muestra la pila (con símbolos del PDB) de los hilos de un proceso en marcha.
// Uso: pila.exe <pid> [hilo]   (sin hilo: todos los hilos con algo de CPU)
// Compilar (tras entorno.cmd): cl /nologo /EHsc /O2 scripts\pila.cpp /Fe:build\pila.exe dbghelp.lib
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void volcarHilo(HANDLE proceso, DWORD tid)
{
    HANDLE hilo = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!hilo)
        return;
    FILETIME c, e, k, u;
    GetThreadTimes(hilo, &c, &e, &k, &u);
    const double cpu = (static_cast<double>(u.dwLowDateTime) + static_cast<double>(u.dwHighDateTime) * 4294967296.0) / 1e7;
    if (SuspendThread(hilo) == static_cast<DWORD>(-1))
    {
        CloseHandle(hilo);
        return;
    }
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(hilo, &ctx))
    {
        std::printf("=== hilo %lu (cpu usuario %.2f s)\n", tid, cpu);
        STACKFRAME64 marco{};
        marco.AddrPC.Offset = ctx.Rip;
        marco.AddrPC.Mode = AddrModeFlat;
        marco.AddrFrame.Offset = ctx.Rbp;
        marco.AddrFrame.Mode = AddrModeFlat;
        marco.AddrStack.Offset = ctx.Rsp;
        marco.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 40; ++i)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proceso, hilo, &marco, &ctx, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
            if (marco.AddrPC.Offset == 0)
                break;
            char buf[sizeof(SYMBOL_INFO) + 512]{};
            auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 511;
            DWORD64 desp = 0;
            IMAGEHLP_LINE64 linea{};
            linea.SizeOfStruct = sizeof(linea);
            DWORD despLinea = 0;
            const bool hayLinea = SymGetLineFromAddr64(proceso, marco.AddrPC.Offset, &despLinea, &linea) != FALSE;
            if (SymFromAddr(proceso, marco.AddrPC.Offset, &desp, sym))
                std::printf("  %2d %s+0x%llx", i, sym->Name, static_cast<unsigned long long>(desp));
            else
                std::printf("  %2d 0x%llx", i, static_cast<unsigned long long>(marco.AddrPC.Offset));
            if (hayLinea)
                std::printf("  (%s:%lu)", linea.FileName, linea.LineNumber);
            std::printf("\n");
        }
    }
    ResumeThread(hilo);
    CloseHandle(hilo);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Uso: pila.exe <pid> [hilo]\n");
        return 1;
    }
    const DWORD pid = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 0));
    const DWORD soloHilo = argc > 2 ? static_cast<DWORD>(std::strtoul(argv[2], nullptr, 0)) : 0u;
    HANDLE proceso = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proceso)
    {
        std::fprintf(stderr, "No se puede abrir el proceso %lu\n", pid);
        return 1;
    }
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proceso, nullptr, TRUE);

    HANDLE instantanea = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    std::vector<DWORD> hilos;
    for (BOOL ok = Thread32First(instantanea, &te); ok; ok = Thread32Next(instantanea, &te))
        if (te.th32OwnerProcessID == pid && (soloHilo == 0u || te.th32ThreadID == soloHilo))
            hilos.push_back(te.th32ThreadID);
    CloseHandle(instantanea);

    for (DWORD tid : hilos)
        volcarHilo(proceso, tid);
    SymCleanup(proceso);
    CloseHandle(proceso);
    return 0;
}
