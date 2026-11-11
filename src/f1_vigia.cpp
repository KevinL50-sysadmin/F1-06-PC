// Vigia de escrituras en la RAM del EE con los registros de depuracion de x64 (diagnostico, solo Windows).
// Como en f1_fallos.cpp, sin windows.h (el lote unity incluye raylib, que choca con el): las pocas declaraciones de
// Win32 van aqui y el CONTEXT de x64 se toca por sus desplazamientos.
//
// f1vigia::registrarHiloEe() se llama desde el hilo del EE (GameThread); f1vigia::vigilar(direccion, avisar), desde
// otro hilo, suspende el del EE, arma DR0 sobre 8 bytes de esa direccion del anfitrion y lo reanuda. Cada escritura
// del hilo del EE en esos 8 bytes llama a avisar(direccion) dentro del manejador de excepciones, justo despues de la
// instruccion que escribio.

#include <atomic>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
extern "C"
{
    __declspec(dllimport) void *__stdcall GetCurrentProcess();
    __declspec(dllimport) void *__stdcall GetCurrentThread();
    __declspec(dllimport) int __stdcall DuplicateHandle(void *procesoOrigen, void *origen, void *procesoDestino,
                                                        void **destino, unsigned long acceso, int heredar,
                                                        unsigned long opciones);
    __declspec(dllimport) unsigned long __stdcall SuspendThread(void *hilo);
    __declspec(dllimport) unsigned long __stdcall ResumeThread(void *hilo);
    __declspec(dllimport) int __stdcall GetThreadContext(void *hilo, void *contexto);
    __declspec(dllimport) int __stdcall SetThreadContext(void *hilo, void *contexto);
    __declspec(dllimport) void *__stdcall AddVectoredExceptionHandler(unsigned long primero,
                                                                      long(__stdcall *manejador)(void *));
}
#endif

namespace f1vigia
{
    using Aviso = void (*)(uintptr_t direccion);

#ifdef _WIN32
    namespace
    {
        // CONTEXT de x64 (winnt.h): 0x4D0 bytes alineados a 16; ContextFlags en 0x30, Dr0 en 0x48, Dr6 en 0x68,
        // Dr7 en 0x70.
        constexpr size_t kTamContexto = 0x4D0u;
        constexpr size_t kFlags = 0x30u, kDr0 = 0x48u, kDr6 = 0x68u, kDr7 = 0x70u;
        constexpr uint32_t kContextDebugRegisters = 0x00100010u;
        constexpr uint32_t kExcepcionPasoAPaso = 0x80000004u; // EXCEPTION_SINGLE_STEP
        constexpr unsigned long kAccesoHilo = 0x0002u | 0x0008u | 0x0010u | 0x0040u; // SUSPEND_RESUME, GET/SET_CONTEXT, QUERY_INFORMATION

        void *g_hiloEe = nullptr;
        std::atomic<Aviso> g_aviso{nullptr};
        void *g_manejador = nullptr;

        uint64_t leer64(const uint8_t *p, size_t o)
        {
            uint64_t v;
            std::memcpy(&v, p + o, 8);
            return v;
        }

        void escribir64(uint8_t *p, size_t o, uint64_t v)
        {
            std::memcpy(p + o, &v, 8);
        }

        long __stdcall manejador(void *infoVoid)
        {
            void *const *info = static_cast<void *const *>(infoVoid); // EXCEPTION_POINTERS
            uint32_t codigo;
            std::memcpy(&codigo, info[0], 4);
            if (codigo != kExcepcionPasoAPaso)
                return 0; // EXCEPTION_CONTINUE_SEARCH
            uint8_t *c = static_cast<uint8_t *>(info[1]);
            if ((leer64(c, kDr6) & 0x1u) == 0u)
                return 0;
            const uintptr_t direccion = static_cast<uintptr_t>(leer64(c, kDr0));
            escribir64(c, kDr6, 0u);
            if (Aviso aviso = g_aviso.load(std::memory_order_acquire))
                aviso(direccion);
            return -1; // EXCEPTION_CONTINUE_EXECUTION
        }
    }

    void registrarHiloEe()
    {
        if (g_hiloEe != nullptr)
            return;
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_hiloEe, kAccesoHilo, 0, 0);
    }

    bool listo()
    {
        return g_hiloEe != nullptr;
    }

    // direccion = 0: desarma. Hay que llamarla desde un hilo que no sea el del EE.
    bool vigilar(uintptr_t direccion, Aviso aviso)
    {
        if (g_hiloEe == nullptr)
            return false;
        if (g_manejador == nullptr)
            g_manejador = AddVectoredExceptionHandler(1, manejador);
        g_aviso.store(aviso, std::memory_order_release);
        if (SuspendThread(g_hiloEe) == static_cast<unsigned long>(-1))
            return false;
        alignas(16) uint8_t c[kTamContexto] = {};
        std::memcpy(c + kFlags, &kContextDebugRegisters, 4);
        bool ok = GetThreadContext(g_hiloEe, c) != 0;
        if (ok)
        {
            uint64_t dr7 = leer64(c, kDr7) & ~0xF0003ull;
            if (direccion != 0u)
                dr7 |= 0x1ull | (0x1ull << 16) | (0x2ull << 18); // L0; RW0 = 01 (escrituras); LEN0 = 10 (8 bytes)
            escribir64(c, kDr0, direccion & ~static_cast<uintptr_t>(7u));
            escribir64(c, kDr6, 0u);
            escribir64(c, kDr7, dr7);
            ok = SetThreadContext(g_hiloEe, c) != 0;
        }
        ResumeThread(g_hiloEe);
        return ok;
    }
#else
    void registrarHiloEe() {}
    bool listo() { return false; }
    bool vigilar(uintptr_t, Aviso) { return false; }
#endif
}
