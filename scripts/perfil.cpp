// Perfilador por muestreo de un proceso en marcha (con símbolos del PDB).
// Uso: perfil.exe <pid> [muestras=2000] [intervalo_ms=5] [hilo=0 (el que más CPU gasta) o nombre del hilo]
// Suspende el hilo, guarda su pila (direcciones) y lo reanuda; al final resuelve los símbolos y muestra:
//  - exclusivo: función en lo alto de la pila;
//  - inclusivo: funciones presentes en la pila (una vez por muestra);
//  - líneas más calientes (exclusivo).
// Compilar (tras entorno.cmd): cl /nologo /std:c++17 /EHsc /O2 scripts\perfil.cpp /Fo:build\perfil.obj /Fe:build\perfil.exe dbghelp.lib winmm.lib
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

static double cpuHilo(DWORD tid)
{
    HANDLE hilo = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!hilo)
        return 0.0;
    FILETIME c, e, k, u;
    GetThreadTimes(hilo, &c, &e, &k, &u);
    CloseHandle(hilo);
    return (static_cast<double>(u.dwLowDateTime) + static_cast<double>(u.dwHighDateTime) * 4294967296.0) / 1e7;
}

static std::vector<DWORD> hilosDe(DWORD pid)
{
    std::vector<DWORD> hilos;
    HANDLE instantanea = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(instantanea, &te); ok; ok = Thread32Next(instantanea, &te))
        if (te.th32OwnerProcessID == pid)
            hilos.push_back(te.th32ThreadID);
    CloseHandle(instantanea);
    return hilos;
}

struct Simbolo
{
    std::string nombre;
    DWORD64 inicio = 0;
};

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Uso: perfil.exe <pid> [muestras] [intervalo_ms] [hilo]\n");
        return 1;
    }
    const DWORD pid = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 0));
    const int muestras = argc > 2 ? std::atoi(argv[2]) : 2000;
    const int intervalo = argc > 3 ? std::atoi(argv[3]) : 5;
    DWORD tid = argc > 4 ? static_cast<DWORD>(std::strtoul(argv[4], nullptr, 0)) : 0u;
    // El hilo tambien se puede dar por su nombre (SetThreadDescription), p. ej. "GS raster".
    if (argc > 4 && tid == 0u)
    {
        wchar_t buscado[128] = {};
        MultiByteToWideChar(CP_UTF8, 0, argv[4], -1, buscado, 127);
        for (DWORD h : hilosDe(pid))
        {
            HANDLE hilo = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, h);
            if (!hilo)
                continue;
            PWSTR nombre = nullptr;
            if (SUCCEEDED(GetThreadDescription(hilo, &nombre)) && nombre)
            {
                if (std::wcsstr(nombre, buscado))
                    tid = h;
                LocalFree(nombre);
            }
            CloseHandle(hilo);
            if (tid != 0u)
                break;
        }
        if (tid == 0u)
        {
            std::fprintf(stderr, "No hay ningun hilo llamado \"%s\"\n", argv[4]);
            return 1;
        }
        std::printf("Se muestrea el hilo %lu (%s)\n", tid, argv[4]);
    }

    HANDLE proceso = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proceso)
    {
        std::fprintf(stderr, "No se puede abrir el proceso %lu\n", pid);
        return 1;
    }

    if (tid == 0u)
    {
        // El hilo que más CPU gasta durante un segundo.
        std::vector<DWORD> hilos = hilosDe(pid);
        std::vector<double> antes;
        for (DWORD h : hilos)
            antes.push_back(cpuHilo(h));
        Sleep(1000);
        double mejor = -1.0;
        for (size_t i = 0; i < hilos.size(); ++i)
        {
            const double d = cpuHilo(hilos[i]) - antes[i];
            if (d > 0.05)
                std::printf("hilo %lu: %.0f %% de CPU\n", hilos[i], d * 100.0);
            if (d > mejor)
            {
                mejor = d;
                tid = hilos[i];
            }
        }
        std::printf("Se muestrea el hilo %lu\n", tid);
    }

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proceso, nullptr, TRUE);

    HANDLE hilo = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!hilo)
    {
        std::fprintf(stderr, "No se puede abrir el hilo %lu\n", tid);
        return 1;
    }

    std::vector<std::vector<DWORD64>> pilas;
    pilas.reserve(static_cast<size_t>(muestras));
    timeBeginPeriod(1);
    for (int n = 0; n < muestras; ++n)
    {
        Sleep(static_cast<DWORD>(intervalo));
        if (SuspendThread(hilo) == static_cast<DWORD>(-1))
            break;
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        std::vector<DWORD64> pila;
        if (GetThreadContext(hilo, &ctx))
        {
            STACKFRAME64 marco{};
            marco.AddrPC.Offset = ctx.Rip;
            marco.AddrPC.Mode = AddrModeFlat;
            marco.AddrFrame.Offset = ctx.Rbp;
            marco.AddrFrame.Mode = AddrModeFlat;
            marco.AddrStack.Offset = ctx.Rsp;
            marco.AddrStack.Mode = AddrModeFlat;
            for (int i = 0; i < 48; ++i)
            {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proceso, hilo, &marco, &ctx, nullptr,
                                 SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                    break;
                if (marco.AddrPC.Offset == 0)
                    break;
                pila.push_back(marco.AddrPC.Offset);
            }
        }
        ResumeThread(hilo);
        if (!pila.empty())
            pilas.push_back(std::move(pila));
    }
    timeEndPeriod(1);

    // Resolución de símbolos con caché.
    std::unordered_map<DWORD64, Simbolo> cache;
    auto simbolo = [&](DWORD64 dir) -> const Simbolo &
    {
        auto it = cache.find(dir);
        if (it != cache.end())
            return it->second;
        Simbolo s;
        char buf[sizeof(SYMBOL_INFO) + 512]{};
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 desp = 0;
        if (SymFromAddr(proceso, dir, &desp, sym))
        {
            s.nombre = sym->Name;
            s.inicio = dir - desp;
        }
        else
        {
            // Sin simbolo (codigo generado, p. ej. el JIT de la VU1): todo en una entrada, para ver cuanto suma; la
            // direccion queda como desplazamiento en "Direcciones".
            s.nombre = "[sin simbolo]";
            s.inicio = 0;
        }
        return cache.emplace(dir, s).first->second;
    };

    std::map<std::string, int> exclusivo;
    std::map<std::string, int> inclusivo;
    std::map<std::string, int> lineas;
    std::map<std::string, int> lineasInternas; // la linea mas interna del codigo en linea (SymQueryInlineTrace)
    std::map<std::string, int> lineasMarcos;   // las lineas de todos los marcos en linea (inclusivo)
    std::map<std::string, int> numMarcos;
    std::map<std::string, int> llamadorDeHoja; // "hoja <- llamador"
    std::map<std::string, int> direcciones;    // "funcion+0xdesp" de la hoja (para cruzar con dumpbin /disasm)
    for (const auto &pila : pilas)
    {
        const Simbolo &hoja = simbolo(pila[0]);
        ++exclusivo[hoja.nombre];
        std::set<std::string> vistos;
        for (DWORD64 dir : pila)
        {
            const Simbolo &s = simbolo(dir);
            if (vistos.insert(s.nombre).second)
                ++inclusivo[s.nombre];
        }
        {
            char tmp[600];
            std::snprintf(tmp, sizeof(tmp), "%s+0x%llx", hoja.nombre.c_str(),
                          static_cast<unsigned long long>(pila[0] - hoja.inicio));
            ++direcciones[tmp];
        }
        if (pila.size() > 1)
            ++llamadorDeHoja[hoja.nombre + "  <-  " + simbolo(pila[1]).nombre];
        IMAGEHLP_LINE64 linea{};
        linea.SizeOfStruct = sizeof(linea);
        DWORD despLinea = 0;
        if (SymGetLineFromAddr64(proceso, pila[0], &despLinea, &linea))
        {
            const char *archivo = linea.FileName;
            if (const char *barra = std::strrchr(archivo, '\\'))
                archivo = barra + 1;
            char tmp[600];
            std::snprintf(tmp, sizeof(tmp), "%s:%lu  (%s)", archivo, linea.LineNumber, hoja.nombre.c_str());
            ++lineas[tmp];
        }
        // Codigo en linea: la linea y la funcion de cada marco en linea (contexto + k, del mas interno hacia fuera):
        // el mas interno en lineasInternas y todos, una vez por muestra, en lineasMarcos.
        const DWORD marcos = SymAddrIncludeInlineTrace(proceso, pila[0]);
        {
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%lu marcos en linea", marcos);
            ++numMarcos[tmp];
        }
        DWORD contexto = 0, indice = 0;
        if (marcos > 0 && SymQueryInlineTrace(proceso, pila[0], 0, pila[0], pila[0], &contexto, &indice))
        {
            std::set<std::string> vistosLinea;
            for (DWORD k = 0; k < marcos; ++k)
            {
                IMAGEHLP_LINE64 li{};
                li.SizeOfStruct = sizeof(li);
                DWORD dl = 0;
                char buf[sizeof(SYMBOL_INFO) + 512]{};
                auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = 511;
                DWORD64 ds = 0;
                const char *funcion = SymFromInlineContext(proceso, pila[0], contexto + k, &ds, sym) ? sym->Name : "?";
                if (!SymGetLineFromInlineContext(proceso, pila[0], contexto + k, 0, &dl, &li))
                    continue;
                const char *archivo = li.FileName;
                if (const char *barra = std::strrchr(archivo, '\\'))
                    archivo = barra + 1;
                char tmp[700];
                std::snprintf(tmp, sizeof(tmp), "%s:%lu  (%s)", archivo, li.LineNumber, funcion);
                if (k == 0)
                    ++lineasInternas[tmp];
                if (vistosLinea.insert(tmp).second)
                    ++lineasMarcos[tmp];
            }
        }
    }

    const double total = static_cast<double>(pilas.size());
    auto mostrar = [&](const char *titulo, const std::map<std::string, int> &tabla, size_t maximo)
    {
        std::vector<std::pair<int, std::string>> orden;
        for (const auto &[k, v] : tabla)
            orden.emplace_back(v, k);
        std::sort(orden.begin(), orden.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::printf("\n=== %s (%zu muestras)\n", titulo, pilas.size());
        for (size_t i = 0; i < orden.size() && i < maximo; ++i)
            std::printf("%6.2f %%  %5d  %s\n", 100.0 * orden[i].first / total, orden[i].first, orden[i].second.c_str());
    };
    mostrar("Exclusivo", exclusivo, 45);
    mostrar("Inclusivo", inclusivo, 45);
    mostrar("Hoja <- llamador", llamadorDeHoja, 30);
    mostrar("Lineas (exclusivo)", lineas, 60);
    mostrar("Lineas en linea (exclusivo; el marco en linea mas interno)", lineasInternas, 80);
    mostrar("Lineas en linea (inclusivo; todos los marcos en linea)", lineasMarcos, 120);
    mostrar("Marcos en linea por muestra", numMarcos, 12);
    mostrar("Direcciones (exclusivo; desplazamiento dentro de la funcion, como en dumpbin /disasm del .obj)",
            direcciones, 80);

    // PERFIL_PILAS=<texto>: las pilas completas (de la hoja hacia arriba, hasta 14 marcos) de las muestras con una
    // funcion cuyo nombre contiene ese texto, agrupadas y las 12 mas frecuentes.
    // Varios textos separados por '|': una tabla por texto.
    std::vector<std::string> textosPilas;
    if (const char *lista = std::getenv("PERFIL_PILAS"); lista != nullptr)
    {
        std::string resto = lista;
        for (size_t corte; (corte = resto.find('|')) != std::string::npos; resto = resto.substr(corte + 1))
            textosPilas.push_back(resto.substr(0, corte));
        textosPilas.push_back(resto);
    }
    for (const std::string &textoPila : textosPilas)
    {
        const char *buscar = textoPila.c_str();
        if (buscar[0] == '\0')
            continue;
        std::map<std::string, int> pilasCon;
        for (const auto &pila : pilas)
        {
            bool tiene = false;
            for (DWORD64 dir : pila)
                if (simbolo(dir).nombre.find(buscar) != std::string::npos)
                    tiene = true;
            if (!tiene)
                continue;
            std::string texto;
            for (size_t i = 0; i < pila.size() && i < 14u; ++i)
                texto += "\n           " + simbolo(pila[i]).nombre.substr(0, 150);
            ++pilasCon[texto];
        }
        mostrar((std::string("Pilas con ") + buscar).c_str(), pilasCon, 12);
    }

    // PERFIL_CRUDO=<archivo>: cada direccion sin simbolo en lo alto de la pila con sus muestras ("<dir hex> <n>"), para
    // repartirlas por pares de la VU con el mapa del JIT (PS2X_JIT_MAPA, scripts/jit_mapa.py).
    if (const char *crudo = std::getenv("PERFIL_CRUDO"); crudo != nullptr && crudo[0] != 0)
    {
        std::map<DWORD64, int> sinSimbolo;
        for (const auto &pila : pilas)
            if (simbolo(pila[0]).inicio == 0)
                ++sinSimbolo[pila[0]];
        if (FILE *f = std::fopen(crudo, "w"))
        {
            std::fprintf(f, "# %zu muestras\n", pilas.size());
            for (const auto &[dir, n] : sinSimbolo)
                std::fprintf(f, "%llx %d\n", static_cast<unsigned long long>(dir), n);
            std::fclose(f);
        }
    }

    CloseHandle(hilo);
    SymCleanup(proceso);
    CloseHandle(proceso);
    return 0;
}
