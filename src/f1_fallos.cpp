// Informe de fallos del port de F1 06: si el proceso se cae (excepcion sin manejar), escribe en el registro el codigo,
// la direccion y el informe de ps2_fallo.h ("[fallo]": identidad de la compilacion, hilo, pc y ra del EE y la pila como
// modulo+0xdesp, que scripts/simbolizar.ps1 traduce con el PDB guardado de esa version; odd/tasks/fallos-con-log.md
// T3b). Como cada funcion del juego es una funcion del host (sub_XXXXXX), la pila sirve tambien de pila del EE. El
// filtro devuelve EXCEPTION_CONTINUE_SEARCH: Windows termina el proceso con el codigo de la excepcion (p. ej.
// 0xC0000005), que RecompLauncher ve como fallo. El runner se compila por lotes (unity) junto a archivos que incluyen
// raylib, que choca con windows.h: por eso las pocas declaraciones de Win32 que hacen falta van aqui.
// Tambien, por lo mismo, lo de los nucleos del procesador (f1EnNucleoEficiente, f1NucleosDeRendimiento).
#include "ps2_fallo.h"
#include "ps2_salida.h" // aLaConsola (lanzamiento-1.1.4 S2)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <stdexcept>
#include <thread>

extern "C"
{
    __declspec(dllimport) void *__stdcall SetUnhandledExceptionFilter(void *filtro);
    __declspec(dllimport) void *__stdcall GetCurrentProcess();
    __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
    __declspec(dllimport) void *__stdcall GetCurrentThread();
    __declspec(dllimport) void *__stdcall GetModuleHandleW(const wchar_t *modulo);
    __declspec(dllimport) void *__stdcall GetProcAddress(void *modulo, const char *nombre);
    __declspec(dllimport) unsigned long __stdcall GetCurrentProcessorNumber();
    __declspec(dllimport) int __stdcall GetSystemCpuSetInformation(void *info, unsigned long largo,
                                                                   unsigned long *devuelto, void *proceso,
                                                                   unsigned long banderas);
    __declspec(dllimport) int __stdcall SetThreadSelectedCpuSets(void *hilo, const unsigned long *ids,
                                                                 unsigned long cuantos);
    __declspec(dllimport) int __stdcall SetProcessInformation(void *proceso, int clase, void *info,
                                                              unsigned long largo);
    __declspec(dllimport) int __stdcall SetThreadInformation(void *hilo, int clase, void *info, unsigned long largo);
    __declspec(dllimport) int __stdcall GetThreadTimes(void *hilo, uint64_t *creado, uint64_t *salido, uint64_t *nucleo,
                                                       uint64_t *usuario);
}

// Nucleos del procesador. El PC de pruebas es un i7-13700 (8 nucleos de rendimiento con dos hilos cada uno y 8
// eficientes): con la ventana oculta (pruebas en segundo plano) Windows puede tratar el proceso como de fondo
// (EcoQoS) y llevar el hilo del EE a un nucleo eficiente, mucho mas lento.
namespace
{
    struct FalloNucleos
    {
        uint8_t clase[256] = {}; // EfficiencyClass por procesador logico (grupo 0)
        uint8_t claseMax = 0;
        unsigned long rendimiento[256] = {}; // ids de los conjuntos de CPU de la clase mas alta
        unsigned long cuantos = 0;
        bool hibrido = false;
    };

    const FalloNucleos &falloNucleos()
    {
        static const FalloNucleos s_nucleos = []
        {
            FalloNucleos n;
            alignas(8) static uint8_t memoria[64 * 1024];
            unsigned long largo = 0;
            if (GetSystemCpuSetInformation(memoria, sizeof(memoria), &largo, GetCurrentProcess(), 0) == 0)
                return n;
            // SYSTEM_CPU_SET_INFORMATION: Size (4), Type (4), Id (4), Group (2), LogicalProcessorIndex (1),
            // CoreIndex (1), LastLevelCacheIndex (1), NumaNodeIndex (1), EfficiencyClass (1)...
            uint8_t claseMin = 255;
            for (unsigned long pos = 0; pos + 24 <= largo;)
            {
                uint32_t tam = 0, id = 0;
                uint16_t grupo = 0;
                std::memcpy(&tam, memoria + pos, 4);
                std::memcpy(&id, memoria + pos + 8, 4);
                std::memcpy(&grupo, memoria + pos + 12, 2);
                const uint8_t logico = memoria[pos + 14];
                const uint8_t clase = memoria[pos + 18];
                if (grupo == 0)
                    n.clase[logico] = clase;
                n.claseMax = clase > n.claseMax ? clase : n.claseMax;
                claseMin = clase < claseMin ? clase : claseMin;
                if (tam == 0)
                    break;
                pos += tam;
            }
            n.hibrido = claseMin != n.claseMax;
            for (unsigned long pos = 0; pos + 24 <= largo;)
            {
                uint32_t tam = 0, id = 0;
                std::memcpy(&tam, memoria + pos, 4);
                std::memcpy(&id, memoria + pos + 8, 4);
                if (memoria[pos + 18] == n.claseMax && n.cuantos < 256)
                    n.rendimiento[n.cuantos++] = id;
                if (tam == 0)
                    break;
                pos += tam;
            }
            return n;
        }();
        return s_nucleos;
    }
}

// El hilo que llama esta ahora en un nucleo eficiente (solo en procesadores hibridos).
bool f1EnNucleoEficiente()
{
    const FalloNucleos &n = falloNucleos();
    return n.hibrido && n.clase[GetCurrentProcessorNumber() & 255u] != n.claseMax;
}

// rendimiento = true: el proceso sin regulacion de energia (ni EcoQoS) y el hilo que llama solo en los nucleos de
// rendimiento. false: lo que decida Windows (para la comparacion A/B).
void f1NucleosDeRendimiento(bool rendimiento)
{
    const FalloNucleos &n = falloNucleos();
    // PROCESS_POWER_THROTTLING_STATE / THREAD_POWER_THROTTLING_STATE: Version 1, ControlMask, StateMask. Con la
    // velocidad de ejecucion (1) en ControlMask y 0 en StateMask, Windows no la regula; con ControlMask 0, decide el.
    unsigned long estado[3] = {1u, rendimiento ? 1u : 0u, 0u};
    SetProcessInformation(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, estado, sizeof(estado));
    SetThreadInformation(GetCurrentThread(), 3 /* ThreadPowerThrottling */, estado, sizeof(estado));
    if (n.hibrido)
        SetThreadSelectedCpuSets(GetCurrentThread(), rendimiento ? n.rendimiento : nullptr, rendimiento ? n.cuantos : 0u);
}

// Diagnostico de nucleos de cada ventana A/B (parche 0069), medido en el hilo del EE: tiempo de CPU del hilo en
// unidades de 100 ns (GetThreadTimes; QueryThreadCycleTime no cuenta en ciclos del TSC en este PC), el TSC, y una cadena de 2.000 multiplicaciones dependientes cada 16
// paquetes (siempre los mismos ciclos del nucleo: su tiempo en ciclos del TSC da la frecuencia real del nucleo) y los
// procesadores logicos aparcados por Windows.
uint64_t f1CpuHilo()
{
    uint64_t creado = 0, salido = 0, nucleo = 0, usuario = 0;
    GetThreadTimes(GetCurrentThread(), &creado, &salido, &nucleo, &usuario);
    return nucleo + usuario;
}

uint64_t f1Tsc()
{
    return __rdtsc();
}

// Ciclos del TSC que tarda la cadena (cada vuelta: una multiplicacion y una suma dependientes, 4 ciclos del nucleo).
uint64_t f1MedirCadena()
{
    static volatile uint64_t s_factor = 0x2545F4914F6CDD1Dull;
    static volatile uint64_t s_sumidero = 0;
    const uint64_t factor = s_factor;
    uint64_t x = s_sumidero | 1u;
    const uint64_t inicio = __rdtsc();
    for (uint64_t i = 0; i < 2000u; ++i)
        x = x * factor + i;
    const uint64_t fin = __rdtsc();
    s_sumidero = x;
    return fin - inicio;
}

// Procesadores logicos aparcados ahora (bandera Parked de SYSTEM_CPU_SET_INFORMATION), y la primera vez la lista de
// los conjuntos de CPU en stderr.
unsigned f1NucleosAparcados()
{
    alignas(8) static uint8_t memoria[64 * 1024];
    static bool s_listados = false;
    unsigned long largo = 0;
    if (GetSystemCpuSetInformation(memoria, sizeof(memoria), &largo, GetCurrentProcess(), 0) == 0)
        return 0;
    unsigned aparcados = 0;
    for (unsigned long pos = 0; pos + 24 <= largo;)
    {
        uint32_t tam = 0, id = 0;
        std::memcpy(&tam, memoria + pos, 4);
        std::memcpy(&id, memoria + pos + 8, 4);
        if ((memoria[pos + 19] & 1u) != 0)
            ++aparcados;
        if (!s_listados)
            std::fprintf(stderr, "[f1-06:nucleos] id=%u logico=%u nucleo=%u clase=%u banderas=0x%02x planificacion=%u\n",
                         id, memoria[pos + 14], memoria[pos + 15], memoria[pos + 18], memoria[pos + 19],
                         memoria[pos + 20]);
        if (tam == 0)
            break;
        pos += tam;
    }
    s_listados = true;
    return aparcados;
}

namespace
{
    // EXCEPTION_RECORD (x64) con lo que se usa.
    struct FalloRegistro
    {
        unsigned long ExceptionCode;
        unsigned long ExceptionFlags;
        FalloRegistro *ExceptionRecord;
        void *ExceptionAddress;
        unsigned long NumberParameters;
        uintptr_t ExceptionInformation[15];
    };
    struct FalloPunteros
    {
        FalloRegistro *ExceptionRecord;
        uint8_t *ContextRecord; // CONTEXT: Rsp en +0x98, Rip en +0xF8
    };

    long __stdcall falloInformar(FalloPunteros *info)
    {
        static volatile long s_dentro = 0;
        if (s_dentro++ != 0)
            return 0; // EXCEPTION_CONTINUE_SEARCH
        const FalloRegistro *r = info->ExceptionRecord;
        uint64_t rsp = 0, rip = 0;
        std::memcpy(&rsp, info->ContextRecord + 0x98, 8);
        std::memcpy(&rip, info->ContextRecord + 0xF8, 8);
        // La primera linea, tambien en la consola de antes del registro (lanzamiento-1.1.4 S2): sin reservar memoria.
        char linea[192];
        int largo = std::snprintf(linea, sizeof(linea), "[fallo] excepcion 0x%08lx en 0x%p (hilo %lu)",
                                  r->ExceptionCode, r->ExceptionAddress, GetCurrentThreadId());
        if (largo < 0)
            largo = 0;
        if (r->ExceptionCode == 0xC0000005ul && r->NumberParameters >= 2 && largo < static_cast<int>(sizeof(linea)))
        {
            const int mas = std::snprintf(linea + largo, sizeof(linea) - largo, ", %s 0x%llx",
                                          r->ExceptionInformation[0] == 8   ? "ejecutar"
                                          : r->ExceptionInformation[0] == 1 ? "escribir"
                                                                            : "leer",
                                          static_cast<unsigned long long>(r->ExceptionInformation[1]));
            largo += mas > 0 ? mas : 0;
        }
        if (largo > static_cast<int>(sizeof(linea)) - 2)
            largo = static_cast<int>(sizeof(linea)) - 2;
        linea[largo++] = '\n';
        linea[largo] = '\0';
        std::fputs(linea, stderr);
        ps2x::salida::aLaConsola(linea, static_cast<size_t>(largo));
        // Desde aqui, la pila pasa por el despachador de excepciones y sigue en el punto del fallo. Con un salto a la
        // direccion 0 no hay marco del fallo: el #0 es entonces quien llamo (la vuelta en la cima de la pila).
        void *pila[64] = {};
        int n = 0;
        const bool saltoACero = rip == 0 && rsp != 0;
        if (saltoACero)
            std::memcpy(&pila[n++], reinterpret_cast<const void *>(rsp), 8);
        n += ps2x::fallo::pilaActual(pila + n, 64 - n, 0);
        ps2x::fallo::escribirInforme("[fallo] ",
                                     saltoACero ? "pila (salto a 0: #0 es quien llamo; luego el despachador de excepciones)"
                                                : "pila (incluye el despachador de excepciones)",
                                     pila, n);
        std::fflush(stderr);
        std::fflush(stdout);
        return 0;
    }

    const bool s_falloInstalado = []
    {
        SetUnhandledExceptionFilter(reinterpret_cast<void *>(&falloInformar));
        return true;
    }();
}

// PS2X_PRUEBA_FALLO (ps2_fallo.h, solo para las pruebas de T4): el fallo de cada tipo, aparte (no en linea) para que
// salga con su nombre en la pila. acceso: escritura en la direccion 0 en el hilo del juego (el informe "[fallo]").
__declspec(noinline) void f1FalloDeAcceso()
{
    volatile int *volatile nulo = nullptr;
    std::fprintf(stderr, "[fallo] prueba: escritura en la direccion 0 a proposito (PS2X_PRUEBA_FALLO=acceso)\n");
    *nulo = 0x0F106;
}

// terminate: std::runtime_error en un hilo auxiliar "PruebaFallo" que nadie recoge (std::terminate, "[terminate]").
__declspec(noinline) void f1FalloEnHiloAuxiliar()
{
    std::thread([]
                {
                    using Nombrar = long(__stdcall *)(void *, const wchar_t *); // SetThreadDescription (Windows 10 1607)
                    if (const auto nombrar = reinterpret_cast<Nombrar>(
                            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")))
                        nombrar(GetCurrentThread(), L"PruebaFallo");
                    throw std::runtime_error("prueba: excepcion en un hilo auxiliar (PS2X_PRUEBA_FALLO=terminate)");
                })
        .detach();
}
