// Codigo propio de Formula One 06 (Europa, SCES-53950 v1.01) para el runtime de PS2Recomp.
// recompilar.cmd lo copia a ps2xRuntime/src/runner junto al codigo generado.
//
// Los archivos del disco se leen de la carpeta del ELF (juego\), que el runtime presenta al IOP como
// una ISO virtual. Con PS2X_ISO=<ruta> el CDVD del IOP lee la ISO original (sceCdSearchFile y sceCdRead
// con sus sectores reales); el resto (cdrom0: del EE, IRX) sigue saliendo de juego\ por ahora.
#include "game_overrides.h"
#include "ps2_heap_reserve.h"
#include "ps2_high_ram.h"
#include "ps2_runtime.h"
#include "ps2_host_overlay.h"
#if defined(PS2X_ENABLE_DEBUG_UI)
#include "ps2_options_menu.h"
#endif
#include "ps2x/iop/iop_spu2_audio.h" // parche 0159: spu2QuietFrames
#include "../lib/ps2_ai_difficulty.h" // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_camera_fov.h" // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_game_font.h"  // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_game_graphics.h" // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_game_rand.h"  // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_hud.h"        // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_lgdev.h"      // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_pad_players.h" // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_photo_mode.h" // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_race_laps.h" // desde ps2xRuntime/src/runner, donde se compila
#include "../lib/ps2_replay_scrub.h" // desde ps2xRuntime/src/runner, donde se compila
#include "raylib.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_ab.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <random>
#include <vector>

// ps2_vif1_interpreter.cpp (parche 0016): ultimas interrupciones del VIF1 con su marca.
void ps2DebugDumpVif1Irqs();
// f1_fallos.cpp: nucleos del procesador (aparte porque necesita Win32).
bool f1EnNucleoEficiente();
void f1NucleosDeRendimiento(bool rendimiento);
uint64_t f1CpuHilo();
uint64_t f1Tsc();
// kernel32 (1.4 T2): ciclos del hilo actual, sin <windows.h> (choca con raylib.h).
extern "C" __declspec(dllimport) int __stdcall QueryThreadCycleTime(void *hilo, unsigned long long *ciclos);
extern "C" __declspec(dllimport) void *__stdcall GetCurrentThread();
static uint64_t f1CiclosHilo()
{
    unsigned long long ciclos = 0;
    QueryThreadCycleTime(GetCurrentThread(), &ciclos);
    return ciclos;
}
uint64_t f1MedirCadena();
unsigned f1NucleosAparcados();
// ps2_runtime.cpp (parche 0103): la opcion panoramica del juego (1 = 16:9 anamorfico, 0 = 4:3).
void ps2SetGameWidescreen(int on);
// gs_cpu_backend.cpp (parche 0104): presentar otro bufer del contexto cuando el de pantalla (FBP 0) sale negro.
void ps2GsSetBlackDisplayFallback(bool enabled);
// gs_frontend.cpp (parche 0148): bordes suaves para los dibujos de una ventana de recorte (SCISSOR) concreta.
void ps2GsSetScissorFeather(uint64_t scissor, uint32_t widthPixels);
// gs_frontend.cpp (parche 0148): los texeles transparentes no escriben el alfa de estos buferes de imagen (FBP).
void ps2GsSetKeepFrameAlpha(const uint32_t *fbps, uint32_t count);
// EeScheduler.cpp (parche 0159): el reloj del juego no espera al real (cargas rapidas).
void ps2SetFastForward(bool on) noexcept;
// f1_vigia.cpp: escrituras del hilo del EE vigiladas con los registros de depuracion (aparte porque necesita Win32).
namespace f1vigia
{
    using Aviso = void (*)(uintptr_t direccion);
    void registrarHiloEe();
    bool listo();
    bool vigilar(uintptr_t direccion, Aviso aviso);
}

namespace
{
    // El juego gestiona su heap con su propio sbrk/malloc (newlib) y al arrancar se queda con toda la
    // memoria que le da EndOfHeap. Reservamos el final del heap para el runtime (paquetes SIF, etc.)
    // para que sus reservas internas no pisen los gestores de memoria del juego.
    constexpr uint32_t kReservaRuntime = 512u * 1024u;

    constexpr uint32_t kCodigoInicio = 0x00100000u; // .text del ELF
    constexpr uint32_t kCodigoFin = 0x003E22C0u;

    // ---- Herramientas de depuracion ----

    // Segundos desde que arranca el programa (no desde la primera lectura del mando, que llega en plena carga).
    const auto g_inicioPrograma = std::chrono::steady_clock::now();

    double segundosDesdeArranque()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_inicioPrograma).count();
    }

    uint32_t leer32(const uint8_t *rdram, uint32_t direccion)
    {
        uint32_t valor = 0;
        std::memcpy(&valor, rdram + (direccion & PS2_RAM_MASK), sizeof(valor));
        return valor;
    }

    float leerFloat(const uint8_t *rdram, uint32_t direccion)
    {
        float valor = 0.0f;
        std::memcpy(&valor, rdram + (direccion & PS2_RAM_MASK), sizeof(valor));
        return valor;
    }

    std::string leerCadena(const uint8_t *rdram, uint32_t direccion, size_t maximo = 96)
    {
        std::string texto;
        for (size_t i = 0; i < maximo; ++i)
        {
            const char c = static_cast<char>(rdram[(direccion + i) & PS2_RAM_MASK]);
            if (c == '\0')
                break;
            texto.push_back(c >= 0x20 && c < 0x7F ? c : '.');
        }
        return texto;
    }

    // Recorre la pila del EE desde un punto de llamada. Para cada funcion busca hacia atras su prologo
    // ("addiu $sp, $sp, -N" seguido de "sd/sw $ra, X($sp)") para saber donde guardo su direccion de retorno.
    void imprimirPila(const uint8_t *rdram, uint32_t ra, uint32_t sp, int niveles)
    {
        for (int nivel = 0; nivel < niveles && ra >= kCodigoInicio && ra < kCodigoFin; ++nivel)
        {
            std::cout << "    #" << nivel << " 0x" << std::hex << ra << std::dec << std::endl;
            int32_t marco = 0;
            int32_t posicionRa = -1;
            for (uint32_t pc = ra - 4u; pc >= kCodigoInicio && pc + 0x8000u > ra; pc -= 4u)
            {
                const uint32_t instruccion = leer32(rdram, pc);
                if ((instruccion & 0xFFFF8000u) == 0x27BD8000u) // addiu $sp, $sp, -N
                {
                    marco = -static_cast<int16_t>(instruccion & 0xFFFFu);
                    for (uint32_t p = pc; p < pc + 0x40u; p += 4u)
                    {
                        const uint32_t guardado = leer32(rdram, p);
                        if ((guardado & 0xFFFF0000u) == 0xFFBF0000u || (guardado & 0xFFFF0000u) == 0xAFBF0000u)
                        {
                            posicionRa = static_cast<int16_t>(guardado & 0xFFFFu);
                            break;
                        }
                    }
                    break;
                }
            }
            if (marco <= 0 || posicionRa < 0)
            {
                break;
            }
            ra = leer32(rdram, sp + static_cast<uint32_t>(posicionRa));
            sp += static_cast<uint32_t>(marco);
        }
    }

    // Sustituye una funcion del juego por un chivato que registra y despues llama a la original.
    void instalarChivato(PS2Runtime &runtime, uint32_t direccion, PS2Runtime::RecompiledFunction chivato,
                         PS2Runtime::RecompiledFunction &original)
    {
        original = runtime.lookupFunction(direccion);
        if (original)
        {
            runtime.replaceFunction(direccion, chivato);
        }
    }

    // ---- Chivatos ----

    // Ayudas a la conduccion del perfil. Las funciones nativas de Lua de la tabla 0x3FC7C0 (clave, 0xFFFF0000,
    // funcion: "steeringassistance" 0x15CFD0, "brakingassistance" 0x15CFF0, "antilockbrakes" 0x15D010,
    // "spinrecovery" 0x15D050, "braketurnapexmarkers" 0x15D030...) llaman a func_15C658(obj, L, bit): con un
    // argumento (el jugador j) devuelven el bit `bit` de la media palabra obj + 0xB4 + j * 12 + 8; con dos, lo cambian.
    // Bits: 0 direccion asistida, 1 frenada asistida, 2 ABS, 3 recuperacion de trompos, 5 marcas de frenada. Solo las
    // usa el menu de opciones (en el camino menu -> carrera no se llaman; el chivato las registra si se usan): la
    // carrera lee el perfil directamente. El objeto es un singleton de 0x6238 bytes en *0x49ED94 (lo crea func_1644B0
    // al registrar la tabla 0x3FC6B8 en Lua; en las pruebas, en 0x9C1470). Un perfil nuevo trae 0xFFDF en las tres
    // entradas (todas las ayudas menos el bit 5): el coche ya iba con ayudas y aun asi se salia en Copse.
    // F1_AYUDAS=<valor> fija esa media palabra del jugador 0 en cada graph.outcome (p. ej. 0 = sin ayudas) y
    // [f1-06:ayudas] muestra las entradas.
    constexpr uint32_t kAyudaPerfil = 0x0015C658u;
    constexpr uint32_t kPerfilSingleton = 0x0049ED94u;
    PS2Runtime::RecompiledFunction g_ayudaPerfilOriginal = nullptr;

    // F1_PRUEBA_CARRERA=<distancia>,<averias>[,<desgaste>] (T1, pruebas): fija en cada graph.outcome las opciones de
    // carrera del perfil de los 7 modos (*0x49ED94 + 0x7C + modo * 8: game.racedistance, un float, negativa = la que
    // haya; + 4: bits 0-3 game.fuelandwear y bit 5 game.componentfailures, func_15CED8 / func_398EC8) y las anota
    // cuando cambian.
    void aplicarOpcionesCarrera(uint8_t *rdram, uint32_t obj)
    {
        struct Prueba
        {
            bool activa = false;
            float distancia = 0.0f;
            int averias = -1, desgaste = -1;
        };
        static const Prueba s_prueba = []
        {
            Prueba p;
            if (const char *valor = std::getenv("F1_PRUEBA_CARRERA"); valor != nullptr && *valor != '\0')
            {
                p.activa = std::sscanf(valor, "%f,%d,%d", &p.distancia, &p.averias, &p.desgaste) >= 2;
            }
            return p;
        }();
        if (!s_prueba.activa || obj < 0x00100000u || obj >= 0x02000000u)
            return;
        static uint32_t s_informes = 0u;
        for (uint32_t modo = 0; modo < 7u; ++modo)
        {
            const uint32_t ranura = obj + 0x7Cu + modo * 8u;
            const float antes = leerFloat(rdram, ranura);
            const float distancia = s_prueba.distancia < 0.0f ? antes : s_prueba.distancia;
            const uint32_t bitsAntes = leer32(rdram, ranura + 4u);
            uint32_t bits = (bitsAntes & ~0x20u) | (s_prueba.averias != 0 ? 0x20u : 0u);
            if (s_prueba.desgaste >= 0)
                bits = (bits & ~0xFu) | (static_cast<uint32_t>(s_prueba.desgaste) & 0xFu);
            if ((antes != distancia || bits != bitsAntes) && s_informes < 40u)
            {
                ++s_informes;
                std::cout << "[f1-06:prueba] t=" << segundosDesdeArranque() << " modo " << modo << " distancia " << antes
                          << " -> " << distancia << " bits 0x" << std::hex << bitsAntes << " -> 0x" << bits
                          << std::dec << std::endl;
            }
            std::memcpy(rdram + (ranura & PS2_RAM_MASK), &distancia, 4);
            std::memcpy(rdram + ((ranura + 4u) & PS2_RAM_MASK), &bits, 4);
        }
    }

    // Carrera directa (pruebas, odd/tasks/carrera-directa.md): variables F1_CARRERA_* leidas una vez. En el perfil, por
    // modo (S = *0x49ED94 + 0x7C + modo * 8): + 7 game.climate (byte), + 6 game.aidifficulty (0 rookie, 1 semipro,
    // 2 professional) y en la palabra + 4 el bit 4 game.crashdamage y el bit 6 game.penalties. Los valores del clima
    // son las constantes game.forecast/sunny/overcast/lightrain/heavyrain, enteros del perfil en + 0x608C..0x609C (se
    // leen al aplicar). Circuito y piloto van por Lua (kExtensionCarreraDirecta, puenteCarrera). F1_CARRERA_LISTA=1
    // solo anota: constantes, valores actuales, circuitos y pilotos.
    struct CarreraDirecta
    {
        int clima = -1;      // 0..4: forecast, sunny, overcast, lightrain, heavyrain (indice de la constante)
        int climaCrudo = -1; // un numero tal cual
        int ia = -1, danos = -1, sanciones = -1, circuito = -1;
        std::string piloto; // en minusculas
        bool lista = false;
        bool memoria() const { return clima >= 0 || climaCrudo >= 0 || ia >= 0 || danos >= 0 || sanciones >= 0; }
        bool lua() const { return circuito > 0 || !piloto.empty() || lista; }
    };

    std::string minusculas(std::string texto)
    {
        std::transform(texto.begin(), texto.end(), texto.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return texto;
    }

    // El numero de `valor` o el indice de su nombre en `nombres` (grupos separados por '|'); -1 si no es ninguno.
    int valorCarrera(const char *variable, std::initializer_list<const char *> nombres, bool &crudo)
    {
        crudo = false;
        const char *valor = std::getenv(variable);
        if (valor == nullptr || *valor == '\0')
            return -1;
        const std::string texto = minusculas(valor);
        int indice = 0;
        for (const char *grupo : nombres)
        {
            std::string resto = std::string(grupo) + "|";
            for (size_t corte; (corte = resto.find('|')) != std::string::npos; resto.erase(0, corte + 1))
                if (texto == resto.substr(0, corte))
                    return indice;
            ++indice;
        }
        char *fin = nullptr;
        const long numero = std::strtol(texto.c_str(), &fin, 0);
        if (fin != texto.c_str() && *fin == '\0' && numero >= 0)
        {
            crudo = true;
            return static_cast<int>(numero);
        }
        std::cout << "[f1-06:carrera] " << variable << "=" << valor << ": valor desconocido, no se aplica" << std::endl;
        return -1;
    }

    const CarreraDirecta &carreraDirecta()
    {
        static const CarreraDirecta s_carrera = []
        {
            CarreraDirecta c;
            bool crudo = false;
            const int clima = valorCarrera("F1_CARRERA_CLIMA", {"variable|forecast", "soleado|sunny", "nublado|overcast",
                                                                "lluvia|lightrain", "lluviafuerte|heavyrain"}, crudo);
            (crudo ? c.climaCrudo : c.clima) = clima;
            c.ia = valorCarrera("F1_CARRERA_IA", {"facil|rookie|easy", "media|semipro|medium", "dificil|professional|hard"}, crudo);
            if (c.ia > 2)
                c.ia = -1;
            c.danos = valorCarrera("F1_CARRERA_DANOS", {"0|no", "1|si"}, crudo);
            c.sanciones = valorCarrera("F1_CARRERA_SANCIONES", {"0|no", "1|si"}, crudo);
            // Numeracion de game.track (comprobada en el juego con F1_CARRERA_LISTA): el calendario de 2006, de 1
            // Bahrain a 18 Interlagos (8 Silverstone, el de por defecto). Pilotos: game.teaminfo(1..11).driver1/2.
            c.circuito = valorCarrera("F1_CARRERA_CIRCUITO", {"", "bahrain|sakhir", "sepang|malasia|malaysia",
                                                                 "melbourne|australia", "imola|sanmarino",
                                                                 "nurburgring|europa|europe", "catalunya|barcelona|espana|spain",
                                                                 "monaco|montecarlo", "silverstone|granbretana|britain",
                                                                 "montreal|canada", "indianapolis|usa",
                                                                 "magnycours|francia|france", "hockenheim|alemania|germany",
                                                                 "budapest|hungaroring|hungria|hungary", "istanbul|turquia|turkey",
                                                                 "monza|italia|italy", "shanghai|china", "suzuka|japon|japan",
                                                                 "interlagos|brasil|brazil"}, crudo);
            if (c.circuito < 1 || c.circuito > 18)
                c.circuito = -1;
            if (const char *piloto = std::getenv("F1_CARRERA_PILOTO"); piloto != nullptr)
                c.piloto = minusculas(piloto);
            const char *lista = std::getenv("F1_CARRERA_LISTA");
            c.lista = lista != nullptr && std::atoi(lista) != 0;
            if (c.memoria() || c.lua())
                std::cout << "[f1-06:carrera] clima " << c.clima << " (crudo " << c.climaCrudo << ") ia " << c.ia
                          << " danos " << c.danos << " sanciones " << c.sanciones << " circuito " << c.circuito
                          << " piloto '" << c.piloto << "' lista " << c.lista << std::endl;
            return c;
        }();
        return s_carrera;
    }

    // En cada graph.outcome (tras aplicarOpcionesCarrera): clima, IA, danos y sanciones de los 7 modos; el resto de
    // bits y bytes de la ranura no cambia.
    void aplicarCarreraDirecta(uint8_t *rdram, uint32_t obj)
    {
        const CarreraDirecta &c = carreraDirecta();
        if ((!c.memoria() && !c.lista) || obj < 0x00100000u || obj >= 0x02000000u)
            return;
        static uint32_t s_informes = 0u;
        static bool s_constantes = false;
        if (!s_constantes)
        {
            s_constantes = true;
            std::cout << "[f1-06:carrera] t=" << segundosDesdeArranque() << " perfil=0x" << std::hex << obj << std::dec
                      << " clima: variable " << leer32(rdram, obj + 0x608Cu) << " soleado " << leer32(rdram, obj + 0x6090u)
                      << " nublado " << leer32(rdram, obj + 0x6094u) << " lluvia " << leer32(rdram, obj + 0x6098u)
                      << " lluviafuerte " << leer32(rdram, obj + 0x609Cu) << " fromfile " << leer32(rdram, obj + 0x60A0u)
                      << std::endl;
        }
        static uint32_t s_vistos[7] = {};
        for (uint32_t modo = 0; modo < 7u; ++modo)
        {
            const uint32_t ranura = obj + 0x7Cu + modo * 8u;
            uint8_t *const ia = rdram + ((ranura + 6u) & PS2_RAM_MASK);
            uint8_t *const clima = rdram + ((ranura + 7u) & PS2_RAM_MASK);
            const uint32_t bitsAntes = leer32(rdram, ranura + 4u);
            const uint8_t iaAntes = *ia, climaAntes = *clima;
            uint32_t bits = bitsAntes;
            if (c.danos >= 0)
                bits = (bits & ~0x10u) | (c.danos != 0 ? 0x10u : 0u);
            if (c.sanciones >= 0)
                bits = (bits & ~0x40u) | (c.sanciones != 0 ? 0x40u : 0u);
            // La palabra + 4 incluye los bytes + 6 y + 7: primero ella y despues IA y clima.
            std::memcpy(rdram + ((ranura + 4u) & PS2_RAM_MASK), &bits, 4);
            if (c.ia >= 0)
                *ia = static_cast<uint8_t>(c.ia);
            if (c.clima >= 0)
                *clima = static_cast<uint8_t>(leer32(rdram, obj + 0x608Cu + 4u * static_cast<uint32_t>(c.clima)));
            else if (c.climaCrudo >= 0)
                *clima = static_cast<uint8_t>(c.climaCrudo);
            bits = leer32(rdram, ranura + 4u);
            const uint32_t visto = (bits << 16) | (static_cast<uint32_t>(*ia) << 8) | *clima;
            const bool cambio = bits != bitsAntes || *ia != iaAntes || *clima != climaAntes;
            if ((cambio || (c.lista && visto != s_vistos[modo])) && s_informes < 60u)
            {
                ++s_informes;
                std::cout << "[f1-06:carrera] t=" << segundosDesdeArranque() << " modo " << modo << " clima "
                          << static_cast<int>(climaAntes) << " -> " << static_cast<int>(*clima) << " ia "
                          << static_cast<int>(iaAntes) << " -> " << static_cast<int>(*ia) << " bits 0x" << std::hex
                          << (bitsAntes & 0xFFFFu) << " -> 0x" << (bits & 0xFFFFu) << std::dec << std::endl;
            }
            s_vistos[modo] = visto;
        }
    }

    void aplicarAyudas(uint8_t *rdram)
    {
        static const int32_t s_valor = []
        {
            const char *valor = std::getenv("F1_AYUDAS");
            return valor != nullptr ? static_cast<int32_t>(std::strtoul(valor, nullptr, 0) & 0xFFFFu) : -1;
        }();
        const uint32_t obj = leer32(rdram, kPerfilSingleton);
        aplicarOpcionesCarrera(rdram, obj);
        aplicarCarreraDirecta(rdram, obj);
        if (s_valor < 0 || obj == 0u)
            return;
        uint16_t *const ayudas = reinterpret_cast<uint16_t *>(rdram + ((obj + 0xBCu) & PS2_RAM_MASK));
        static uint32_t informes = 0;
        if (*ayudas != static_cast<uint16_t>(s_valor) && informes++ < 20u)
            std::cout << "[f1-06:ayudas] t=" << segundosDesdeArranque() << " perfil=0x" << std::hex << obj << " ayudas 0x"
                      << *ayudas << " -> 0x" << s_valor << std::dec << std::endl;
        *ayudas = static_cast<uint16_t>(s_valor);
    }

    void trazarAyudaPerfil(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        const uint32_t obj = GPR_U32(ctx, 4);
        const uint32_t bit = GPR_U32(ctx, 6);
        g_ayudaPerfilOriginal(rdram, ctx, runtime);
        if (veces++ < 40u)
            std::cout << "[f1-06:ayudas] t=" << segundosDesdeArranque() << " func_15C658 obj=0x" << std::hex << obj
                      << " bit=" << bit << " ayudas=0x" << *reinterpret_cast<const uint16_t *>(rdram + ((obj + 0xBCu) & PS2_RAM_MASK))
                      << " ra=0x" << GPR_U32(ctx, 31) << std::dec << std::endl;
    }

    // RAM extra T4: region alta 0x2000000-0x3FFFFFF (memoria propia desde el parche 0180; el juego solo ve 32 MB), con un
    // asignador del port. De ella salen los buferes de los montones de Lua: func_29C728 (la llaman func_29B698 al
    // arrancar, montones de 0x404958 y 0x40495C, y func_29B740 al cambiar de monton) pide el bufer al malloc del juego
    // (jal func_259B50 en 0x29C758, vuelta 0x29C760) y func_29C7A0 lo devuelve con el free del juego (jal func_259DE8 en
    // 0x29C7D8; nadie mas libera el bufer). Asi el monton general del juego no paga el bufer. Asignador minimo
    // (ps2x::HighRamAllocator de ps2_high_ram.h, parche 0181, con sus pruebas): primer hueco de una lista ordenada,
    // bloques alineados a 64 bytes, huecos fusionados al liberar; las cuentas viven en el PC (nada en la RAM del juego). Si no cabe: linea [f1-06:ram-alta] y el malloc original. El free del juego devuelve a
    // la region todo puntero que caiga en ella (si no es un bloque suyo, lo avisa y no hace nada). Con la region activa
    // el monton de Lua de la carrera (1 MB, que func_29C728 sube a 1,5 MB) pasa a F1_LUA_CARRERA_MB (4 por defecto,
    // 1-16): en el fin de semana del campeonato se llenaba al 99 % y un NULL de ese monton es LUA_ERRMEM.
    // F1_LUA_RAM_ALTA=0 vuelve al comportamiento anterior (buferes del malloc general, monton de carrera de 1,5 MB);
    // solo valen 0 y 1: otro valor lo avisa y deja la region activa.
    namespace ram_alta
    {
        constexpr uint32_t kInicio = PS2_GUEST_RAM_SIZE;
        constexpr uint32_t kFin = PS2_RAM_SIZE;
        static_assert(kFin > kInicio, "sin region alta: PS2_RAM_SIZE debe pasar de PS2_GUEST_RAM_SIZE");
        bool g_activa = true;
        ps2x::HighRamAllocator g_asignador{kInicio, kFin - kInicio}; // ps2_high_ram.h (parche 0181)

        bool enRegion(uint32_t direccion)
        {
            return g_asignador.contains(direccion);
        }

        // Bloque de al menos `tamano` bytes (0 si no cabe).
        uint32_t pedir(uint32_t tamano)
        {
            return g_asignador.allocate(tamano);
        }

        // Devuelve el bloque que empieza en `direccion`; false si no es un bloque de la region.
        bool soltar(uint32_t direccion)
        {
            return g_asignador.release(direccion);
        }

        void informar(std::ostream &salida)
        {
            g_asignador.report(salida);
        }
    }

    // F1_MALLOC_LIBRE=1 (nativizacion T2): memoria libre del EE, para saber si caben montones de Lua mas grandes.
    // Una linea [f1-06:malloc-libre] por segundo con tres niveles:
    // - newlib (malloc 0x1020D0, trozos con el tamano en +4 y el bit 0 de "el anterior en uso"): de sbrk (0x3F90DC)
    //   al final del monton del juego (0x1FE0000, donde empieza la pila principal; otro con F1_MEMORIA_ANTIGUA) no se
    //   ha usado nunca; dentro, el trozo de arriba (top, __malloc_av_ 0x3F8608 + 8) y los trozos libres, recorridos
    //   desde __malloc_sbrk_base (0x3F8A20).
    // - malloc del juego (func_259B50): objeto en *0x3FEC0C con tres montones (+0 general, +0x80 para menos de 1 KB,
    //   +0x104 otro) que sacan sus regiones de newlib (lista en +0: siguiente, tamano, inicio y util en +0xC; tamanos
    //   iniciales en 0x3F8588) y, si se acaba el general, func_259A60 le pide a newlib otra region. Bytes usados en
    //   +0xC, maximo en +0x14 y 25 listas de bloques libres en +0x18 (tamano en +0, siguiente en +0x10).
    // - monton de Lua (*0x40495C: bufer sacado del malloc del juego, tamano en +4, asignador en +8 con base en +0x408,
    //   puntero que solo sube en +0x40C, tamano en +0x410, grano en +0x418 y 257 listas libres en +4).
    // En cada llamada al malloc del juego se apunta ademas el minimo de lo libre sin recorrer listas (newlib sin usar +
    // top, util - usados del general), para no perder el peor momento entre dos lineas. Sin la variable no cuesta nada.
    bool g_mallocLibre = false;
    constexpr uint32_t kNewlibBrk = 0x003F90DCu;
    constexpr uint32_t kNewlibAv = 0x003F8608u;
    constexpr uint32_t kNewlibSbrkBase = 0x003F8A20u;
    constexpr uint32_t kFinMontonJuego = 0x01FE0000u;
    constexpr uint32_t kMallocJuego = 0x003FEC0Cu;
    constexpr uint32_t kTamanosMallocJuego = 0x003F8588u;
    constexpr uint32_t kMontonLua = 0x0040495Cu;

    struct MontonJuego
    {
        uint32_t util = 0u, usados = 0u, maximo = 0u, regiones = 0u;
    };

    MontonJuego leerMontonJuego(const uint8_t *rdram, uint32_t h)
    {
        MontonJuego m;
        m.usados = leer32(rdram, h + 0xCu);
        m.maximo = leer32(rdram, h + 0x14u);
        for (uint32_t r = leer32(rdram, h); r != 0u && m.regiones < 64u; r = leer32(rdram, r))
        {
            m.util += leer32(rdram, r + 0xCu);
            ++m.regiones;
        }
        return m;
    }

    // Bytes libres sin recorrer listas: newlib (sin usar + top) y el monton general del juego (util - usados).
    void libreRapido(const uint8_t *rdram, uint32_t &newlib, uint32_t &general)
    {
        const uint32_t brk = leer32(rdram, kNewlibBrk);
        const uint32_t top = leer32(rdram, kNewlibAv + 8u);
        newlib = (brk < kFinMontonJuego ? kFinMontonJuego - brk : 0u) + (leer32(rdram, top + 4u) & ~3u);
        const uint32_t obj = leer32(rdram, kMallocJuego);
        const MontonJuego m = obj != 0u ? leerMontonJuego(rdram, obj) : MontonJuego{};
        general = m.util > m.usados ? m.util - m.usados : 0u;
    }

    void informarMallocLibre(const uint8_t *rdram)
    {
        static uint32_t s_minNewlib = UINT32_MAX, s_minGeneral = UINT32_MAX;
        static double s_ultimo = 0.0;
        static bool s_tamanos = false;
        uint32_t newlib = 0u, general = 0u;
        libreRapido(rdram, newlib, general);
        s_minNewlib = std::min(s_minNewlib, newlib);
        s_minGeneral = std::min(s_minGeneral, general);
        const double ahora = segundosDesdeArranque();
        if (ahora - s_ultimo < 1.0)
            return;
        s_ultimo = ahora;
        if (!s_tamanos)
        {
            s_tamanos = true;
            std::cout << "[f1-06:malloc-libre] tamanos iniciales: pequeno=" << leer32(rdram, kTamanosMallocJuego)
                      << " general=" << leer32(rdram, kTamanosMallocJuego + 4u)
                      << " crecer=" << leer32(rdram, kTamanosMallocJuego + 8u)
                      << " otro=" << leer32(rdram, kTamanosMallocJuego + 0xCu) << std::endl;
        }
        // newlib: trozos desde el primero (alineado como malloc_extend_top: trozo + 8 a 16 bytes) hasta el top.
        const uint32_t brk = leer32(rdram, kNewlibBrk);
        const uint32_t top = leer32(rdram, kNewlibAv + 8u);
        const uint32_t tamTop = leer32(rdram, top + 4u) & ~3u;
        const uint32_t base = leer32(rdram, kNewlibSbrkBase);
        uint32_t trozo = base + ((16u - ((base + 8u) & 15u)) & 15u);
        uint32_t libresNewlib = 0u, mayorNewlib = 0u, trozos = 0u;
        while (trozo < top && trozos < 400000u)
        {
            const uint32_t tam = leer32(rdram, trozo + 4u) & ~3u;
            if (tam < 16u || trozo + tam > top)
                break;
            if ((leer32(rdram, trozo + tam + 4u) & 1u) == 0u)
            {
                libresNewlib += tam;
                mayorNewlib = std::max(mayorNewlib, tam);
            }
            trozo += tam;
            ++trozos;
        }
        std::cout << "[f1-06:malloc-libre] t=" << ahora << " newlib: brk=0x" << std::hex << brk << " fin=0x"
                  << kFinMontonJuego << std::dec << " sin_usar=" << (brk < kFinMontonJuego ? kFinMontonJuego - brk : 0u)
                  << " top=" << tamTop << " libres=" << libresNewlib << " mayor_libre=" << mayorNewlib
                  << " trozos=" << trozos << (trozo == top ? "" : " (recorrido incompleto)") << std::endl;
        // malloc del juego: sus tres montones.
        if (const uint32_t obj = leer32(rdram, kMallocJuego); obj != 0u)
        {
            static const char *const nombres[] = {"general", "pequeno", "otro"};
            const uint32_t desplazamientos[] = {0u, 0x80u, 0x104u};
            for (int i = 0; i < 3; ++i)
            {
                const uint32_t h = obj + desplazamientos[i];
                const MontonJuego m = leerMontonJuego(rdram, h);
                uint32_t libres = 0u, mayor = 0u, bloques = 0u;
                for (uint32_t lista = 0u; lista < 25u; ++lista)
                    for (uint32_t b = leer32(rdram, h + 0x18u + 4u * lista); b != 0u && bloques < 200000u;
                         b = leer32(rdram, b + 0x10u), ++bloques)
                    {
                        const uint32_t tam = leer32(rdram, b);
                        libres += tam;
                        mayor = std::max(mayor, tam);
                    }
                std::cout << "[f1-06:malloc-libre]   juego " << nombres[i] << ": util=" << m.util << " usados=" << m.usados
                          << " maximo=" << m.maximo << " regiones=" << m.regiones << " libres=" << libres
                          << " mayor_libre=" << mayor << " bloques_libres=" << bloques << std::endl;
            }
        }
        // Monton de Lua.
        if (const uint32_t lua = leer32(rdram, kMontonLua); lua != 0u)
        {
            const uint32_t a = leer32(rdram, lua + 8u);
            const uint32_t luaBase = leer32(rdram, a + 0x408u), puntero = leer32(rdram, a + 0x40Cu);
            const uint32_t luaTam = leer32(rdram, a + 0x410u), grano = leer32(rdram, a + 0x418u);
            uint32_t enListas = 0u, nodos = 0u;
            for (uint32_t lista = 0u; lista <= 0x100u; ++lista)
                for (uint32_t b = leer32(rdram, a + 4u + 4u * lista); b != 0u && nodos < 400000u; b = leer32(rdram, b), ++nodos)
                    enListas += lista * grano;
            const uint32_t tocado = puntero - luaBase;
            std::cout << "[f1-06:malloc-libre]   lua: bufer=0x" << std::hex << leer32(rdram, lua) << std::dec
                      << " tamano=" << leer32(rdram, lua + 4u) << " util=" << luaTam
                      << " tocado=" << tocado << " en_listas=" << enListas
                      << " usado=" << (tocado > enListas ? tocado - enListas : 0u)
                      << " sin_tocar=" << (luaTam > tocado ? luaTam - tocado : 0u) << std::endl;
        }
        if (ram_alta::g_activa)
        {
            std::cout << "[f1-06:malloc-libre]   alta: ";
            ram_alta::informar(std::cout);
            std::cout << std::endl;
        }
        std::cout << "[f1-06:malloc-libre]   minimo desde la linea anterior: newlib(sin_usar+top)=" << s_minNewlib
                  << " general(util-usados)=" << s_minGeneral << std::endl;
        s_minNewlib = UINT32_MAX;
        s_minGeneral = UINT32_MAX;
    }

    // malloc del juego: func 0x259B50(tamano). Avisa de las peticiones de 16 MB o mas con la pila.
    // Con la region alta (RAM extra T4), el pedido del bufer de un monton de Lua (vuelta a 0x29C760) sale de ella.
    constexpr uint32_t kMalloc = 0x00259B50u;
    constexpr uint32_t kVueltaBuferMontonLua = 0x0029C760u;
    PS2Runtime::RecompiledFunction g_mallocOriginal = nullptr;

    void trazarMalloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (g_mallocLibre)
            informarMallocLibre(rdram);
        const uint32_t tamano = GPR_U32(ctx, 4);
        if (ram_alta::g_activa && GPR_U32(ctx, 31) == kVueltaBuferMontonLua)
        {
            const uint32_t bloque = ram_alta::pedir(tamano);
            std::cout << "[f1-06:ram-alta] t=" << segundosDesdeArranque() << " bufer de monton Lua: " << (tamano >> 10)
                      << " KB ";
            if (bloque != 0u)
            {
                std::cout << "en 0x" << std::hex << bloque << std::dec << " (";
                ram_alta::informar(std::cout);
                std::cout << ")" << std::endl;
                SET_GPR_U32(ctx, 2, bloque);
                ctx->pc = GPR_U32(ctx, 31);
                return;
            }
            std::cout << "sin sitio en la region alta: va al malloc del juego" << std::endl;
        }
        if (tamano >= 0x01000000u)
        {
            std::cout << "[f1-06] malloc enorme tamano=0x" << std::hex << tamano << std::dec << std::endl;
            imprimirPila(rdram, GPR_U32(ctx, 31), GPR_U32(ctx, 29), 12);
        }
        g_mallocOriginal(rdram, ctx, runtime);
    }

    // free del juego: func 0x259DE8(bloque). Los punteros de la region alta vuelven a su asignador (RAM extra T4).
    constexpr uint32_t kFree = 0x00259DE8u;
    PS2Runtime::RecompiledFunction g_freeOriginal = nullptr;

    void trazarFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t bloque = GPR_U32(ctx, 4);
        if (!ram_alta::enRegion(bloque))
        {
            g_freeOriginal(rdram, ctx, runtime);
            return;
        }
        const bool suyo = ram_alta::soltar(bloque);
        std::cout << "[f1-06:ram-alta] t=" << segundosDesdeArranque() << " free 0x" << std::hex << bloque
                  << (suyo ? "" : " que no es un bloque de la region (ignorado)") << " ra=0x" << GPR_U32(ctx, 31)
                  << std::dec << " (";
        ram_alta::informar(std::cout);
        std::cout << ")" << std::endl;
        ctx->pc = GPR_U32(ctx, 31);
    }

    // Letras del juego para los avisos del anfitrion (Aspecto T2): func_264888(fuente, nombre) lee un .bft del WAD con
    // el flujo que abre func_258778 (objeto en v0; su lectura, func_27A0B8(flujo, destino, n), descomprime n bytes; su
    // cierre, func_27A340(flujo)). Se copian, en orden, los bytes que lee ese flujo: el archivo tal cual, sacado de los
    // datos del usuario (nada del juego va en el paquete), y se dejan en ps2x::storeGameFontFile para los avisos
    // (ps2_game_font.h: el formato). El codigo traducido puede volver a medias de una llamada (en un punto de control)
    // y seguir despues sin pasar por el chivato: cada lectura se copia al empezar la siguiente (que ya no puede
    // empezar sin que acabe la anterior) y la ultima al cerrar el flujo. El juego vuelve a leer sus letras en muchas
    // pantallas: [f1-06:letra] sale solo cuando cambian. F1_BFT_VOLCAR=<carpeta>: copia cada .bft en esa carpeta
    // (para scripts/bft_decodificar.py; son datos del juego: fuera del proyecto).
    constexpr uint32_t kLeerFuente = 0x00264888u;
    constexpr uint32_t kLeerFlujoWad = 0x0027A0B8u;
    constexpr uint32_t kCerrarFlujoWad = 0x0027A340u;
    PS2Runtime::RecompiledFunction g_leerFuenteOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_leerFlujoWadOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_cerrarFlujoWadOriginal = nullptr;
    struct CapturaFuente
    {
        bool activa = false;
        uint32_t flujo = 0u;
        std::string nombre;
        std::vector<uint8_t> bytes;
        uint32_t pendienteDestino = 0u, pendienteBytes = 0u;
    };
    CapturaFuente g_fuente; // solo la toca el hilo del EE

    void copiarLecturaPendiente(const uint8_t *rdram, CapturaFuente &captura = g_fuente)
    {
        for (uint32_t i = 0; i < captura.pendienteBytes; ++i)
            captura.bytes.push_back(rdram[(captura.pendienteDestino + i) & PS2_RAM_MASK]);
        captura.pendienteBytes = 0u;
    }

    // Aspecto T4: los graficos del juego para las barras de ayuda del anfitrion (ps2_game_graphics.h: botones del
    // mando, la banda inferior de los menus). Cada .pct se lee del WAD con el mismo flujo que las letras: desde su
    // apertura (func_258778 con un nombre de kGameTextureFiles) se copian sus lecturas hasta que se cierra y se dejan en
    // ps2x::storeGameTextureFile. F1_PCT_VOLCAR=<carpeta>: copia ahi cada .pct que abra el juego (todos; son datos del
    // juego: fuera del proyecto, para scripts/pct_decodificar.py).
    CapturaFuente g_textura; // solo la toca el hilo del EE
    const char *g_volcarPct = nullptr;

    // Apertura de archivos del juego: func 0x258778(nombre, ...). Sirve para seguir el avance del arranque.
    constexpr uint32_t kAbrirArchivo = 0x00258778u;
    PS2Runtime::RecompiledFunction g_abrirArchivoOriginal = nullptr;

    void trazarAbrirArchivo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const std::string nombre = leerCadena(rdram, GPR_U32(ctx, 4));
        g_abrirArchivoOriginal(rdram, ctx, runtime);
        std::cout << "[f1-06] abrir \"" << nombre << "\" -> 0x" << std::hex << GPR_U32(ctx, 2) << std::dec << std::endl;
        if (g_fuente.activa && g_fuente.flujo == 0u && nombre.size() > 4u &&
            nombre.compare(nombre.size() - 4u, 4u, ".bft") == 0)
        {
            g_fuente.flujo = GPR_U32(ctx, 2);
            g_fuente.nombre = nombre;
        }
        else if (nombre.size() > 4u && ps2x::sameGameFileName(nombre.substr(nombre.size() - 4u), ".pct") &&
                 GPR_U32(ctx, 2) != 0u && (ps2x::wantedGameTexture(nombre) || g_volcarPct != nullptr))
        {
            g_textura = CapturaFuente{};
            g_textura.activa = true;
            g_textura.flujo = GPR_U32(ctx, 2);
            g_textura.nombre = nombre;
        }
    }

    void leerFlujoWad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (g_fuente.activa && GPR_U32(ctx, 4) == g_fuente.flujo)
        {
            copiarLecturaPendiente(rdram);
            g_fuente.pendienteDestino = GPR_U32(ctx, 5);
            g_fuente.pendienteBytes = std::min<uint32_t>(GPR_U32(ctx, 6), 1u << 20);
        }
        if (g_textura.activa && GPR_U32(ctx, 4) == g_textura.flujo)
        {
            copiarLecturaPendiente(rdram, g_textura);
            g_textura.pendienteDestino = GPR_U32(ctx, 5);
            g_textura.pendienteBytes = std::min<uint32_t>(GPR_U32(ctx, 6), 4u << 20);
        }
        g_leerFlujoWadOriginal(rdram, ctx, runtime);
    }

    void cerrarFlujoWad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (g_fuente.activa && GPR_U32(ctx, 4) == g_fuente.flujo)
        {
            copiarLecturaPendiente(rdram);
            g_fuente.activa = false;
            if (const char *carpeta = std::getenv("F1_BFT_VOLCAR"); carpeta != nullptr && *carpeta != '\0')
            {
                const std::string base = g_fuente.nombre.substr(g_fuente.nombre.find_last_of('/') + 1);
                std::ofstream(std::string(carpeta) + "/" + base, std::ios::binary)
                    .write(reinterpret_cast<const char *>(g_fuente.bytes.data()),
                           static_cast<std::streamsize>(g_fuente.bytes.size()));
            }
            const size_t bytes = g_fuente.bytes.size();
            if (ps2x::storeGameFontFile(g_fuente.nombre, std::move(g_fuente.bytes)))
                std::cout << "[f1-06:letra] \"" << g_fuente.nombre << "\": " << bytes << " bytes leidos" << std::endl;
        }
        if (g_textura.activa && GPR_U32(ctx, 4) == g_textura.flujo)
        {
            copiarLecturaPendiente(rdram, g_textura);
            g_textura.activa = false;
            if (g_volcarPct != nullptr)
            {
                std::string base = g_textura.nombre;
                std::replace(base.begin(), base.end(), '/', '_');
                std::ofstream(std::string(g_volcarPct) + "/" + base, std::ios::binary)
                    .write(reinterpret_cast<const char *>(g_textura.bytes.data()),
                           static_cast<std::streamsize>(g_textura.bytes.size()));
            }
            const size_t bytes = g_textura.bytes.size();
            if (ps2x::wantedGameTexture(g_textura.nombre) &&
                ps2x::storeGameTextureFile(g_textura.nombre, std::move(g_textura.bytes)))
                std::cout << "[f1-06:grafico] \"" << g_textura.nombre << "\": " << bytes << " bytes leidos" << std::endl;
        }
        g_cerrarFlujoWadOriginal(rdram, ctx, runtime);
    }

    void leerFuente(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_fuente = CapturaFuente{};
        g_fuente.activa = true;
        g_leerFuenteOriginal(rdram, ctx, runtime);
    }

    // Envios al VIF1: func_2D55E8 lanza el DMA y suma 1 al contador 0x404FE8; el manejador de la
    // interrupcion del VIF1 (func_2CB0B8, causa INTC 5) lo resta; func_2D52F8 espera a que llegue a 0.
    constexpr uint32_t kContadorVif1 = 0x00404FE8u;
    constexpr uint32_t kLanzarDmaVif1 = 0x002D55E8u;
    constexpr uint32_t kIrqVif1 = 0x002CB0B8u;
    constexpr uint32_t kEsperaVif1 = 0x002D52F8u;
    // Ritmo de los fotogramas (parche 0106). func_143BF8 cierra cada fotograma: lee el contador de VBLANK del manejador
    // del juego (func_2CB238, 0x404FF4), espera a que se acabe de dibujar (func_2D5390), lanza la lista de dibujo
    // (func_2D53C8) y, si no ha pasado ningun VBLANK desde que empezo, espera al siguiente con WaitSema (func_2D5538,
    // semaforo de 0x4B11EC). La linea [f1-06:vif1fps] cuenta por segundo los fotogramas, los VBLANK que vio el juego y
    // las esperas.
    constexpr uint32_t kFinFotograma = 0x00143BF8u;
    constexpr uint32_t kEsperaVblank = 0x002D5538u;
    constexpr uint32_t kContadorVblank = 0x00404FF4u;
    PS2Runtime::RecompiledFunction g_finFotogramaOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_esperaVblankOriginal = nullptr;
    std::atomic<uint64_t> g_finesFotograma{0u}, g_esperasVblank{0u};

    // F1_VIGIA_MATRIZ=1 (diagnostico de la "V" de la cabina, parche 0108): un hilo busca cada segundo en la RAM del EE
    // las piezas (bloques de 0xD0 del heap 0x4C1710 con la vtable 0x412DC8 en +0xB0) cuya matriz tiene la fila 2 igual
    // a la fila 1, y vigila con DR0 la fila 2 de una de ellas: cada escritura del EE dice su pc y su ra.
    uint8_t *g_vigiaRdram = nullptr;
    PS2Runtime *g_vigiaRuntime = nullptr;
    std::atomic<uint32_t> g_vigiaAvisos{0u};
    uint32_t g_vigiaEntrada = 0u;
    int g_vigiaModo = 1; // F1_VIGIA_MATRIZ=2: el color de la linea verde (ver arrancarVigiaMatriz)

    void avisoVigia(uintptr_t direccion)
    {
        const uint32_t n = g_vigiaAvisos.fetch_add(1u, std::memory_order_relaxed);
        if (n >= 40u)
            return;
        R5900Context *c = g_vigiaRuntime->eeScheduler().currentContext();
        const uint32_t ee = static_cast<uint32_t>(direccion - reinterpret_cast<uintptr_t>(g_vigiaRdram));
        const uint32_t fila = ee & ~0xFu;
        float f[3][4];
        std::memcpy(f, g_vigiaRdram + fila - 0x10u, sizeof(f));
        std::fprintf(stderr,
                     "[vigia] escritura en 0x%08x (entrada 0x%08x) pc=0x%08x ra=0x%08x sp=0x%08x hilo=%d "
                     "f1=(%.4f %.4f %.4f %.4f) f2=(%.4f %.4f %.4f %.4f) f3=(%.4f %.4f %.4f %.4f)\n",
                     ee, g_vigiaEntrada, c ? c->pc : 0u, c ? static_cast<uint32_t>(_mm_extract_epi32(c->r[31], 0)) : 0u,
                     c ? static_cast<uint32_t>(_mm_extract_epi32(c->r[29], 0)) : 0u,
                     g_vigiaRuntime->eeScheduler().currentThreadId(), f[0][0], f[0][1], f[0][2], f[0][3], f[1][0],
                     f[1][1], f[1][2], f[1][3], f[2][0], f[2][1], f[2][2], f[2][3]);
    }

    void arrancarVigiaMatriz()
    {
        std::thread([]
                    {
            uint32_t vigiladas = 0u;
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (!f1vigia::listo() || g_vigiaRdram == nullptr)
                    continue;
                if (g_vigiaEntrada != 0u && g_vigiaAvisos.load(std::memory_order_relaxed) < 12u)
                    continue;
                if (g_vigiaEntrada != 0u)
                {
                    f1vigia::vigilar(0u, avisoVigia);
                    g_vigiaEntrada = 0u;
                    if (++vigiladas >= 3u)
                        return;
                }
                const uint8_t *ram = g_vigiaRdram;
                if (g_vigiaModo == 2)
                {
                    // Modo 2: el color (15,255,0,a) de los vertices de la linea verde del asfalto.
                    for (uint32_t a = 0x00500000u; a + 0x10u < 0x01F00000u; a += 0x10u)
                    {
                        if (leer32(ram, a) != 0x0Fu || leer32(ram, a + 4u) != 0xFFu || leer32(ram, a + 8u) != 0u ||
                            leer32(ram, a + 12u) > 0x80u)
                            continue;
                        g_vigiaAvisos.store(0u, std::memory_order_relaxed);
                        g_vigiaEntrada = a;
                        const bool ok = f1vigia::vigilar(reinterpret_cast<uintptr_t>(ram + a), avisoVigia);
                        std::fprintf(stderr, "[vigia] color verde en 0x%08x: vigilado %s t=%.1f\n", a, ok ? "si" : "NO",
                                     segundosDesdeArranque());
                        break;
                    }
                    continue;
                }
                for (uint32_t a = 0x00500000u; a + 0xD0u < 0x01F00000u; a += 0x10u)
                {
                    if (leer32(ram, a) != 0xD0u || leer32(ram, a + 4u) != 0x004C1710u || leer32(ram, a + 0xC0u) != 0x00412DC8u)
                        continue;
                    float m[3][4];
                    std::memcpy(m, ram + a + 0x20u, sizeof(m));
                    if (std::memcmp(m[1], m[2], 12) != 0 || std::fabs(m[1][1]) < 0.5f)
                        continue;
                    g_vigiaAvisos.store(0u, std::memory_order_relaxed);
                    g_vigiaEntrada = a;
                    const bool ok = f1vigia::vigilar(reinterpret_cast<uintptr_t>(ram + a + 0x40u), avisoVigia);
                    std::fprintf(stderr, "[vigia] entrada 0x%08x (objeto 0x%08x) fila 2 = fila 1: vigilada %s t=%.1f\n", a,
                                 leer32(ram, a + 0x10u), ok ? "si" : "NO", segundosDesdeArranque());
                    break;
                }
            } })
            .detach();
    }

    // Marcas de frenada (diagnostico). func_304DB8 (vtable 0x41EDB8, la clase que carga Tracks/RLAid.pct) solo hace algo
    // si el bit 5 ("braketurnapexmarkers") de las ayudas del jugador *(a1+0x14) en la copia de la carrera
    // (*0x3FE588 + jugador*0x78 + 0x7C) esta puesto. La linea verde/roja del asfalto es la trazada virtual (bit 4,
    // func_31AAA0), no esto. F1_TRAZADA_DIAG=1 anota lo que lee; F1_TRAZADA_DIAG=2 ademas se salta la funcion.
    constexpr uint32_t kTrazada = 0x00304DB8u;
    PS2Runtime::RecompiledFunction g_trazadaOriginal = nullptr;
    int g_trazadaDiag = 0;

    void trazarTrazada(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint64_t llamadas = 0;
        const uint32_t obj = GPR_U32(ctx, 4), datos = GPR_U32(ctx, 5);
        const uint32_t jugador = leer32(rdram, datos + 0x14u);
        if (llamadas < 12u || llamadas % 2000u == 0u)
        {
            const uint32_t base = leer32(rdram, 0x003FE588u);
            uint16_t ayudas = 0;
            std::memcpy(&ayudas, rdram + ((base + jugador * 0x78u + 0x7Cu) & PS2_RAM_MASK), 2);
            std::cout << "[f1-06:trazada] t=" << segundosDesdeArranque() << " n=" << llamadas << std::hex << " obj=0x"
                      << obj << " jugador=" << jugador << " obj+20=" << leer32(rdram, obj + 0x20u) << " +48="
                      << leer32(rdram, datos + 0x48u) << " ayudas=0x" << ayudas << std::dec << std::endl;
        }
        ++llamadas;
        if (g_trazadaDiag >= 2)
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        g_trazadaOriginal(rdram, ctx, runtime);
    }

    void trazarFinFotograma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2x::setReplayScrubSeeking(false); // fase 5 (T14): el salto atras de la repeticion ya se ha hecho
        if (g_vigiaRdram == nullptr && g_vigiaRuntime != nullptr)
        {
            f1vigia::registrarHiloEe();
            g_vigiaRdram = rdram;
        }
        g_finesFotograma.fetch_add(1u, std::memory_order_relaxed);
        g_finFotogramaOriginal(rdram, ctx, runtime);
    }

    void trazarEsperaVblank(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_esperasVblank.fetch_add(1u, std::memory_order_relaxed);
        g_esperaVblankOriginal(rdram, ctx, runtime);
    }

    // Fase 6 T5: el lector de bits del juego, en nativo. func_279FA8(lector, n) devuelve los n bits siguientes de un
    // flujo de bytes (el primero, el mas alto; n = 0 lee 32): lector[0x24] es la mascara del bit actual (0x80: hay que
    // leer otro byte), lector[0x28] el byte actual, lector[0x3C] la posicion en el bufer de 0x400 bytes de *0x4B0158 y
    // lector[0x40] lo que queda del archivo. Con la posicion en 0x400 rellena el bufer con la lectura del archivo (la
    // funcion virtual de lector[0x14], que puede esperar al IOP). Al empezar la carrera el juego descodifica asi
    // Animations/Pitstop/Pitstop.rcc (5,1 millones de bits en 626.000 llamadas): el codigo traducido cobra 32 ciclos del
    // EE por vuelta del bucle (eeCheckpointDue), una vuelta por bit, y como el reloj del EE no adelanta al real, el
    // fotograma duraba lo que suman esos ciclos (~0,85 s, un congelon) aunque el PC lo haga en 0,2 s.
    // lectorBitsNucleo hace lo mismo que el codigo traducido, con las mismas macros (registros de 128 bits, pila,
    // escrituras), sin anotar el pc de cada instruccion, y se cobra lo que tardaria el R5900:
    //  - por bit, 15 ciclos: 13 instrucciones por vuelta (0x279FE0 addiu, bnel + lbu, or, lw, srl, srl, and, sb,
    //    bnez + movn, bnel + lbu) a una por ciclo (es una cadena dependiente, lbu -> srl -> sb -> lbu, que no se
    //    empareja) mas ~2 ciclos de las tres bifurcaciones tomadas;
    //  - por byte leido del bufer, 12 mas (lw, addiu, bne + lui, lw, lw, addu, lbu, addiu, sw, sw, lbu);
    //  - por llamada, 24 (prologo de 12 instrucciones y epilogo de 10 con el salto de vuelta).
    // Si hay que rellenar el bufer (una llamada de cada ~1.000), el nucleo para en 0x279FFC con el mismo estado que el
    // codigo traducido y este sigue desde alli (su tabla de funciones lleva cada direccion intermedia a el).
    // F1_LECTOR_BITS_NATIVO=0: siempre el codigo traducido. F1_LECTOR_BITS_VERIFICAR=<n> (prueba): en la primera
    // llamada compara las dos versiones en 20.000 casos sinteticos (semilla fija; incluye rellenos, con una
    // retrollamada que solo vuelve) y despues en las n primeras llamadas reales sin relleno (el juego sigue con el
    // resultado del codigo traducido); compara el contexto entero salvo el contador de instrucciones y la memoria
    // tocada. Resultado en [f1-06:lector-bits].
    constexpr uint32_t kLectorBits = 0x00279FA8u;
    constexpr uint32_t kLectorBitsRelleno = 0x00279FFCu;
    constexpr uint32_t kCiclosBit = 15u, kCiclosByte = 12u, kCiclosLlamada = 24u;
    PS2Runtime::RecompiledFunction g_lectorBitsOriginal = nullptr;
    bool g_lectorBitsNativo = true;
    int64_t g_lectorBitsVerificar = 0; // llamadas reales que quedan por comparar
    bool g_lectorBitsSinteticos = false;
    uint64_t g_lectorLlamadas = 0u, g_lectorBits = 0u, g_lectorCiclos = 0u, g_lectorRelevos = 0u;

    // Devuelve true con la funcion acabada (ctx->pc = $ra) o false parada en 0x279FFC para rellenar el bufer.
    bool lectorBitsNucleo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t &bits, uint32_t &bytes)
    {
        bits = bytes = 0u;
        // Prologo (0x279FA8-0x279FD8).
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967232u));
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294967295u));
        WRITE64(ADD32(GPR_U32(ctx, 29), 16), GPR_U64(ctx, 17));
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
        WRITE64(ADD32(GPR_U32(ctx, 29), 32), GPR_U64(ctx, 19));
        SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
        WRITE64(ADD32(GPR_U32(ctx, 29), 40), GPR_U64(ctx, 20));
        SET_GPR_S32(ctx, 19, (int32_t)SLL32(GPR_U32(ctx, 2), GPR_U32(ctx, 5) & 0x1F));
        WRITE128(ADD32(GPR_U32(ctx, 29), 0), GPR_VEC(ctx, 16));
        SET_GPR_U64(ctx, 20, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        WRITE64(ADD32(GPR_U32(ctx, 29), 24), GPR_U64(ctx, 18));
        const bool sinBits = GPR_U64(ctx, 19) == GPR_U64(ctx, 0);
        WRITE64(ADD32(GPR_U32(ctx, 29), 48), GPR_U64(ctx, 31));
        if (!sinBits)
        {
            SET_GPR_ZE32(ctx, 3, (uint8_t)READ8(ADD32(GPR_U32(ctx, 17), 36)));
            for (;;)
            {
                // Un bit por vuelta (0x279FE0-0x27A090).
                SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 128));
                if (GPR_U64(ctx, 3) != GPR_U64(ctx, 2))
                {
                    SET_GPR_ZE32(ctx, 4, (uint8_t)READ8(ADD32(GPR_U32(ctx, 17), 36)));
                }
                else
                {
                    // Byte siguiente del bufer (0x279FEC); con la posicion en 0x400 lo rellena el codigo traducido.
                    SET_GPR_S32(ctx, 18, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 60)));
                    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 1024));
                    const bool hayByte = GPR_U64(ctx, 18) != GPR_U64(ctx, 2);
                    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)75 << 16));
                    if (!hayByte)
                    {
                        ctx->in_delay_slot = false;
                        ctx->branch_pc = 0x279FF4u;
                        ctx->pc = kLectorBitsRelleno;
                        return false;
                    }
                    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 60)));
                    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 344)));
                    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 3)));
                    SET_GPR_ZE32(ctx, 2, (uint8_t)READ8(ADD32(GPR_U32(ctx, 2), 0)));
                    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 1));
                    WRITE32(ADD32(GPR_U32(ctx, 17), 60), GPR_U32(ctx, 3));
                    WRITE32(ADD32(GPR_U32(ctx, 17), 40), GPR_U32(ctx, 2));
                    SET_GPR_ZE32(ctx, 4, (uint8_t)READ8(ADD32(GPR_U32(ctx, 17), 36)));
                    ++bytes;
                }
                SET_GPR_U64(ctx, 5, GPR_U64(ctx, 20) | GPR_U64(ctx, 19));
                SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 40)));
                SET_GPR_S32(ctx, 19, (int32_t)SRL32(GPR_U32(ctx, 19), 1));
                SET_GPR_S32(ctx, 3, (int32_t)SRL32(GPR_U32(ctx, 4), 1));
                SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & GPR_U64(ctx, 4));
                WRITE8(ADD32(GPR_U32(ctx, 17), 36), (uint8_t)GPR_U32(ctx, 3));
                const bool quedaMascara = GPR_U64(ctx, 3) != GPR_U64(ctx, 0);
                if (GPR_U64(ctx, 2) != 0)
                    SET_GPR_VEC(ctx, 20, GPR_VEC(ctx, 5));
                if (!quedaMascara)
                {
                    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 128));
                    WRITE8(ADD32(GPR_U32(ctx, 17), 36), (uint8_t)GPR_U32(ctx, 2));
                }
                ++bits;
                if (GPR_U64(ctx, 19) == GPR_U64(ctx, 0))
                    break;
                SET_GPR_ZE32(ctx, 3, (uint8_t)READ8(ADD32(GPR_U32(ctx, 17), 36)));
            }
        }
        // Epilogo (0x27A094-0x27A0B4).
        SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 20) + (uint64_t)GPR_U64(ctx, 0));
        SET_GPR_VEC(ctx, 16, READ128(ADD32(GPR_U32(ctx, 29), 0)));
        SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
        SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 24)));
        SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 32)));
        SET_GPR_U64(ctx, 20, READ64(ADD32(GPR_U32(ctx, 29), 40)));
        SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 48)));
        const uint32_t vuelta = GPR_U32(ctx, 31);
        ctx->in_delay_slot = false;
        ctx->branch_pc = 0x27A0B0u;
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 64));
        ctx->pc = vuelta;
#if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, vuelta, 0x27A0B0u, 0u, PS2Runtime::GuestBranchKind::Return,
                                           "JR $ra");
#endif
        return true;
    }

    void lectorBitsNativo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t bits = 0u, bytes = 0u;
        const bool acabada = lectorBitsNucleo(rdram, ctx, runtime, bits, bytes);
        const uint32_t ciclos = kCiclosLlamada + bits * kCiclosBit + bytes * kCiclosByte;
        ++g_lectorLlamadas;
        g_lectorBits += bits;
        g_lectorCiclos += ciclos;
        // La cuenta normal de ciclos del EE (temporizadores, IOP, plazos): si vence algo, lo atiende el siguiente punto
        // de control (la vuelta del bucle que llama o el despacho siguiente).
        (void)runtime->eeCheckpointDue(ciclos);
        if (!acabada)
        {
            ++g_lectorRelevos;
            g_lectorBitsOriginal(rdram, ctx, runtime); // sigue en 0x279FFC
        }
    }

    // Sin tocar nada: si la llamada tendra que rellenar el bufer (la misma cuenta que el bucle).
    bool lectorBitsRellenara(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t lector = GPR_U32(ctx, 4);
        uint32_t vueltas = ((GPR_U32(ctx, 5) - 1u) & 31u) + 1u;
        uint32_t mascara = READ8(ADD32(lector, 36));
        uint32_t posicion = READ32(ADD32(lector, 60));
        for (; vueltas > 0u; --vueltas)
        {
            if (mascara == 0x80u)
            {
                if (posicion == 0x400u)
                    return true;
                ++posicion;
            }
            mascara >>= 1;
            if (mascara == 0u)
                mascara = 0x80u;
        }
        return false;
    }

    // Lleva la llamada hasta que vuelve a `vuelta`, como el despachador (el codigo traducido puede volver a medias en
    // un punto de control, o en la retrollamada del relleno).
    bool lectorBitsHastaVolver(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t vuelta)
    {
        for (int paso = 0; paso < 1000000 && ctx->pc != vuelta; ++paso)
        {
            const PS2Runtime::RecompiledFunction funcion =
                ctx->pc == kLectorBits ? g_lectorBitsOriginal : runtime->lookupFunction(ctx->pc);
            funcion(rdram, ctx, runtime);
        }
        return ctx->pc == vuelta;
    }

    bool contextosIguales(R5900Context a, R5900Context b)
    {
        a.insn_count = b.insn_count = 0u;
        return std::memcmp(&a, &b, sizeof(R5900Context)) == 0;
    }

    std::string diferenciasContexto(const R5900Context &a, const R5900Context &b)
    {
        std::ostringstream out;
        for (int r = 0; r < 32; ++r)
            if (std::memcmp(&a.r[r], &b.r[r], sizeof(__m128i)) != 0)
            {
                uint64_t x[2], y[2];
                std::memcpy(x, &a.r[r], 16);
                std::memcpy(y, &b.r[r], 16);
                out << " r" << r << "=" << std::hex << x[1] << ":" << x[0] << "/" << y[1] << ":" << y[0] << std::dec;
            }
        if (a.pc != b.pc)
            out << " pc=" << std::hex << a.pc << "/" << b.pc << std::dec;
        if (a.in_delay_slot != b.in_delay_slot || a.branch_pc != b.branch_pc)
            out << " ramal=" << std::hex << a.branch_pc << "/" << b.branch_pc << std::dec;
        return out.str().empty() ? std::string(" (otros campos)") : out.str();
    }

    // Casos sinteticos: un lector, un objeto con su retrollamada de relleno (0x1A3728, solo jr $ra) y la pila en una
    // zona del monton del runtime; registros al azar.
    void verificarLectorBitsSintetico(uint8_t *rdram, const R5900Context &base, PS2Runtime *runtime)
    {
        constexpr uint32_t kTam = 0x400u, kCasos = 20000u, kVuelta = 0x00FFFFF0u, kRetrollamada = 0x001A3728u;
        const uint32_t zona = runtime->guestMalloc(kTam, 64u);
        if (zona == 0u || PS2Runtime::isSpecialAddress(zona))
        {
            std::cout << "[f1-06:lector-bits] sinteticos: sin memoria" << std::endl;
            return;
        }
        const uint32_t lector = zona, objeto = zona + 0x80u, entrada = zona + 0xC0u, pila = zona + kTam;
        uint8_t *memoria = rdram + (zona & PS2_RAM_MASK);
        std::mt19937 azar(0x0171u);
        auto al = [&](uint32_t n) { return static_cast<uint32_t>(azar() % n); };
        static const uint32_t kBordes[] = {0u, 1u, 2u, 7u, 8u, 9u, 15u, 16u, 17u, 24u, 31u, 32u, 33u, 63u, 64u, 65u};
        std::vector<uint8_t> inicial(kTam), finalNativo(kTam);
        uint32_t iguales = 0u, distintos = 0u, conRelleno = 0u, sinVolver = 0u;
        for (uint32_t caso = 0u; caso < kCasos; ++caso)
        {
            for (uint8_t &b : inicial)
                b = static_cast<uint8_t>(azar());
            auto poner32 = [&](uint32_t direccion, uint32_t valor) { std::memcpy(&inicial[direccion - zona], &valor, 4); };
            const uint32_t m = al(10u);
            inicial[lector + 0x24u - zona] = m < 8u ? static_cast<uint8_t>(0x80u >> m) : static_cast<uint8_t>(azar());
            const uint32_t p = al(4u);
            poner32(lector + 0x3Cu, p == 0u ? 0u : p == 1u ? 0x3FCu + al(5u) : al(0x400u));
            poner32(lector + 0x40u, al(0x800u));
            poner32(lector + 0x14u, objeto);
            poner32(objeto + 0x10u, entrada);
            poner32(entrada + 0xCu, kRetrollamada);
            const uint32_t q = al(3u);
            const uint32_t n = q == 0u ? kBordes[al(16u)] : q == 1u ? al(41u) : static_cast<uint32_t>(azar());
            R5900Context a = base;
            for (int r = 1; r < 32; ++r)
            {
                const uint64_t v[2] = {(static_cast<uint64_t>(azar()) << 32) | azar(), (static_cast<uint64_t>(azar()) << 32) | azar()};
                std::memcpy(&a.r[r], v, 16);
            }
            auto bajo32 = [&](int r, uint32_t valor) { std::memcpy(&a.r[r], &valor, 4); };
            bajo32(4, lector);
            bajo32(5, n);
            bajo32(29, pila);
            bajo32(31, kVuelta);
            a.pc = kLectorBits;
            R5900Context b = a;

            std::memcpy(memoria, inicial.data(), kTam);
            const bool relleno = lectorBitsRellenara(rdram, &a, runtime);
            conRelleno += relleno ? 1u : 0u;
            lectorBitsNativo(rdram, &a, runtime);
            const bool vuelveA = lectorBitsHastaVolver(rdram, &a, runtime, kVuelta);
            std::memcpy(finalNativo.data(), memoria, kTam);

            std::memcpy(memoria, inicial.data(), kTam);
            const bool vuelveB = lectorBitsHastaVolver(rdram, &b, runtime, kVuelta);
            if (!vuelveA || !vuelveB)
                ++sinVolver;
            const bool igual = vuelveA && vuelveB && contextosIguales(a, b) &&
                               std::memcmp(finalNativo.data(), memoria, kTam) == 0;
            if (igual)
                ++iguales;
            else if (++distintos <= 5u)
                std::cout << "[f1-06:lector-bits] sintetico " << caso << " distinto: n=" << n << " relleno=" << relleno
                          << (std::memcmp(finalNativo.data(), memoria, kTam) != 0 ? " memoria" : "")
                          << diferenciasContexto(a, b) << std::endl;
        }
        runtime->guestFree(zona);
        g_lectorLlamadas = g_lectorBits = g_lectorCiclos = g_lectorRelevos = 0u; // la prueba no cuenta
        std::cout << "[f1-06:lector-bits] sinteticos: casos=" << kCasos << " iguales=" << iguales << " distintos="
                  << distintos << " conRelleno=" << conRelleno << " sinVolver=" << sinVolver << std::endl;
    }

    void lectorBits(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kLectorBits)
        {
            g_lectorBitsOriginal(rdram, ctx, runtime);
            return;
        }
        if (g_lectorBitsVerificar > 0)
        {
            if (!g_lectorBitsSinteticos)
            {
                g_lectorBitsSinteticos = true;
                verificarLectorBitsSintetico(rdram, *ctx, runtime);
            }
            if (!lectorBitsRellenara(rdram, ctx, runtime))
            {
                static uint32_t iguales = 0u, distintos = 0u, interrumpidas = 0u;
                const uint32_t lector = GPR_U32(ctx, 4), sp = GPR_U32(ctx, 29), n = GPR_U32(ctx, 5);
                uint8_t *struc = rdram + (lector & PS2_RAM_MASK), *pila = rdram + ((sp - 0x40u) & PS2_RAM_MASK);
                uint8_t antesLector[0x50], antesPila[0x40], nativoLector[0x50], nativoPila[0x40];
                std::memcpy(antesLector, struc, sizeof(antesLector));
                std::memcpy(antesPila, pila, sizeof(antesPila));
                R5900Context nativo = *ctx;
                uint32_t bits = 0u, bytes = 0u;
                (void)lectorBitsNucleo(rdram, &nativo, runtime, bits, bytes);
                std::memcpy(nativoLector, struc, sizeof(nativoLector));
                std::memcpy(nativoPila, pila, sizeof(nativoPila));
                std::memcpy(struc, antesLector, sizeof(antesLector));
                std::memcpy(pila, antesPila, sizeof(antesPila));
                const uint32_t vuelta = GPR_U32(ctx, 31);
                g_lectorBitsOriginal(rdram, ctx, runtime);
                if (ctx->pc != vuelta)
                    ++interrumpidas; // sigue en el despachador; no se compara
                else if (contextosIguales(nativo, *ctx) && std::memcmp(nativoLector, struc, sizeof(nativoLector)) == 0 &&
                         std::memcmp(nativoPila, pila, sizeof(nativoPila)) == 0)
                    ++iguales;
                else if (++distintos <= 5u)
                    std::cout << "[f1-06:lector-bits] real distinta: n=" << n << diferenciasContexto(nativo, *ctx)
                              << std::endl;
                if (--g_lectorBitsVerificar == 0)
                    std::cout << "[f1-06:lector-bits] reales: iguales=" << iguales << " distintas=" << distintos
                              << " interrumpidas=" << interrumpidas << std::endl;
                return;
            }
        }
        if (g_lectorBitsNativo)
            lectorBitsNativo(rdram, ctx, runtime);
        else
            g_lectorBitsOriginal(rdram, ctx, runtime);
    }

    void instalarLectorBits(PS2Runtime &runtime)
    {
        const char *nativo = std::getenv("F1_LECTOR_BITS_NATIVO");
        g_lectorBitsNativo = nativo == nullptr || std::atoi(nativo) != 0;
        if (const char *verificar = std::getenv("F1_LECTOR_BITS_VERIFICAR"))
            g_lectorBitsVerificar = std::max(1, std::atoi(verificar));
        std::cout << "[f1-06:lector-bits] nativo = " << (g_lectorBitsNativo ? 1 : 0)
                  << (g_lectorBitsVerificar > 0 ? " (verificando)" : "") << std::endl;
        if (g_lectorBitsNativo || g_lectorBitsVerificar > 0)
            instalarChivato(runtime, kLectorBits, lectorBits, g_lectorBitsOriginal);
    }

    // Diagnostico del desfase de 1 linea (fase 6, T2). Cada fotograma func_143BF8 mide con T3 (0x10001800, en lineas)
    // el tiempo desde el inicio del fotograma (*0x4B1CC0) y ajusta dos factores de calidad (0x4116C8 v0, 0x4116CC v1;
    // func_2FC6B8 los divide entre 2, func_2FC6E0 les suma su paso de 0x46C3A8): v1 baja si el fotograma pasa de 301
    // lineas y v0 si se pierden dos VBLANK seguidos. func_30FFB8 solo dibuja el efecto func_31A740 (alfa del bufer a 0 y
    // brillo) con min(v0, v1) > 0,75. F1_CALIDAD_DIAG=1: [f1-06:calidad] cada 50 fotogramas.
    // Lejania T4: v1 = v0 by default. The v1 test (T3 lines since the frame start >= 301) never holds on the PS2 at
    // 50 fps, where the VU1 and the GS run in parallel with the EE, but always does in the port (VU1/VIF1 run serially
    // on the EE thread and T3 follows wall time at 15734 Hz, so a steady frame is ~314 lines): v1 sat at 0 in races,
    // the car LOD switched sqrt(6) = 2.45 times closer than intended (see camaraCarrera) and func_31A740 was off.
    // After func_143BF8 the hook copies v0 to v1, so v1 only drops when frames really miss VBLANK, like v0.
    // The option detalle_coches (GRAFICOS PC row DETALLE DE LOS COCHES, PS2X_DETALLE_COCHES, 1 by default) = 0 keeps
    // the game's v1; it is read every frame, so the row takes effect at once. Its levels 2 and 3 (Lejania T5) keep
    // v1 = v0 too and draw more than the PS2 (see lodCoches). F1_CALIDAD_V1=0 (or =juego), the variable
    // of the measurements before the option, still keeps the game's v1 whatever the option says; A/B mode 180 does the
    // same in its windows. The diagnostic prints both the final v1 and the game's own decision (v1juego, before the
    // copy).
    constexpr uint32_t kMedirFotograma = 0x00143BF8u;
    constexpr uint32_t kEfectoAlfa = 0x0031A740u;
    constexpr uint32_t kCalidadV0 = 0x004116C8u;
    constexpr uint32_t kCalidadV1 = 0x004116CCu;
    PS2Runtime::RecompiledFunction g_medirFotogramaOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_efectoAlfaOriginal = nullptr;
    uint32_t g_efectosAlfa = 0u;
    bool g_calidadV1ComoV0 = true; // false with F1_CALIDAD_V1=0|juego

    // 0 reduced, 1 as on the PS2, 2 high, 3 maximum. The windows of A/B modes 181 and 182 use 2 and 3 whatever the
    // option says, so their cost is measured against the option's level (as on the PS2 by default) in the same race.
    int nivelDetalleCoches()
    {
        const int modo = g_ps2AbMode.load(std::memory_order_relaxed);
        if (modo == 181 || modo == 182)
            return modo - 179;
#if defined(PS2X_ENABLE_DEBUG_UI)
        float valor = 0.0f;
        if (PS2OptionsMenu::gameOptionNumber("detalle_coches", valor))
            return std::clamp(static_cast<int>(std::lround(valor)), 0, 3);
#endif
        const char *variable = std::getenv("PS2X_DETALLE_COCHES");
        return variable == nullptr ? 1 : std::clamp(std::atoi(variable), 0, 3);
    }

    bool detalleCochesPs2()
    {
        return nivelDetalleCoches() >= 1;
    }

    void igualarCalidadV1(uint8_t *rdram)
    {
        if (g_calidadV1ComoV0 && g_ps2AbMode.load(std::memory_order_relaxed) != 180 && detalleCochesPs2())
            std::memcpy(rdram + (kCalidadV1 & PS2_RAM_MASK), rdram + (kCalidadV0 & PS2_RAM_MASK), sizeof(float));
    }

    void medirFotograma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_medirFotogramaOriginal(rdram, ctx, runtime);
        igualarCalidadV1(rdram);
    }

    void trazarEfectoAlfa(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ++g_efectosAlfa;
        g_efectoAlfaOriginal(rdram, ctx, runtime);
    }

    void trazarMedirFotograma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t fotogramas = 0u, bajasV0 = 0u, bajasV1 = 0u, largos = 0u, maximo = 0u;
        static uint64_t suma = 0u;
        const uint32_t t3 = runtime->memory().read32(0x10001800u) & 0xFFFFu;
        const uint32_t transcurrido = (t3 - leer32(rdram, 0x004B1CC0u)) & 0xFFFFu;
        const float v0 = leerFloat(rdram, kCalidadV0), v1 = leerFloat(rdram, kCalidadV1);
        g_medirFotogramaOriginal(rdram, ctx, runtime);
        const float n1Juego = leerFloat(rdram, kCalidadV1);
        igualarCalidadV1(rdram);
        const float n0 = leerFloat(rdram, kCalidadV0), n1 = leerFloat(rdram, kCalidadV1);
        const uint32_t despues = (runtime->memory().read32(0x10001800u) - leer32(rdram, 0x004B1CC0u)) & 0xFFFFu;
        ++fotogramas;
        bajasV0 += n0 < v0 ? 1u : 0u;
        bajasV1 += n1Juego < v1 ? 1u : 0u;
        largos += transcurrido >= 0x12Du ? 1u : 0u;
        suma += transcurrido;
        maximo = std::max(maximo, transcurrido);
        // Fotogramas de mas de 1.000 lineas (~64 ms) con su duracion real, de fin a fin (fase 6 T5).
        static double s_finAnterior = 0.0;
        const double ahora = segundosDesdeArranque();
        if (transcurrido >= 1000u)
            std::printf("[f1-06:calidad-largo] t=%.3f lineas=%u ms=%.1f\n", ahora, transcurrido,
                        (ahora - s_finAnterior) * 1000.0);
        s_finAnterior = ahora;
        if (fotogramas % 50u == 0u)
        {
            // Lo que ha cobrado el lector de bits nativo en estos 50 fotogramas (fase 6 T5).
            if (g_lectorLlamadas != 0u)
                std::printf("[f1-06:lector-bits] t=%.1f llamadas=%llu bits=%llu ciclos=%llu relevos=%llu\n", ahora,
                            static_cast<unsigned long long>(g_lectorLlamadas),
                            static_cast<unsigned long long>(g_lectorBits),
                            static_cast<unsigned long long>(g_lectorCiclos),
                            static_cast<unsigned long long>(g_lectorRelevos));
            g_lectorLlamadas = g_lectorBits = g_lectorCiclos = g_lectorRelevos = 0u;
            // v1juego goes last: logs/f6-t6-analizar.py parses the fields before it.
            std::printf("[f1-06:calidad] t=%.1f v0=%.3f v1=%.3f lineas media=%llu max=%u >=301:%u bajas v0=%u v1=%u "
                        "efecto=%u ultimo=%u/%u vblank=%u v1juego=%.3f\n",
                        segundosDesdeArranque(), n0, n1, static_cast<unsigned long long>(suma / 50u), maximo, largos,
                        bajasV0, bajasV1, g_efectosAlfa, transcurrido, despues, leer32(rdram, kContadorVblank),
                        n1Juego);
            std::fflush(stdout);
            bajasV0 = bajasV1 = largos = maximo = 0u;
            suma = 0u;
            g_efectosAlfa = 0u;
        }
    }

    // Fase 3 T4: 60 fotogramas por segundo. El paso de la logica no es una constante suelta: func_1E87B8(modo) copia la
    // tabla del modo a cuatro globales que leen la fisica, la IA y el crono (cientos de lecturas): 0x3FE58C paso en
    // segundos, 0x3FE590 paso nominal, 0x3FE594 fotogramas/s y 0x3FE598 fotogramas/s nominales. Modos: 0 = 25 (0,04 s),
    // 1 = 30, 2 = 50 (0,01999 s y 50,025/s), 3 = 60 (0,0166 s y 60/s). El modo vive en 0x3FEBDC (func_255DB8 lo fija,
    // func_255DC8 lo lee; func_1E0998 escala otra tabla por 50/60, 25/60 o 30/60): el arranque (func_100570) pide al GS
    // PAL y fija el modo 2, o NTSC y el 3; func_1002C0 (la nativa de Lua game.refreshmode, 0x15B378, que ningun trozo
    // usa en la version PAL) hace lo mismo con 2 = NTSC, 3 = PAL o 0x50 = 480p. Es decir: el juego ya trae el modo de 60
    // (el de la version NTSC); con F1_FOTOGRAMAS=60 el modo 2 se cambia por el 3 y el VBLANK pasa a 60 Hz
    // (ps2SetVideoModePal(false)) sin tocar la salida PAL del GS. El paso ha de ser fijo: el reloj de la carrera sale de
    // contadores de fotogramas por el paso (con un paso de 1/60 a 1/50 segun lo que tarde cada fotograma, probado en la
    // fase 3 T4, salta hacia atras y adelante varios segundos).
    // Solo para pruebas (no es una opcion del jugador; fase 3 T4): la logica va bien (vuelta del piloto 2:01,5 frente a
    // 2:02,7 a 50, el reloj sigue a los fotogramas), pero el EE no llega a 60 (salida 42-49 fotogramas/s, carrera 47-60:
    // el juego va al 66-93 % del tiempo real, frente al 80-100 % a 50) y los videos .ipf van al ritmo del VBLANK (30
    // imagenes/s en vez de 25).
    constexpr uint32_t kFijarModoRitmo = 0x00255DB8u;
    constexpr uint32_t kTablasRitmo = 0x001E87B8u;
    constexpr uint32_t kPasoLogica = 0x003FE58Cu;
    PS2Runtime::RecompiledFunction g_fijarModoRitmoOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_tablasRitmoOriginal = nullptr;
    bool g_sesentaFps = false;
    // Reloj de la sesion de carrera en segundos (lo avanza el paso de la logica; sale igual en 0x3FBE00 y 0x3FEB30): el
    // crono "Current" del HUD es este reloj menos el instante de la salida.
    constexpr uint32_t kRelojCarrera = 0x003FE5C0u;
    std::atomic<uint8_t *> g_ritmoRdram{nullptr};

    void trazarFijarModoRitmo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_ritmoRdram.store(rdram, std::memory_order_relaxed);
        const uint32_t modo = GPR_U32(ctx, 4);
        if (g_sesentaFps && modo == 2u)
        {
            SET_GPR_U32(ctx, 4, 3u);
            ps2SetVideoModePal(false);
            // func_1E87B8 solo se llama al cargar cada carrera: hasta entonces los menus usarian el paso de 50 del ELF
            // a 60 VBLANK/s (un 20 % mas rapidos). Los valores del modo 3 de su tabla (0x1E881C).
            const uint32_t tabla[4] = {0x3C87FCB9u, 0x3C888889u, 0x42700000u, 0x42700000u};
            std::memcpy(rdram + kPasoLogica, tabla, sizeof(tabla));
        }
        std::cout << "[f1-06:ritmo] t=" << segundosDesdeArranque() << " modo " << modo << " -> " << GPR_U32(ctx, 4)
                  << std::endl;
        g_fijarModoRitmoOriginal(rdram, ctx, runtime);
    }

    void trazarTablasRitmo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (g_sesentaFps && GPR_U32(ctx, 4) == 2u)
            SET_GPR_U32(ctx, 4, 3u);
        const uint32_t modo = GPR_U32(ctx, 4);
        g_tablasRitmoOriginal(rdram, ctx, runtime);
        std::cout << "[f1-06:ritmo] t=" << segundosDesdeArranque() << " tablas del modo " << modo << ": paso "
                  << leerFloat(rdram, kPasoLogica) << " s (nominal " << leerFloat(rdram, kPasoLogica + 4u) << "), "
                  << leerFloat(rdram, kPasoLogica + 8u) << " fotogramas/s (nominal " << leerFloat(rdram, kPasoLogica + 12u)
                  << ")" << std::endl;
    }

    // F1_RITMO_DIAG=1: [f1-06:ritmo] cada 10 s con las imagenes de video .ipf decodificadas por segundo (func_2D3CD0, una
    // por imagen) y los VBLANK del juego, para ver si los fondos de video siguen el VBLANK o su propio reloj.
    constexpr uint32_t kImagenIpf = 0x002D3CD0u;
    PS2Runtime::RecompiledFunction g_imagenIpfOriginal = nullptr;

    void trazarImagenIpf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint64_t imagenes = 0;
        static double desde = segundosDesdeArranque();
        static uint32_t vblanksDesde = leer32(rdram, kContadorVblank);
        static bool primera = true;
        ++imagenes;
        if (std::exchange(primera, false))
            std::cout << "[f1-06:ritmo] t=" << segundosDesdeArranque() << " primera imagen ipf" << std::endl;
        const double ahora = segundosDesdeArranque();
        if (ahora - desde >= 10.0)
        {
            const uint32_t vblanks = leer32(rdram, kContadorVblank);
            std::cout << "[f1-06:ritmo] t=" << ahora << " imagenes ipf/s=" << imagenes / (ahora - desde)
                      << " vblanksJuego/s=" << (vblanks - vblanksDesde) / (ahora - desde) << std::endl;
            imagenes = 0;
            desde = ahora;
            vblanksDesde = vblanks;
        }
        g_imagenIpfOriginal(rdram, ctx, runtime);
    }

    void instalarRitmo(PS2Runtime &runtime)
    {
        const char *diagnostico = std::getenv("F1_RITMO_DIAG");
        const bool diag = diagnostico != nullptr && std::atoi(diagnostico) != 0;
        if (diag)
        {
            instalarChivato(runtime, kImagenIpf, trazarImagenIpf, g_imagenIpfOriginal);
            // Y cada 5 s el reloj de la carrera frente al tiempo real (el crono: 100 % = el juego va a tiempo real).
            std::thread([]()
                        {
                float relojAntes = 0.0f;
                double antes = segundosDesdeArranque();
                for (;;)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    const uint8_t *rdram = g_ritmoRdram.load(std::memory_order_relaxed);
                    if (rdram == nullptr)
                        continue;
                    const double ahora = segundosDesdeArranque();
                    const float reloj = leerFloat(rdram, kRelojCarrera);
                    if (reloj != relojAntes)
                        std::cout << "[f1-06:ritmo] t=" << ahora << " reloj de carrera " << reloj << " s ("
                                  << 100.0 * (reloj - relojAntes) / (ahora - antes) << " % del tiempo real)" << std::endl;
                    relojAntes = reloj;
                    antes = ahora;
                } })
                .detach();
        }
        const char *valor = std::getenv("F1_FOTOGRAMAS");
        g_sesentaFps = valor != nullptr && std::atoi(valor) == 60;
        if (!g_sesentaFps && !diag)
            return; // 50: el juego tal cual, sin ganchos
        std::cout << "[f1-06:ritmo] fotogramas por segundo: " << (g_sesentaFps ? 60 : 50) << std::endl;
        instalarChivato(runtime, kFijarModoRitmo, trazarFijarModoRitmo, g_fijarModoRitmoOriginal);
        instalarChivato(runtime, kTablasRitmo, trazarTablasRitmo, g_tablasRitmoOriginal);
    }

    // Fotogramas que llegan tarde (parche 0106). El juego espera al VBLANK con WaitSema (func_1078A0) del semaforo de
    // 0x4B11EC al final de cada fotograma (func_2D5538; tambien el bucle de las pantallas de carga en 0x2D4D50), salvo
    // si ha pasado un VBLANK mientras esperaba a que se acabara de dibujar. En la PS2 el fotograma cabe en 20 ms; aqui,
    // si tarda 21, el VBLANK pasa durante la logica, el juego espera al siguiente y cae a 25 fotogramas/s. Como en un
    // emulador que no da abasto (el tiempo emulado se estira), si ya ha pasado un VBLANK desde que el juego desperto de
    // la espera anterior, esta no se hace: el fotograma de 21 ms se ve a ~48/s. Con el juego adelantado espera al VBLANK
    // como en la PS2 (no pasa del 100 %). F1_VBLANK_ELASTICO=0 lo apaga.
    constexpr uint32_t kWaitSema = 0x001078A0u;
    constexpr uint32_t kSemaforoVblank = 0x004B11ECu;
    PS2Runtime::RecompiledFunction g_waitSemaOriginal = nullptr;
    uint32_t g_vblankAlDespertar = 0u; // contador de VBLANK del juego (0x404FF4) cuando volvio de la ultima espera
    std::atomic<uint64_t> g_esperasSaltadas{0u};

    void trazarWaitSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t semaforo = GPR_U32(ctx, 4);
        if (ctx->pc == kWaitSema && semaforo == leer32(rdram, kSemaforoVblank) && semaforo != 0u)
        {
            const uint32_t contador = leer32(rdram, kContadorVblank);
            if (contador != g_vblankAlDespertar)
            {
                // Ya ha pasado un VBLANK en este fotograma: sigue sin esperar (WaitSema devuelve el semaforo).
                g_vblankAlDespertar = contador;
                g_esperasSaltadas.fetch_add(1u, std::memory_order_relaxed);
                SET_GPR_S32(ctx, 2, static_cast<int32_t>(semaforo));
                ctx->pc = GPR_U32(ctx, 31);
                return;
            }
            g_vblankAlDespertar = contador + 1u; // despertara con el VBLANK siguiente
        }
        g_waitSemaOriginal(rdram, ctx, runtime);
    }

    PS2Runtime::RecompiledFunction g_lanzarDmaVif1Original = nullptr;
    PS2Runtime::RecompiledFunction g_irqVif1Original = nullptr;
    PS2Runtime::RecompiledFunction g_esperaVif1Original = nullptr;

    // Ultimos sucesos del VIF1 (L lanzar DMA, I interrupcion con la marca que ve el manejador, W espera). Se vuelcan
    // si la espera agota su plazo (func_2D56D0), con los registros del VIF1 y del DMAC.
    struct SucesoVif1
    {
        double t = 0.0;
        char tipo = 0;
        uint32_t datos[7] = {};
    };
    SucesoVif1 g_sucesosVif1[128];
    uint64_t g_numSucesosVif1 = 0;

    void anotarVif1(char tipo, uint32_t a, uint32_t b, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0, uint32_t f = 0,
                    uint32_t g = 0)
    {
        SucesoVif1 &s = g_sucesosVif1[g_numSucesosVif1++ % 128u];
        s.t = segundosDesdeArranque();
        s.tipo = tipo;
        s.datos[0] = a;
        s.datos[1] = b;
        s.datos[2] = c;
        s.datos[3] = d;
        s.datos[4] = e;
        s.datos[5] = f;
        s.datos[6] = g;
    }

    // Fin del DMA del GIF (func_2CAFD0, manejador DMAC del canal 2) y iEnableDmac/iDisableDmac (func_10B198/func_10B1B8):
    // la subida de texturas por PATH3 que lanza la marca 0x9000 del VIF1 termina en ese manejador, que reanuda el VIF1.
    constexpr uint32_t kFinDmaGif = 0x002CAFD0u;
    constexpr uint32_t kHabilitarDmacI = 0x0010B198u;
    constexpr uint32_t kDeshabilitarDmacI = 0x0010B1B8u;
    PS2Runtime::RecompiledFunction g_finDmaGifOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_habilitarDmacIOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_deshabilitarDmacIOriginal = nullptr;

    void trazarFinDmaGif(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        anotarVif1('G', GPR_U32(ctx, 4), leer32(rdram, 0x00404F68u), leer32(rdram, 0x00404F6Cu),
                   runtime->memory().read32(0x1000A000u), runtime->memory().read32(0x1000A030u),
                   runtime->memory().read32(0x1000E010u));
        g_finDmaGifOriginal(rdram, ctx, runtime);
    }

    void trazarHabilitarDmacI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        anotarVif1('E', GPR_U32(ctx, 4), GPR_U32(ctx, 31));
        g_habilitarDmacIOriginal(rdram, ctx, runtime);
    }

    void trazarDeshabilitarDmacI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        anotarVif1('D', GPR_U32(ctx, 4), GPR_U32(ctx, 31));
        g_deshabilitarDmacIOriginal(rdram, ctx, runtime);
    }

    void trazarLanzarDmaVif1(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        if (veces++ < 20)
            std::cout << "[f1-06] lanzar DMA VIF1 tag=0x" << std::hex << GPR_U32(ctx, 5) << " contador="
                      << std::dec << static_cast<int32_t>(leer32(rdram, kContadorVif1)) << std::endl;
        anotarVif1('L', GPR_U32(ctx, 4), GPR_U32(ctx, 5), leer32(rdram, kContadorVif1));
        // Fotogramas/s en cualquier pantalla (tambien en carrera, donde no se llama al render del front-end): cada
        // fotograma manda un paquete al VIF1.
        // F1_AB=<segundos>: comparacion A/B en la misma partida. Cada ventana de ese largo alterna g_ps2AbMode entre
        // 0 (codigo actual) y F1_AB_MODO (por defecto 1: version anterior de las optimizaciones que la consultan; 2:
        // GS sin los hilos de bandas; 3: VU1 sin bloques; 4: IOP sin lotes; 5: bloques de la VU1 sin upperFn; 6: cadenas
        // del DMA del VIF1 copiadas como antes del 0039; 7: nucleos del procesador como decida Windows, ver abajo; 8: el
        // hilo del GS se duerme en cuanto vacia su cola; 9: pares de la VU1 decodificados de nuevo tras cada subida de
        // microcodigo; 10: inferiores WAITQ, B, IBxx y FCxx de los bloques de la VU1 por execLower; 11: DIV, SQRT y
        // RSQRT por execLower; 12: cola del GS con el estado entero en cada primitiva; 13: filas de triangulos del GS
        // con el escritor y el muestreador por puntero; 14: bandas del GS solo en los nucleos eficientes; 15: dependencias del GS por paginas; 16: inferiores de
        // los bloques de la VU1 por un solo switch en vez de una funcion por operacion; 17: BAL, JR, JALR, FCSET, MFP,
        // XTOP, XITOP y XGKICK de los bloques por execLower; 18: cada bloque de la VU1 en una llamada propia, sin
        // inlinear en runBlocks; 19: dependencias de las bandas del GS por la union de los bloques pendientes en vez
        // del ultimo trabajo de cada bloque; 20: cola del GS publicada con barrera completa en cada orden; 21:
        // resultados superiores de la VU1 por un temporal y un switch, sin upperEarly; 22: el planificador del EE toma su
        // cerrojo y lee el reloj en cada punto de control; 23: frontend del GS con el estado entero copiado y comparado
        // en cada primitiva; 24: UNPACK del VIF1 por el bucle general, sin funciones por formato, y los de mascara o
        // suma campo a campo; 25: pares de la VU1 por contenido
        // con 4 versiones por direccion en vez de 8; 26: banderas de los bloques de la VU1 confirmadas releyendo la
        // entrada antes de cada prueba) y el informe dice con cual se midio ("modo=0..26"). F1_AB_MODO puede ser una lista ("5,6"): las ventanas B van
        // rotando (0, 5, 0, 6, 0, 5...). La carga del PC varia mucho entre arranques; asi se compara la misma escena.
        // Nucleos (f1_fallos.cpp): en el primer paquete el proceso pide que Windows no lo regule (EcoQoS) y el hilo del
        // EE se queda en los nucleos de rendimiento (F1_NUCLEOS=0 lo deja todo como decida Windows). El informe dice en
        // que parte de los paquetes el hilo del EE estaba en un nucleo eficiente ("nucleosE=").
        {
            static const double s_ventanaAb = []
            {
                const char *valor = std::getenv("F1_AB");
                return valor != nullptr ? std::atof(valor) : 0.0;
            }();
            static const std::vector<int> s_modosB = []
            {
                std::vector<int> modos;
                const char *valor = std::getenv("F1_AB_MODO");
                for (const char *c = valor != nullptr ? valor : "1"; *c != '\0';)
                {
                    char *fin = nullptr;
                    const long modo = std::strtol(c, &fin, 10);
                    if (fin == c)
                        break;
                    modos.push_back(static_cast<int>(modo));
                    c = *fin == ',' ? fin + 1 : fin;
                }
                if (modos.empty())
                    modos.push_back(1);
                return modos;
            }();
            static size_t s_siguienteB = 0;
            // F1_AB_FIJO=<modo> (parche 0069): ese modo toda la partida, sin F1_AB (para perfilar uno solo).
            static const bool s_fijo = []
            {
                const char *valor = std::getenv("F1_AB_FIJO");
                if (valor != nullptr)
                    g_ps2AbMode.store(std::atoi(valor), std::memory_order_relaxed);
                return valor != nullptr;
            }();
            (void)s_fijo;
            static const bool s_nucleos = []
            {
                const char *valor = std::getenv("F1_NUCLEOS");
                const bool activo = valor == nullptr || std::atoi(valor) != 0;
                if (activo)
                    f1NucleosDeRendimiento(true);
                return activo;
            }();
            // F1_EE_PROPIO=1 (parche 0069): el hilo del EE en un nucleo de rendimiento para el solo (sus dos hilos) y el
            // resto del proceso fuera de ese nucleo (ps2EeOwnCore). En A/B no gana nada medible (+0,8 % +-1,6, ESTADO.md),
            // asi que no va por defecto. El modo A/B 47 lo invierte en sus ventanas.
            static const bool s_propio = []
            {
                const char *valor = std::getenv("F1_EE_PROPIO");
                if (!s_nucleos || valor == nullptr || std::atoi(valor) == 0)
                    return false;
                const int nucleo = ps2EeOwnCore(true);
                std::cout << "[f1-06] nucleo propio del EE: " << nucleo << std::endl;
                return nucleo >= 0;
            }();
            // Diagnostico de cada ventana (parche 0069): CPU del hilo del EE, su espera al GS, ns por par de la VU1,
            // frecuencia del nucleo (cadena de multiplicaciones) y procesadores logicos aparcados.
            struct Diagnostico
            {
                uint64_t cpuHilo = 0, tsc = 0, esperaGs = 0, vu1Ns = 0, vu1Pares = 0, despertares = 0, esperaVu1 = 0;
                uint64_t cambios = 0, vblanks = 0; // cambios de bufer de pantalla y VBLANK (parche 0106)
                uint64_t fines = 0, esperas = 0, vblanksJuego = 0;
                uint64_t ciclosHilo = 0; // QueryThreadCycleTime del hilo del EE (1.4 T2)
            };
            static Diagnostico s_antes{f1CpuHilo(), f1Tsc(), g_ps2GsEeWaitNs.load(), g_ps2Vu1HostNs.load(),
                                       g_ps2Vu1HostPairs.load(), g_ps2GsWakes.load(), g_ps2Vu1GsWaitNs.load(),
                                       g_ps2DisplayFlips.load(), runtime->memory().gs().vsyncTick.load(),
                                       g_finesFotograma.load(), g_esperasVblank.load(), leer32(rdram, kContadorVblank),
                                       f1CiclosHilo()};
            static uint64_t s_cadenaMin = ~0ull, s_cadenaSuma = 0, s_cadenas = 0, s_aparcados = 0, s_muestrasAparcados = 0;
            static uint32_t paquetes = 0;
            static uint32_t enNucleoE = 0;
            static double ultimo = 0.0;
            ++paquetes;
            if (f1EnNucleoEficiente())
                ++enNucleoE;
            if ((paquetes & 15u) == 0u)
            {
                const uint64_t cadena = f1MedirCadena();
                s_cadenaMin = cadena < s_cadenaMin ? cadena : s_cadenaMin;
                s_cadenaSuma += cadena;
                ++s_cadenas;
            }
            if ((paquetes & 63u) == 1u)
            {
                s_aparcados += f1NucleosAparcados();
                ++s_muestrasAparcados;
            }
            const double ahora = segundosDesdeArranque();
            if (ahora - ultimo >= (s_ventanaAb > 0.0 ? s_ventanaAb : 10.0))
            {
                std::cout << "[f1-06:vif1fps] t=" << ahora << " paquetes/s=" << paquetes / (ahora - ultimo);
                if (s_ventanaAb > 0.0)
                {
                    const int modo = g_ps2AbMode.load(std::memory_order_relaxed);
                    std::cout << " modo=" << modo;
                    // F1_AB_DESDE=<s>: todo en el modo 0 hasta esos segundos desde el arranque (el modo 83, VBLANK a
                    // 60 Hz, cuelga la comprobacion de la tarjeta de memoria si alterna en los menus).
                    static const double s_abDesde = []
                    {
                        const char *valor = std::getenv("F1_AB_DESDE");
                        return valor != nullptr ? std::atof(valor) : 0.0;
                    }();
                    const int nuevo = modo == 0 && ahora >= s_abDesde ? s_modosB[s_siguienteB++ % s_modosB.size()] : 0;
                    g_ps2AbMode.store(nuevo, std::memory_order_relaxed);
                    if (s_nucleos && (nuevo == 7 || modo == 7))
                        f1NucleosDeRendimiento(nuevo != 7);
                    if (s_nucleos && (nuevo == 47 || modo == 47))
                        ps2EeOwnCore(nuevo == 47 ? !s_propio : s_propio);
                }
                std::cout << " nucleosE=" << (100u * enNucleoE) / (paquetes != 0u ? paquetes : 1u) << "%";
                {
                    const Diagnostico d{f1CpuHilo(), f1Tsc(), g_ps2GsEeWaitNs.load(), g_ps2Vu1HostNs.load(),
                                        g_ps2Vu1HostPairs.load(), g_ps2GsWakes.load(), g_ps2Vu1GsWaitNs.load(),
                                        g_ps2DisplayFlips.load(), runtime->memory().gs().vsyncTick.load(),
                                       g_finesFotograma.load(), g_esperasVblank.load(), leer32(rdram, kContadorVblank),
                                       f1CiclosHilo()};
                    const double segundos = ahora - ultimo;
                    const double tsc = static_cast<double>(d.tsc - s_antes.tsc);
                    const double tscHz = tsc / segundos;
                    const double pares = static_cast<double>(d.vu1Pares - s_antes.vu1Pares);
                    char texto[480];
                    std::snprintf(texto, sizeof(texto),
                                  " cpuEE=%.1f%% esperaGS=%.1f%% vu1=%.1f%% nsPar=%.1f ghz=%.2f ghzMax=%.2f aparcados=%.1f despertares=%.0f esperaVu1=%.1f%% cambios/s=%.1f vblanks/s=%.1f fotogramas/s=%.1f esperas/s=%.1f vblanksJuego/s=%.1f",
                                  1e-5 * static_cast<double>(d.cpuHilo - s_antes.cpuHilo) / segundos,
                                  1e-7 * static_cast<double>(d.esperaGs - s_antes.esperaGs) / segundos,
                                  1e-7 * static_cast<double>(d.vu1Ns - s_antes.vu1Ns) / segundos,
                                  pares > 0.0 ? static_cast<double>(d.vu1Ns - s_antes.vu1Ns) / pares : 0.0,
                                  s_cadenas != 0u ? 8000.0 * tscHz * static_cast<double>(s_cadenas) /
                                                        static_cast<double>(s_cadenaSuma) / 1e9
                                                  : 0.0,
                                  s_cadenas != 0u ? 8000.0 * tscHz / static_cast<double>(s_cadenaMin) / 1e9 : 0.0,
                                  s_muestrasAparcados != 0u
                                      ? static_cast<double>(s_aparcados) / static_cast<double>(s_muestrasAparcados)
                                      : 0.0,
                                  static_cast<double>(d.despertares - s_antes.despertares) / segundos,
                                  1e-7 * static_cast<double>(d.esperaVu1 - s_antes.esperaVu1) / segundos,
                                  static_cast<double>(d.cambios - s_antes.cambios) / segundos,
                                  static_cast<double>(d.vblanks - s_antes.vblanks) / segundos,
                                  static_cast<double>(d.fines - s_antes.fines) / segundos,
                                  static_cast<double>(d.esperas - s_antes.esperas) / segundos,
                                  static_cast<double>(static_cast<uint32_t>(d.vblanksJuego - s_antes.vblanksJuego)) / segundos);
                    // Al final de la linea (no mueve lo que leen ab_resumen.py y f6-t6-analizar.py): millones de ciclos
                    // del hilo del EE por paquete (fotograma) de la ventana. En este PC QueryThreadCycleTime cuenta a
                    // ritmo fijo (~2,1 GHz, no los ciclos del nucleo): es un tiempo de CPU preciso, sin la frecuencia
                    // muestreada de ghz= (1.4 T2).
                    std::cout << texto;
                    std::snprintf(texto, sizeof(texto), " mciclosFot=%.2f",
                                  paquetes != 0u ? 1e-6 * static_cast<double>(d.ciclosHilo - s_antes.ciclosHilo) /
                                                       static_cast<double>(paquetes)
                                                 : 0.0);
                    std::cout << texto << std::endl;
                    s_antes = d;
                    s_cadenaMin = ~0ull;
                    s_cadenaSuma = s_cadenas = s_aparcados = s_muestrasAparcados = 0;
                }
                paquetes = 0;
                enNucleoE = 0;
                ultimo = ahora;
            }
        }
        g_lanzarDmaVif1Original(rdram, ctx, runtime);
    }

    // Descarte por visibilidad (func_11E280): func_2C9EA0 lanza con vcallmsr el microprograma de VU0 (caja contra los
    // 10 planos del frustum que func_2C9E70 sube con vsqi a la memoria de la VU0) y func_2C9EE0 lee el resultado de
    // vf20-vf31: -1 fuera, 1 cruza un plano (hay que recortar), 0 dentro del todo. Cada 10 s cuenta cada clase, para
    // comprobar el modo micro de la VU0 (parche 0019: antes vcallmsr saltaba a una direccion basura y vsqi escribia
    // en la RAM del EE, asi que el resultado era aleatorio).
    constexpr uint32_t kResultadoVisibilidad = 0x002C9EE0u;
    constexpr uint32_t kLanzarVisibilidad = 0x002C9EA0u;
    PS2Runtime::RecompiledFunction g_resultadoVisibilidadOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_lanzarVisibilidadOriginal = nullptr;

    // F1_VISIBILIDAD_DETALLE=<segundo>: desde ese segundo, las 6 primeras pruebas de visibilidad imprimen la entrada
    // (matriz de a0, centro a1 y extensiones a2, de 128 bits) y la salida del microprograma (vf5 centro transformado,
    // vf6 extensiones transformadas y la .y de vf20-vf31: d-r o d+r de cada plano) con los 10 planos.
    double g_visibilidadDetalleDesde = -1.0;
    uint32_t g_visibilidadDetalleVeces = 0;

    std::string vectorTexto(const float *v)
    {
        std::ostringstream s;
        s << "(" << v[0] << "," << v[1] << "," << v[2] << "," << v[3] << ")";
        return s.str();
    }

    bool visibilidadDetalleActivo()
    {
        return g_visibilidadDetalleDesde >= 0.0 && g_visibilidadDetalleVeces < 6u &&
               segundosDesdeArranque() >= g_visibilidadDetalleDesde;
    }

    void trazarLanzarVisibilidad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (visibilidadDetalleActivo())
        {
            float m[16];
            for (uint32_t i = 0; i < 16u; ++i)
            {
                const uint32_t bits = leer32(rdram, GPR_U32(ctx, 4) + i * 4u);
                std::memcpy(&m[i], &bits, 4);
            }
            alignas(16) float a1[4], a2[4];
            _mm_store_ps(a1, _mm_castsi128_ps(ctx->r[5]));
            _mm_store_ps(a2, _mm_castsi128_ps(ctx->r[6]));
            std::cout << "[f1-06:visdet] entrada m0=" << vectorTexto(m) << " m1=" << vectorTexto(m + 4) << " m2="
                      << vectorTexto(m + 8) << " m3=" << vectorTexto(m + 12) << " centro=" << vectorTexto(a1)
                      << " ext=" << vectorTexto(a2) << std::endl;
        }
        g_lanzarVisibilidadOriginal(rdram, ctx, runtime);
    }

    void trazarResultadoVisibilidad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_resultadoVisibilidadOriginal(rdram, ctx, runtime);
        if (visibilidadDetalleActivo())
        {
            ++g_visibilidadDetalleVeces;
            alignas(16) float v[32][4];
            for (uint32_t i = 0; i < 32u; ++i)
                _mm_store_ps(v[i], ctx->vu0_vf[i]);
            std::ostringstream s;
            s << "[f1-06:visdet] salida=" << static_cast<int32_t>(GPR_U32(ctx, 2)) << " vf5=" << vectorTexto(v[5])
              << " vf6=" << vectorTexto(v[6]) << " y31..20=";
            for (int i = 31; i >= 20; --i)
                s << v[i][1] << (i > 20 ? "," : "");
            if (const uint8_t *vu0 = runtime->memory().getVU0Data())
            {
                s << " planos=";
                for (uint32_t p = 0; p < 10u; ++p)
                {
                    float plano[4];
                    std::memcpy(plano, vu0 + p * 16u, sizeof(plano));
                    s << vectorTexto(plano);
                }
            }
            std::cout << s.str() << std::endl;
        }
        static uint64_t cuentas[3] = {};
        static double ultimo = 0.0;
        const int32_t resultado = static_cast<int32_t>(GPR_U32(ctx, 2));
        ++cuentas[resultado < 0 ? 0 : resultado == 0 ? 1 : 2];
        const double ahora = segundosDesdeArranque();
        if (ahora - ultimo >= 10.0)
        {
            float plano[4] = {};
            if (const uint8_t *vu0 = runtime->memory().getVU0Data())
                std::memcpy(plano, vu0, sizeof(plano));
            std::cout << "[f1-06:visibilidad] t=" << ahora << " fuera=" << cuentas[0] << " dentro=" << cuentas[1]
                      << " recortar=" << cuentas[2] << " cmsar0=0x" << std::hex << ctx->vu0_cmsar0 << std::dec
                      << " plano0=" << plano[0] << "," << plano[1] << "," << plano[2] << "," << plano[3] << std::endl;
            cuentas[0] = cuentas[1] = cuentas[2] = 0;
            ultimo = ahora;
        }
    }

    void trazarIrqVif1(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        if (veces++ < 20)
            std::cout << "[f1-06] IRQ VIF1 causa=" << GPR_U32(ctx, 4) << " contador="
                      << static_cast<int32_t>(leer32(rdram, kContadorVif1)) << std::endl;
        anotarVif1('I', GPR_U32(ctx, 4), runtime->memory().read32(0x10003C30u), leer32(rdram, kContadorVif1),
                   leer32(rdram, 0x00404F68u), leer32(rdram, 0x00404F6Cu), runtime->memory().read32(0x1000A000u),
                   runtime->memory().read32(0x1000A030u));
        g_irqVif1Original(rdram, ctx, runtime);
    }

    void trazarEsperaVif1(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        if (veces++ < 20)
            std::cout << "[f1-06] espera VIF1 contador=" << static_cast<int32_t>(leer32(rdram, kContadorVif1))
                      << std::endl;
        anotarVif1('W', leer32(rdram, kContadorVif1), leer32(rdram, 0x00404FF4u), GPR_U32(ctx, 31));
        g_esperaVif1Original(rdram, ctx, runtime);
    }

    // func_2D56D0(0x4B1A80): la espera del VIF1 (func_2D52F8) la llama si el contador no llega a 0 en 4 tics de
    // 0x404FF4; despues gira sin fin mientras el contador no sea 0.
    constexpr uint32_t kPlazoVif1Agotado = 0x002D56D0u;
    PS2Runtime::RecompiledFunction g_plazoVif1AgotadoOriginal = nullptr;

    void trazarPlazoVif1Agotado(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        if (veces++ < 3u)
        {
            PS2Memory &mem = runtime->memory();
            std::ostringstream out;
            out << "[f1-06:vif1] PLAZO AGOTADO t=" << segundosDesdeArranque() << " ra=0x" << std::hex << GPR_U32(ctx, 31)
                << " contador=0x" << leer32(rdram, kContadorVif1) << " [404F68]=" << leer32(rdram, 0x00404F68u)
                << " [404F6C]=" << leer32(rdram, 0x00404F6Cu) << " [404FF4]=" << leer32(rdram, 0x00404FF4u)
                << " [405000]=" << leer32(rdram, 0x00405000u) << "\n[f1-06:vif1]   VIF1 STAT=" << mem.read32(0x10003C00u)
                << " MARK=" << mem.read32(0x10003C30u) << " CODE=" << mem.read32(0x10003C80u)
                << " D1 CHCR=" << mem.read32(0x10009000u) << " MADR=" << mem.read32(0x10009010u)
                << " QWC=" << mem.read32(0x10009020u) << " TADR=" << mem.read32(0x10009030u)
                << " D2 CHCR=" << mem.read32(0x1000A000u) << " TADR=" << mem.read32(0x1000A030u)
                << " D_STAT=" << mem.read32(0x1000E010u) << "\n";
            const uint64_t primero = g_numSucesosVif1 > 128u ? g_numSucesosVif1 - 128u : 0u;
            for (uint64_t i = primero; i < g_numSucesosVif1; ++i)
            {
                const SucesoVif1 &s = g_sucesosVif1[i % 128u];
                out << "[f1-06:vif1]   t=" << std::dec << s.t << std::hex << " " << s.tipo;
                for (const uint32_t d : s.datos)
                    out << " " << d;
                out << "\n";
            }
            std::cerr << out.str() << std::flush;
            ps2DebugDumpVif1Irqs();
        }
        g_plazoVif1AgotadoOriginal(rdram, ctx, runtime);
    }

    // Bucle de la pantalla de carga (func_146A50): mientras (*0x3FBE24)->[0xA4] != 5 dibuja un fotograma y
    // llama a func_147080(objeto, porcentaje). Registra los cambios de estado del objeto.
    constexpr uint32_t kPunteroCarga = 0x003FBE24u;
    constexpr uint32_t kProgresoCarga = 0x00147080u;
    PS2Runtime::RecompiledFunction g_progresoCargaOriginal = nullptr;

    // Fase 5 (T11): cargas rapidas. La pantalla de carga (func_146BB0(tipo, segundos): Start_Loading de Lua, 0x149528,
    // la pide con 30 s; el arranque con otros) tiene su maquina de estados (func_148CE0, cada fotograma desde el bucle
    // func_146A50 del hilo 18): 1 fundido de entrada (~1 s), 2 animacion de los consejos (3 s, solo los tipos 5 y 6:
    // func_148C10), 3 esperar a que el porcentaje llegue a 100, 4 fundido de salida y 5 fin. El porcentaje es el tiempo
    // pasado entre los segundos pedidos, o 100 en cuanto el hilo que carga (el principal, prioridad 30) pone el suyo
    // (+0x3C del bucle) a 100. Medido (logs/explorar-t11-*): carga de la carrera 1,0 + 3,0 + 3,7 s; vuelta al menu
    // 1,0 + 6,1 s; las de arranque (Sony, aviso legal, Dolby) 1,0 + 4,0 s, con el porcentaje por tiempo. En el estado 3
    // el hilo del EE va ocioso un 50-70 % del tiempo: duerme hasta el siguiente VBLANK o va por delante del reloj real
    // (el ritmo del EE), y el IOP, que sigue al reloj del EE, espera en sus DelayThread y en el sceCdSync del 0114.
    // opciones.ini cargas_rapidas = 1: durante el estado 3 el reloj del juego no espera al real (ps2SetFastForward):
    // el EE, sus temporizadores y el IOP van tan rapido como da el PC. Los fundidos y la animacion (estados 1, 2 y 4)
    // van a su ritmo. Solo mientras el SPU2 lleva kMuestrasSilencio sin sonar: con el IOP adelantado, la salida de
    // sonido (al ritmo real) se desborda y descarta lo que sobra; asi solo se descarta silencio (la musica del menu
    // puede empezar antes de que acabe la carga). Se mira en cada fotograma (AJUSTES PC la cambia en
    // caliente); tras quitar la pantalla (func_146D78, que llama Stop_Loading y marca la carga como hecha), no vuelve
    // hasta la siguiente.
    constexpr uint32_t kMaquinaCarga = 0x00148CE0u;
    constexpr uint32_t kQuitarPantallaCarga = 0x00146D78u;
    constexpr uint32_t kEstadoCargaEsperando = 3u;
    PS2Runtime::RecompiledFunction g_maquinaCargaOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_quitarPantallaCargaOriginal = nullptr;
    constexpr uint64_t kMuestrasSilencio = 4800u; // 100 ms a 48 kHz
    bool g_avanceRapido = false;
    bool g_pantallaCargaQuitada = false;

    bool cargasRapidas()
    {
#if defined(PS2X_ENABLE_DEBUG_UI)
        float valor = 0.0f;
        if (PS2OptionsMenu::gameOptionNumber("cargas_rapidas", valor))
            return valor >= 0.5f;
#endif
        const char *valor_entorno = std::getenv("PS2X_CARGAS_RAPIDAS");
        return valor_entorno != nullptr && valor_entorno[0] == '1';
    }

    bool avanceRapidoEnCarga(uint32_t estado)
    {
        if (estado < kEstadoCargaEsperando)
            g_pantallaCargaQuitada = false;
        return estado == kEstadoCargaEsperando && !g_pantallaCargaQuitada && cargasRapidas() &&
               ps2x::iop::spu2QuietFrames() >= kMuestrasSilencio;
    }

    void fijarAvanceRapido(bool si, uint32_t estado)
    {
        if (si == g_avanceRapido)
            return;
        g_avanceRapido = si;
        ps2SetFastForward(si);
        std::cout << "[f1-06:carga] t=" << segundosDesdeArranque() << " avance rapido " << (si ? "si" : "no")
                  << " (estado " << estado << ")" << std::endl;
    }

    void maquinaCarga(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t objeto = GPR_U32(ctx, 4);
        g_maquinaCargaOriginal(rdram, ctx, runtime);
        const uint32_t estado = leer32(rdram, objeto + 0xA4u);
        fijarAvanceRapido(avanceRapidoEnCarga(estado), estado);
    }

    void quitarPantallaCarga(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_pantallaCargaQuitada = true;
        fijarAvanceRapido(false, 5u);
        g_quitarPantallaCargaOriginal(rdram, ctx, runtime);
    }

    void trazarProgresoCarga(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        static uint32_t ultimoEstado = 0xFFFFFFFFu;
        const uint32_t objeto = GPR_U32(ctx, 4);
        const uint32_t estado = leer32(rdram, objeto + 0xA4u);
        fijarAvanceRapido(avanceRapidoEnCarga(estado), estado);
        float porcentaje = 0.0f;
        std::memcpy(&porcentaje, &ctx->f[12], sizeof(porcentaje));
        if (estado != ultimoEstado || (veces % 300u) == 0u)
        {
            static const auto inicio = std::chrono::steady_clock::now();
            const double real = std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count();
            const uint32_t desbordes = leer32(rdram, 0x004A4928u);
            std::cout << "[f1-06] reloj real=" << real << "s desbordesT0=" << desbordes
                      << " juego~" << (desbordes * 65536.0 / 576000.0) << "s T0_COUNT=0x" << std::hex
                      << runtime->memory().read32(0x10000000u) << " T0_MODE=0x" << runtime->memory().read32(0x10000010u)
                      << std::dec << std::endl;
            std::cout << "[f1-06] carga vuelta=" << veces << " objeto=0x" << std::hex << objeto
                      << " estado=0x" << estado << std::dec << " porcentaje=" << porcentaje << " campos=";
            for (uint32_t off = 0x80u; off < 0xC0u; off += 4u)
                std::cout << std::hex << leer32(rdram, objeto + off) << (off + 4u < 0xC0u ? "," : "");
            std::cout << std::dec << std::endl;
            ultimoEstado = estado;
        }
        ++veces;
        g_progresoCargaOriginal(rdram, ctx, runtime);
    }

    // Reloj del juego: func_25F478 registra el manejador 0x25F300 en la INTC 9 (temporizador 0, desborde),
    // pone T0_MODE = 0x282 y el manejador suma 1 a 0x4A4928. func_25F330 da los milisegundos.
    constexpr uint32_t kIniciarReloj = 0x0025F478u;
    constexpr uint32_t kIrqReloj = 0x0025F300u;
    PS2Runtime::RecompiledFunction g_iniciarRelojOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_irqRelojOriginal = nullptr;

    void trazarIniciarReloj(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cout << "[f1-06] iniciar reloj a0=" << GPR_U32(ctx, 4) << " a1=0x" << std::hex << GPR_U32(ctx, 5)
                  << std::dec << std::endl;
        g_iniciarRelojOriginal(rdram, ctx, runtime);
        std::cout << "[f1-06] reloj iniciado v0=0x" << std::hex << GPR_U32(ctx, 2) << std::dec << std::endl;
    }

    void trazarIrqReloj(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        if (veces++ < 5)
            std::cout << "[f1-06] IRQ reloj T0 causa=" << GPR_U32(ctx, 4) << std::endl;
        g_irqRelojOriginal(rdram, ctx, runtime);
    }

    // ---- Volcado del GS (F1_VOLCAR_GS=<segundos>) ----
    // logs/gs_historial.txt: los ultimos 512 eventos del GS (dibujos con sus registros, transferencias, registros).
    // logs/gs_fbp0.png y logs/gs_fbp128.png: los dos bufferes de imagen del juego (paginas 0 y 0x80, 512 de ancho,
    // CT32). ReadVram recibe la base en bloques de 256 bytes (una pagina son 32 bloques) y el ancho en unidades de 64.
    void volcarBufferGs(PS2Runtime &runtime, uint32_t fbp, uint32_t fbw, const char *ruta)
    {
        const int ancho = static_cast<int>(fbw * 64u);
        const int alto = 448;
        std::vector<uint8_t> pixeles(static_cast<size_t>(ancho) * alto * 4u);
        for (int y = 0; y < alto; ++y)
            for (int x = 0; x < ancho; ++x)
            {
                const uint32_t c = runtime.gs().ReadVram(0u, fbp * 32u, fbw, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                uint8_t *p = &pixeles[(static_cast<size_t>(y) * ancho + x) * 4u];
                p[0] = static_cast<uint8_t>(c);
                p[1] = static_cast<uint8_t>(c >> 8);
                p[2] = static_cast<uint8_t>(c >> 16);
                p[3] = 0xFF;
            }
        Image imagen{pixeles.data(), ancho, alto, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ExportImage(imagen, ruta);
    }

    // F1_VOLCAR_TEX="tbp:tbw:psm:ancho:alto:cbp[;...]" (con F1_VOLCAR_GS): cada textura indexada (psm 19 = T8, 20 = T4)
    // con su CLUT CT32 en CSM1, en logs/gs_tex<tbp>-<s>.png (color, alfa a 255), -alfa.png (alfa x2 en gris) e
    // -indices.png (el indice en gris; T4 x17). Solo diagnostico: lee la VRAM sin parar el GS.
    // Con "dibujo:tbw:psm:ancho:alto", tbp y cbp salen del ultimo dibujo del historial del GS con esa textura (las
    // direcciones cambian de una partida a otra; scripts\hacer-icono.ps1 lo usa para el logo de los menus).
    void volcarTexturaGs(PS2Runtime &runtime, const std::string &spec, const std::string &sufijo)
    {
        unsigned tbp = 0, tbw = 0, psm = 0, ancho = 0, alto = 0, cbp = 0;
        bool leida = false;
        if (spec.rfind("dibujo:", 0) == 0)
        {
            if (std::sscanf(spec.c_str() + 7, "%u:%u:%u:%u", &tbw, &psm, &ancho, &alto) == 4)
            {
                for (const GSDebugHistoryEntry &e : runtime.gs().getDebugHistory())
                    if (e.kind == GSDebugEventKind::Draw && e.prim.tme && e.tex0.tbw == tbw && e.tex0.psm == psm &&
                        (1u << e.tex0.tw) == ancho && (1u << e.tex0.th) == alto)
                    {
                        tbp = e.tex0.tbp0;
                        cbp = e.tex0.cbp;
                        leida = true;
                    }
                if (!leida)
                {
                    std::cerr << "[f1-06] F1_VOLCAR_TEX: ningun dibujo con \"" << spec << "\" en el historial" << std::endl;
                    return;
                }
            }
        }
        else
        {
            leida = std::sscanf(spec.c_str(), "%u:%u:%u:%u:%u:%u", &tbp, &tbw, &psm, &ancho, &alto, &cbp) == 6;
        }
        if (!leida ||
            (psm != 19u && psm != 20u) || ancho == 0u || alto == 0u || ancho > 1024u || alto > 1024u)
        {
            std::cerr << "[f1-06] F1_VOLCAR_TEX: no entiendo \"" << spec << "\"" << std::endl;
            return;
        }
        uint32_t clut[256] = {};
        const unsigned entradas = psm == 19u ? 256u : 16u;
        for (unsigned i = 0; i < entradas; ++i)
        {
            // CSM1, CT32: T8 en 16x16 con los bits 3 y 4 del indice cambiados; T4 en 8x2.
            unsigned x, y;
            if (psm == 19u)
            {
                const unsigned j = (i & 0xE7u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1);
                x = j & 15u;
                y = j >> 4;
            }
            else
            {
                x = i & 7u;
                y = i >> 3;
            }
            clut[i] = runtime.gs().ReadVram(0u, cbp, 1u, x, y);
        }
        std::vector<uint8_t> color(static_cast<size_t>(ancho) * alto * 4u), alfa(color.size()), indices(color.size());
        for (unsigned y = 0; y < alto; ++y)
            for (unsigned x = 0; x < ancho; ++x)
            {
                const uint32_t indice = runtime.gs().ReadVram(psm, tbp, tbw, x, y) & (entradas - 1u);
                const uint32_t c = clut[indice];
                const size_t o = (static_cast<size_t>(y) * ancho + x) * 4u;
                color[o] = static_cast<uint8_t>(c);
                color[o + 1] = static_cast<uint8_t>(c >> 8);
                color[o + 2] = static_cast<uint8_t>(c >> 16);
                color[o + 3] = 0xFF;
                const uint8_t a = static_cast<uint8_t>(std::min<uint32_t>(255u, (c >> 24) * 2u));
                const uint8_t g = static_cast<uint8_t>(psm == 19u ? indice : indice * 17u);
                alfa[o] = alfa[o + 1] = alfa[o + 2] = a;
                indices[o] = indices[o + 1] = indices[o + 2] = g;
                alfa[o + 3] = indices[o + 3] = 0xFF;
            }
        const std::string base = "logs/gs_tex" + std::to_string(tbp) + sufijo;
        Image imagen{color.data(), static_cast<int>(ancho), static_cast<int>(alto), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ExportImage(imagen, (base + ".png").c_str());
        imagen.data = alfa.data();
        ExportImage(imagen, (base + "-alfa.png").c_str());
        imagen.data = indices.data();
        ExportImage(imagen, (base + "-indices.png").c_str());
        std::cerr << "[f1-06] textura " << tbp << " volcada en " << base << ".png" << std::endl;
        std::cerr << "[f1-06] clut " << cbp << ":";
        for (unsigned i = 0; i < entradas; ++i)
            std::cerr << " " << std::hex << clut[i] << std::dec;
        std::cerr << std::endl;
    }

    void volcarHistorialGs(PS2Runtime &runtime, const char *ruta)
    {
        static const char *kTipos[] = {"giftag", "reg", "DIBUJO", "transfer", "present"};
        static const char *kPrims[] = {"punto", "linea", "tiralineas", "tri", "tiratri", "abanico", "sprite", "?"};
        std::ofstream out(ruta);
        for (const GSDebugHistoryEntry &e : runtime.gs().getDebugHistory())
        {
            const int tipo = std::min<int>(static_cast<int>(e.kind), 4);
            out << e.seq << " f=" << e.frameIndex << " vsync=" << e.vsyncTick << " " << kTipos[tipo];
            switch (e.kind)
            {
            case GSDebugEventKind::Draw:
                out << " " << kPrims[e.prim.type & 7] << " tme=" << e.prim.tme << " abe=" << e.prim.abe
                    << " fst=" << e.prim.fst << " ctxt=" << e.prim.ctxt << " fbp=" << e.frame.fbp << " fbw=" << e.frame.fbw
                    << " psm=" << int(e.frame.psm) << " fbmsk=0x" << std::hex << e.frame.fbmsk << std::dec
                    << " zbp=" << e.zbuf.zbp << " zmask=" << e.zbuf.zmask << " tbp=" << e.tex0.tbp0
                    << " tbw=" << int(e.tex0.tbw) << " tpsm=" << int(e.tex0.psm) << " tw=" << int(e.tex0.tw)
                    << " th=" << int(e.tex0.th) << " tfx=" << int(e.tex0.tfx) << " tcc=" << int(e.tex0.tcc)
                    << " test=0x" << std::hex << e.test << " alpha=0x" << e.alpha << std::dec << " v=" << e.vertexCount
                    << " x=[" << e.xMin << "," << e.xMax << "] y=[" << e.yMin << "," << e.yMax << "] z=[" << e.zMin
                    << "," << e.zMax << "] a=[" << int(e.aMin) << "," << int(e.aMax) << "]";
                break;
            case GSDebugEventKind::Transfer:
                out << " dbp=" << e.bitbltbuf.dbp << " dbw=" << int(e.bitbltbuf.dbw) << " dpsm=" << int(e.bitbltbuf.dpsm)
                    << " pos=(" << e.trxpos.dsax << "," << e.trxpos.dsay << ") tam=" << e.trxreg.rrw << "x"
                    << e.trxreg.rrh << " dir=" << e.trxdir << " pix=" << e.transferPixels;
                break;
            case GSDebugEventKind::Register:
                out << " reg=0x" << std::hex << int(e.reg) << " val=0x" << e.regValue << std::dec;
                break;
            case GSDebugEventKind::GifTag:
                out << " nloop=" << e.gifNloop << " flg=" << int(e.gifFlg) << " nreg=" << int(e.gifNreg)
                    << " bytes=" << e.gifSizeBytes;
                break;
            default:
                out << " displayFbp=" << e.displayFbp << " sourceFbp=" << e.sourceFbp;
                break;
            }
            out << "\n";
        }
    }

    // Volcado periodico de los hilos del EE (F1_HILOS=<segundos>): estado y motivo de espera de cada uno.
    void arrancarVolcadoHilos(PS2Runtime &runtime)
    {
        if (const char *volcarGs = std::getenv("F1_VOLCAR_GS"))
        {
            // Lista de segundos separados por comas; cada volcado lleva el segundo en el nombre (gs_historial-130.txt).
            std::vector<int> segundosGs;
            for (const char *p = volcarGs; *p;)
            {
                segundosGs.push_back(std::max(1, std::atoi(p)));
                while (*p && *p != ',')
                    ++p;
                if (*p == ',')
                    ++p;
            }
            std::sort(segundosGs.begin(), segundosGs.end());
            std::thread([&runtime, segundosGs]()
                        {
                for (const int s : segundosGs)
                {
                    while (segundosDesdeArranque() < s)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    const std::string sufijo = "-" + std::to_string(s);
                    runtime.gs().setDebugHistoryPaused(true);
                    volcarHistorialGs(runtime, ("logs/gs_historial" + sufijo + ".txt").c_str());
                    runtime.gs().setDebugHistoryPaused(false);
                    volcarBufferGs(runtime, 0u, 8u, ("logs/gs_fbp0" + sufijo + ".png").c_str());
                    volcarBufferGs(runtime, 0x80u, 8u, ("logs/gs_fbp128" + sufijo + ".png").c_str());
                    // F1_VOLCAR_CRUDO="bp:bw:ancho:alto": esa zona leida como CT32, fila a fila, en
                    // logs/gs_crudo<bp>-<s>.bin (los bytes en el orden en que llegan en una subida CT32, para buscarlos
                    // en la RAM del EE).
                    if (const char *crudo = std::getenv("F1_VOLCAR_CRUDO"))
                    {
                        unsigned bp = 0, bw = 0, ancho = 0, alto = 0;
                        if (std::sscanf(crudo, "%u:%u:%u:%u", &bp, &bw, &ancho, &alto) == 4 && ancho <= 1024u && alto <= 1024u)
                        {
                            std::vector<uint32_t> palabras(static_cast<size_t>(ancho) * alto);
                            for (unsigned y = 0; y < alto; ++y)
                                for (unsigned x = 0; x < ancho; ++x)
                                    palabras[static_cast<size_t>(y) * ancho + x] = runtime.gs().ReadVram(0u, bp, bw, x, y);
                            const std::string ruta = "logs/gs_crudo" + std::to_string(bp) + sufijo + ".bin";
                            std::ofstream(ruta, std::ios::binary)
                                .write(reinterpret_cast<const char *>(palabras.data()),
                                       static_cast<std::streamsize>(palabras.size() * 4u));
                            std::cerr << "[f1-06] crudo " << bp << " en " << ruta << std::endl;
                        }
                    }
                    if (const char *texturas = std::getenv("F1_VOLCAR_TEX"))
                    {
                        std::stringstream flujo(texturas);
                        for (std::string spec; std::getline(flujo, spec, ';');)
                            volcarTexturaGs(runtime, spec, sufijo);
                    }
                    std::cerr << "[f1-06] GS volcado (" << s << " s) en logs/gs_historial" << sufijo << ".txt y gs_fbp*" << sufijo
                              << ".png" << std::endl;
                } })
                .detach();
        }
        // F1_VOLCAR_RAM=<segundos>: guarda la RAM del EE en logs/ee_ram.bin pasado ese tiempo. Con varios separados por
        // comas (F1_VOLCAR_RAM=200,230,260), cada uno en logs/ee_ram-<segundos>.bin.
        if (const char *volcar = std::getenv("F1_VOLCAR_RAM"))
        {
            std::vector<int> momentos;
            std::stringstream flujo(volcar);
            for (std::string parte; std::getline(flujo, parte, ',');)
                momentos.push_back(std::max(1, std::atoi(parte.c_str())));
            std::sort(momentos.begin(), momentos.end());
            const bool varios = momentos.size() > 1u;
            std::thread([&runtime, momentos, varios]()
                        {
                for (const int momento : momentos)
                {
                    const auto espera = std::chrono::duration<double>(momento - segundosDesdeArranque());
                    if (espera.count() > 0.0)
                        std::this_thread::sleep_for(espera);
                    const std::string ruta = varios ? "logs/ee_ram-" + std::to_string(momento) + ".bin" : "logs/ee_ram.bin";
                    if (FILE *f = std::fopen(ruta.c_str(), "wb"))
                    {
                        std::fwrite(runtime.memory().getRDRAM(), 1, PS2_GUEST_RAM_SIZE, f); // los 32 MB del juego (parche 0180)
                        std::fclose(f);
                        std::cerr << "[f1-06] RAM del EE volcada en " << ruta << " (t=" << segundosDesdeArranque() << ")"
                                  << std::endl;
                    }
                } })
                .detach();
        }
        const char *valor = std::getenv("F1_HILOS");
        if (!valor)
            return;
        const int segundos = std::max(1, std::atoi(valor));
        std::thread([&runtime, segundos]()
                    {
            static const char *kEstados[] = {"Running", "Ready", "Waiting", "WaitSusp", "Suspended", "Dormant"};
            static const char *kMotivos[] = {"-", "Sleep", "Sema", "EventFlag", "VSync", "External", "Mpeg"};
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(segundos));
                const EeKernelSnapshot foto = runtime.eeScheduler().snapshot();
                std::cerr << "[f1-06:hilos] ciclo=" << foto.eeCycle << " actual=" << foto.runningThreadId << std::endl;
                const uint8_t *ram = runtime.memory().getRDRAM();
                for (const EeThreadSnapshot &h : foto.threads)
                {
                    std::cerr << "[f1-06:hilos]   id=" << h.id << " pri=" << h.currentPriority << " "
                              << kEstados[static_cast<int>(h.status)] << " espera=" << kMotivos[static_cast<int>(h.waitReason)]
                              << ":" << h.waitId << " pc=0x" << std::hex << h.pc << " ra=0x" << h.ra << " entrada=0x"
                              << h.entry;
                    // Fase 3 T7: direcciones de retorno en la pila del hilo (palabras de .text con un jal/jalr dos
                    // instrucciones antes), de la mas reciente a la mas antigua: la cadena de llamadas de un hilo que espera.
                    if (h.status == EeThreadStatus::Waiting && h.sp >= h.stack && h.sp < h.stack + h.stackSize &&
                        h.stack + h.stackSize <= PS2_GUEST_RAM_SIZE)
                    {
                        std::cerr << " pila:";
                        int vistas = 0;
                        for (uint32_t a = h.sp & ~3u; a + 4u <= h.stack + h.stackSize && vistas < 14; a += 4u)
                        {
                            const uint32_t v = leer32(ram, a);
                            if (v < 0x100008u || v >= 0x422E98u || (v & 3u) != 0u)
                                continue;
                            const uint32_t llamada = leer32(ram, v - 8u);
                            const bool jal = (llamada >> 26) == 3u;
                            const bool jalr = (llamada >> 26) == 0u && (llamada & 0x3Fu) == 9u;
                            if (jal || jalr)
                            {
                                std::cerr << " " << v;
                                ++vistas;
                            }
                        }
                    }
                    std::cerr << std::dec << std::endl;
                }
                static bool volcada = false;
                for (const EeThreadSnapshot &h : foto.threads)
                    if (!volcada && h.id == 1 && h.status == EeThreadStatus::Dormant)
                    {
                        volcada = true;
                        if (FILE *f = std::fopen("logs/ee_ram.bin", "wb"))
                        {
                            std::fwrite(runtime.memory().getRDRAM(), 1, PS2_GUEST_RAM_SIZE, f);
                            std::fclose(f);
                            std::cerr << "[f1-06:hilos] RAM del EE volcada en logs/ee_ram.bin" << std::endl;
                        }
                    }
                for (const EeSemaphoreSnapshot &sem : foto.semaphores)
                    if (sem.waiters != 0u)
                        std::cerr << "[f1-06:hilos]   sema id=" << sem.id << " cuenta=" << sem.count << "/" << sem.maxCount
                                  << " esperando=" << sem.waiters << std::endl;
                for (const EeEventFlagSnapshot &ev : foto.eventFlags)
                    if (ev.waiters != 0u)
                        std::cerr << "[f1-06:hilos]   evento id=" << ev.id << " bits=0x" << std::hex << ev.bits << std::dec
                                  << " esperando=" << ev.waiters << std::endl;
            } })
            .detach();
    }

    // Chivatos de libpad (HLE del runtime): argumentos y resultado de las primeras llamadas de cada funcion.
#define F1_CHIVATO_PAD(nombre, direccion)                                                                          PS2Runtime::RecompiledFunction g_##nombre##Original = nullptr;                                                 void trazar_##nombre(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)                                   {                                                                                                                  static uint32_t veces = 0;                                                                                     const uint32_t a0 = GPR_U32(ctx, 4), a1 = GPR_U32(ctx, 5), a2 = GPR_U32(ctx, 6), a3 = GPR_U32(ctx, 7);         const uint32_t ra = GPR_U32(ctx, 31);                                                                          g_##nombre##Original(rdram, ctx, runtime);                                                                     if (veces++ < 12u)                                                                                                 std::cout << "[f1-06:pad] " #nombre "(" << std::hex << a0 << "," << a1 << "," << a2 << "," << a3                         << ") = " << GPR_U32(ctx, 2) << " ra=" << ra << std::dec << std::endl;                       }
    F1_CHIVATO_PAD(scePadPortOpen, 0x002E8DE8u)
    F1_CHIVATO_PAD(scePadGetState, 0x002E9258u)
    F1_CHIVATO_PAD(scePadInfoMode, 0x002E9510u)
    F1_CHIVATO_PAD(scePadSetMainMode, 0x002E9648u)
    F1_CHIVATO_PAD(scePadInfoAct, 0x002E93F0u)
    F1_CHIVATO_PAD(scePadGetReqState, 0x002E9390u)
    F1_CHIVATO_PAD(scePadInit, 0x002E8990u)
    F1_CHIVATO_PAD(scePadGetModVersion, 0x002E9A40u)
#undef F1_CHIVATO_PAD

    // Pulsaciones programadas para pruebas sin teclado: F1_PULSAR="105:cruz,115:start:0.5" (segundo desde el arranque,
    // boton y, opcional, cuantos segundos se mantiene; por defecto 0,2). Envuelve scePadRead (0x2E91E0) y marca el boton
    // en el bufer de 32 bytes que lee el juego. El menu repite mientras se mantiene: 0,5 s de "abajo" baja unas 3 lineas.
    // Botones activos a nivel bajo: byte 2 = Select,L3,R3,Start,Arriba,Derecha,Abajo,Izquierda (bits 0-7);
    // byte 3 = L2,R2,L1,R1,Triangulo,Circulo,Cruz,Cuadrado (bits 0-7).
    // El juego llama a scePadRead desde func_275360, metodo virtual (vptr+0x24, tabla 0x41D768) del objeto de mando
    // que crea func_274C78 (maquina de estados de cada puerto) cuando el mando esta en modo analogico (id 7).
    // Cada pulsacion se aplica al menos en una lectura aunque su tiempo ya haya pasado: con el avatar 3D el juego va
    // a unos 4 fotogramas/s y lee el mando una vez por fotograma (cada ~0,25 s).
    // Ademas del bit digital se pone la presion del boton (bytes 8-19 del modo DualShock 2: derecha, izquierda, arriba,
    // abajo, triangulo, circulo, cruz, cuadrado, L1, L2, R1, R2), que el HLE ya calculo con el boton suelto: el
    // acelerador y el freno de un juego de coches pueden leer la presion. "stick-izq"/"stick-der"/"stick-arriba"/
    // "stick-abajo" llevan el stick izquierdo al tope (byte 6 = X, byte 7 = Y; 0x80 es el centro).
    struct PulsacionProgramada
    {
        double segundo;
        double duracion;
        uint32_t byte;
        uint8_t bit;
        uint32_t lecturas = 0;
        int32_t bytePresion = -1;
        int32_t byteStick = -1;
        uint8_t valorStick = 0x80u;
    };
    std::vector<PulsacionProgramada> g_pulsaciones;
    PS2Runtime::RecompiledFunction g_scePadReadOriginal = nullptr;
    constexpr double kDuracionPulsacion = 0.2;

    struct BotonPad
    {
        const char *nombre;
        uint32_t byte;
        uint8_t bit;
        int32_t bytePresion;
        int32_t byteStick;
        uint8_t valorStick;
    };
    const BotonPad kBotones[] = {{"select", 2, 0, -1, -1, 0x80}, {"l3", 2, 1, -1, -1, 0x80}, {"r3", 2, 2, -1, -1, 0x80},
                                 {"start", 2, 3, -1, -1, 0x80}, {"arriba", 2, 4, 10, -1, 0x80},
                                 {"derecha", 2, 5, 8, -1, 0x80}, {"abajo", 2, 6, 11, -1, 0x80}, {"izquierda", 2, 7, 9, -1, 0x80},
                                 {"l2", 3, 0, 17, -1, 0x80}, {"r2", 3, 1, 19, -1, 0x80}, {"l1", 3, 2, 16, -1, 0x80},
                                 {"r1", 3, 3, 18, -1, 0x80}, {"triangulo", 3, 4, 12, -1, 0x80}, {"circulo", 3, 5, 13, -1, 0x80},
                                 {"cruz", 3, 6, 14, -1, 0x80}, {"cuadrado", 3, 7, 15, -1, 0x80},
                                 // Stick izquierdo: sin bit digital (bit 8 = ninguno).
                                 {"stick-izq", 2, 8, -1, 6, 0x00}, {"stick-der", 2, 8, -1, 6, 0xFF},
                                 {"stick-arriba", 2, 8, -1, 7, 0x00}, {"stick-abajo", 2, 8, -1, 7, 0xFF}};

    const BotonPad *buscarBoton(const std::string &nombre)
    {
        for (const auto &b : kBotones)
            if (nombre == b.nombre)
                return &b;
        return nullptr;
    }

    // Guion de pulsaciones ligado a las pantallas del juego (F1_GUION): "evento[#n]+retraso:boton[:duracion],...". Cada
    // entrada se dispara la n-esima vez (por defecto la primera) que el juego emite graph.outcome("evento") y pulsa el
    // boton "retraso" segundos despues. Los tiempos del arranque varian unos 12 s entre ejecuciones y, con el juego mas rapido, las
    // pulsaciones a segundos fijos de F1_PULSAR caian en otras pantallas. Ejemplo para una carrera rapida:
    // F1_GUION='NoInGameAtBoot+4:cruz:0.5,SinglePlayer+4:cruz:0.5,InstantRace+6:cruz:0.5,game+30:cruz:0.5,game+60:cruz:300'
    // (el titulo "PRESS START" y la carga del perfil siguen yendo con F1_PULSAR: 35:cruz,118:start,130:cruz,...).
    struct EntradaGuion
    {
        std::string evento;
        double retraso;
        const BotonPad *boton;
        double duracion;
        uint32_t ocurrencia = 1;
        bool disparada = false;
    };
    std::vector<EntradaGuion> g_guion;
    std::map<std::string, uint32_t> g_ocurrenciasGuion;

    void cargarGuion()
    {
        const char *valor = std::getenv("F1_GUION");
        if (!valor)
            return;
        std::stringstream flujo(valor);
        std::string elemento;
        while (std::getline(flujo, elemento, ','))
        {
            const size_t mas = elemento.find('+');
            const size_t dosPuntos = elemento.find(':', mas == std::string::npos ? 0 : mas);
            if (mas == std::string::npos || dosPuntos == std::string::npos)
                continue;
            std::string boton = elemento.substr(dosPuntos + 1);
            double duracion = kDuracionPulsacion;
            if (const size_t otros = boton.find(':'); otros != std::string::npos)
            {
                duracion = std::atof(boton.substr(otros + 1).c_str());
                boton.resize(otros);
            }
            std::string evento = elemento.substr(0, mas);
            uint32_t ocurrencia = 1;
            if (const size_t almohadilla = evento.find('#'); almohadilla != std::string::npos)
            {
                ocurrencia = static_cast<uint32_t>(std::max(1, std::atoi(evento.c_str() + almohadilla + 1)));
                evento.resize(almohadilla);
            }
            if (const BotonPad *b = buscarBoton(boton))
                g_guion.push_back({evento, std::atof(elemento.substr(mas + 1, dosPuntos - mas - 1).c_str()), b, duracion,
                                   ocurrencia});
        }
        std::cout << "[f1-06:guion] " << g_guion.size() << " entradas" << std::endl;
    }

    void dispararGuion(const std::string &evento)
    {
        const double ahora = segundosDesdeArranque();
        const uint32_t vez = ++g_ocurrenciasGuion[evento];
        for (EntradaGuion &e : g_guion)
        {
            if (e.disparada || e.evento != evento || e.ocurrencia != vez)
                continue;
            e.disparada = true;
            g_pulsaciones.push_back({ahora + e.retraso, e.duracion, e.boton->byte, e.boton->bit, 0u, e.boton->bytePresion,
                                     e.boton->byteStick, e.boton->valorStick});
            std::cout << "[f1-06:guion] t=" << ahora << " " << evento << " -> " << e.boton->nombre << " en "
                      << ahora + e.retraso << " s durante " << e.duracion << " s" << std::endl;
        }
    }

    void cargarPulsaciones()
    {
        cargarGuion();
        const char *valor = std::getenv("F1_PULSAR");
        if (!valor)
            return;
        std::string texto(valor);
        size_t inicio = 0;
        while (inicio < texto.size())
        {
            size_t fin = texto.find(',', inicio);
            if (fin == std::string::npos)
                fin = texto.size();
            const std::string elemento = texto.substr(inicio, fin - inicio);
            const size_t dosPuntos = elemento.find(':');
            if (dosPuntos != std::string::npos)
            {
                const double segundo = std::atof(elemento.substr(0, dosPuntos).c_str());
                std::string boton = elemento.substr(dosPuntos + 1);
                double duracion = kDuracionPulsacion;
                const size_t otrosDosPuntos = boton.find(':');
                if (otrosDosPuntos != std::string::npos)
                {
                    duracion = std::atof(boton.substr(otrosDosPuntos + 1).c_str());
                    boton.resize(otrosDosPuntos);
                }
                for (const auto &b : kBotones)
                    if (boton == b.nombre)
                        g_pulsaciones.push_back({segundo, duracion, b.byte, b.bit, 0u, b.bytePresion, b.byteStick,
                                                 b.valorStick});
            }
            inicio = fin + 1;
        }
        std::cout << "[f1-06:pulsar] " << g_pulsaciones.size() << " pulsaciones programadas" << std::endl;
    }

    // ---- Piloto automatico (F1_PILOTO=1) ----
    // Los coches de la carrera son 22 objetos de 0x4F0 bytes seguidos (en la partida de prueba la posicion del primero
    // estaba en 0x7508A0 y la del jugador, el ultimo de la parrilla, en 0x757050). Desde la posicion de cada uno:
    // +0x00 posicion (x, y, z, 1) con y hacia arriba, +0x10 otra posicion, +0x20 velocidad en m/s (x, y, z, 1), +0x30
    // otra velocidad, +0x40 una distancia que baja lo que avanza el coche, y -0xD0 el vector unitario hacia delante.
    // La tabla se busca en la RAM (cuatro w = 1 seguidos y coordenadas de pista en los 22 objetos). El piloto sigue la
    // estela del coche guia (F1_PILOTO_GUIA, por defecto el anterior al jugador): guarda sus posiciones cada 2 m con su
    // velocidad, gira hacia el punto de la estela que esta ~10-45 m por delante (persecucion pura) y acelera o frena
    // para ir al F1_PILOTO_RITMO (0,92) de la velocidad minima que llevo el guia en los ~2,8 s siguientes. No toca
    // nada hasta que el guia arranca (la salida). En el plano (x, z), un giro a la derecha va de +z a +x (Copse: de
    // (-829, 204) a (-615, 349)). [f1-06:piloto] cada 2 s.
    // Cuando el guia vuelve a pasar por el principio de su estela (una vuelta), la estela se cierra: el piloto la
    // recorre en bucle y ya no depende del guia, que solo actualiza las velocidades de los puntos por los que pasa
    // (a menos de 4 m; asi una entrada en boxes no cuenta). Si el guia se para en la primera vuelta (se retira), sigue
    // la estela el coche en marcha mas cercano a su final. Si el coche se queda parado (choque, trompo), da marcha
    // atras 2,5 s girando al reves y vuelve a la estela.
    struct PuntoEstela
    {
        float x, z, v;
    };
    struct EstadoPiloto
    {
        bool activo = false;
        uint32_t tabla = 0u; // posicion del coche 0
        uint32_t guia = 0u;  // indice del coche guia
        double ultimaBusqueda = 0.0;
        double ultimoInforme = 0.0;
        bool salida = false;
        bool cerrada = false; // la estela es una vuelta entera y se recorre en bucle
        std::vector<PuntoEstela> estela;
        size_t cercano = 0u;
        size_t cercanoGuia = 0u;
        double guiaParadoDesde = -1.0;
        double paradoDesde = -1.0;
        double recuperarHasta = -1.0;
    };
    EstadoPiloto g_piloto;
    constexpr uint32_t kCochePaso = 0x4F0u;
    constexpr uint32_t kCoches = 22u;

    bool pareceCoche(const uint8_t *rdram, uint32_t a)
    {
        for (uint32_t w = 0x0Cu; w <= 0x3Cu; w += 0x10u)
            if (leerFloat(rdram, a + w) != 1.0f)
                return false;
        const float x = leerFloat(rdram, a), y = leerFloat(rdram, a + 4u), z = leerFloat(rdram, a + 8u);
        if (!std::isfinite(x) || !std::isfinite(z) || std::max(std::fabs(x), std::fabs(z)) < 20.0f ||
            std::max(std::fabs(x), std::fabs(z)) > 20000.0f || std::fabs(y) > 500.0f)
            return false;
        return std::hypot(leerFloat(rdram, a + 0x10u) - x, leerFloat(rdram, a + 0x18u) - z) < 10.0f;
    }

    uint32_t buscarTablaCoches(const uint8_t *rdram)
    {
        for (uint32_t a = 0x100000u; a + kCoches * kCochePaso < PS2_GUEST_RAM_SIZE; a += 16u)
        {
            if (!pareceCoche(rdram, a))
                continue;
            uint32_t k = 1u;
            while (k < kCoches && pareceCoche(rdram, a + k * kCochePaso))
                ++k;
            if (k == kCoches)
                return a;
        }
        return 0u;
    }

    void pilotar(uint8_t *rdram, uint32_t datos, double ahora)
    {
        static const bool s_activo = std::getenv("F1_PILOTO") != nullptr && std::atoi(std::getenv("F1_PILOTO")) != 0;
        static const uint32_t s_jugador = []
        {
            const char *valor = std::getenv("F1_PILOTO_COCHE");
            return valor != nullptr ? static_cast<uint32_t>(std::atoi(valor)) : kCoches - 1u;
        }();
        static const uint32_t s_guia = []
        {
            const char *valor = std::getenv("F1_PILOTO_GUIA");
            return valor != nullptr ? static_cast<uint32_t>(std::atoi(valor)) : (kCoches - 1u + kCoches - 1u) % kCoches;
        }();
        static const float s_ritmo = []
        {
            const char *valor = std::getenv("F1_PILOTO_RITMO");
            return valor != nullptr ? static_cast<float>(std::atof(valor)) : 0.92f;
        }();
        if (!s_activo || g_ocurrenciasGuion.find("game") == g_ocurrenciasGuion.end())
            return;
        // Fase 5 (T14): con el menu de pausa abierto (*0x3FE3EC + 0x568) o en la repeticion (+ 0x24) no toca el mando:
        // su cruz elegiria CONTINUE en el menu. Aspecto T1: en el modo foto tampoco (el mando del jugador queda en reposo
        // y la carrera esta congelada: el volante giraria con el piloto).
        if (ps2x::photoModeActive())
            return;
        if (const uint32_t juego = leer32(rdram, 0x003FE3ECu);
            juego >= 0x00100000u && juego < 0x02000000u &&
            (leer32(rdram, juego + 0x568u) != 0u || leer32(rdram, juego + 0x24u) != 0u))
            return;
        EstadoPiloto &e = g_piloto;
        if (e.tabla == 0u)
        {
            if (ahora - e.ultimaBusqueda < 1.0)
                return;
            e.ultimaBusqueda = ahora;
            e.tabla = buscarTablaCoches(rdram);
            if (e.tabla == 0u)
                return;
            if (!e.salida)
                e.guia = s_guia;
            std::cout << "[f1-06:piloto] t=" << ahora << " tabla de coches en 0x" << std::hex << e.tabla << " (jugador 0x"
                      << e.tabla + s_jugador * kCochePaso << ")" << std::dec << std::endl;
        }
        const uint32_t guia = e.tabla + e.guia * kCochePaso;
        const uint32_t yo = e.tabla + s_jugador * kCochePaso;
        if (!pareceCoche(rdram, guia) || !pareceCoche(rdram, yo))
        {
            e.tabla = 0u; // la carrera ha terminado o se ha movido la tabla
            return;
        }
        const auto distancia = [](const PuntoEstela &a, const PuntoEstela &b) { return std::hypot(b.x - a.x, b.z - a.z); };
        const size_t n = e.estela.size();
        // Indice k puntos despues de i (en bucle si la estela esta cerrada; n si se sale de una abierta).
        const auto avanzar = [&](size_t i, ptrdiff_t k) -> size_t
        {
            if (e.cerrada)
                return static_cast<size_t>(((static_cast<ptrdiff_t>(i) + k) % static_cast<ptrdiff_t>(n) +
                                            static_cast<ptrdiff_t>(n)) % static_cast<ptrdiff_t>(n));
            const ptrdiff_t j = static_cast<ptrdiff_t>(i) + k;
            return j < 0 ? 0u : static_cast<size_t>(std::min<ptrdiff_t>(j, static_cast<ptrdiff_t>(n)));
        };
        const auto masCercano = [&](size_t desde, float x, float z, ptrdiff_t atras, ptrdiff_t delante, float &mejorD)
        {
            size_t mejor = desde;
            mejorD = 1e30f;
            for (ptrdiff_t k = -atras; k < delante; ++k)
            {
                const size_t i = avanzar(desde, k);
                if (i >= n)
                    break;
                const float d = std::hypot(e.estela[i].x - x, e.estela[i].z - z);
                if (d < mejorD)
                {
                    mejorD = d;
                    mejor = i;
                }
            }
            return mejor;
        };

        // Estela del guia.
        const float gx = leerFloat(rdram, guia), gz = leerFloat(rdram, guia + 8u);
        const float gv = std::hypot(leerFloat(rdram, guia + 0x20u), leerFloat(rdram, guia + 0x28u));
        if (!e.cerrada)
        {
            if (e.estela.empty() || std::hypot(gx - e.estela.back().x, gz - e.estela.back().z) >= 2.0f)
                e.estela.push_back({gx, gz, gv});
            if (e.estela.size() > 500u && std::hypot(gx - e.estela.front().x, gz - e.estela.front().z) < 12.0f)
            {
                e.cerrada = true;
                std::cout << "[f1-06:piloto] t=" << ahora << " estela cerrada: " << e.estela.size() << " puntos" << std::endl;
            }
        }
        else
        {
            // Solo velocidades, y solo cuando el guia va por la estela.
            float d = 0.0f;
            e.cercanoGuia = masCercano(e.cercanoGuia, gx, gz, 10, 80, d);
            if (d < 4.0f)
                e.estela[e.cercanoGuia].v = gv;
        }
        if (!e.salida)
        {
            if (gv < 2.0f)
                return; // aun no se ha dado la salida: acelerar ahora es salida en falso
            e.salida = true;
            std::cout << "[f1-06:piloto] t=" << ahora << " salida" << std::endl;
        }
        if (!e.cerrada)
        {
            // Un guia parado mucho rato en la primera vuelta se ha retirado: sigue el coche en marcha mas cercano al
            // final de la estela.
            if (gv >= 1.0f)
                e.guiaParadoDesde = -1.0;
            else if (e.guiaParadoDesde < 0.0)
                e.guiaParadoDesde = ahora;
            else if (ahora - e.guiaParadoDesde > 5.0)
            {
                float mejorD = 1e30f;
                uint32_t mejor = e.guia;
                for (uint32_t c = 0; c < kCoches; ++c)
                {
                    const uint32_t a = e.tabla + c * kCochePaso;
                    if (c == e.guia || c == s_jugador ||
                        std::hypot(leerFloat(rdram, a + 0x20u), leerFloat(rdram, a + 0x28u)) < 5.0f)
                        continue;
                    const float d = std::hypot(leerFloat(rdram, a) - e.estela.back().x, leerFloat(rdram, a + 8u) - e.estela.back().z);
                    if (d < mejorD)
                    {
                        mejorD = d;
                        mejor = c;
                    }
                }
                std::cout << "[f1-06:piloto] t=" << ahora << " el guia " << e.guia << " esta parado: ahora guia el "
                          << mejor << " (a " << mejorD << " m del final de la estela)" << std::endl;
                e.guia = mejor;
                e.guiaParadoDesde = -1.0;
            }
        }
        // Mi coche.
        const float px = leerFloat(rdram, yo), pz = leerFloat(rdram, yo + 8u);
        const float v = std::hypot(leerFloat(rdram, yo + 0x20u), leerFloat(rdram, yo + 0x28u));
        float fx = leerFloat(rdram, yo - 0xD0u), fz = leerFloat(rdram, yo - 0xC8u);
        const float fn = std::hypot(fx, fz);
        if (fn < 0.1f || e.estela.empty())
            return;
        fx /= fn;
        fz /= fn;
        // Punto de la estela mas cercano (buscando hacia delante desde el ultimo) y punto objetivo por delante.
        float mejorD = 0.0f;
        const size_t mejor = masCercano(e.cercano, px, pz, 20, 400, mejorD);
        e.cercano = mejor;
        const float delante = std::clamp(10.0f + 0.6f * v, 12.0f, 45.0f);
        size_t objetivo = mejor;
        float recorrido = 0.0f;
        for (size_t paso = 0; recorrido < delante && paso < 200u; ++paso)
        {
            const size_t siguiente = avanzar(objetivo, 1);
            if (siguiente >= n)
                break;
            recorrido += distancia(e.estela[objetivo], e.estela[siguiente]);
            objetivo = siguiente;
        }
        // Velocidad objetivo: la minima del guia en los ~2,8 s siguientes (para frenar a tiempo).
        float minima = 1e30f;
        float tramo = 0.0f;
        for (size_t i = mejor, paso = 0; tramo < 20.0f + 2.8f * v && paso < 1000u; ++paso)
        {
            minima = std::min(minima, e.estela[i].v);
            const size_t siguiente = avanzar(i, 1);
            if (siguiente >= n)
                break;
            tramo += distancia(e.estela[i], e.estela[siguiente]);
            i = siguiente;
        }
        if (minima > 1e29f)
            minima = 0.0f;
        const float meta = std::max(8.0f, s_ritmo * minima);
        // Direccion (persecucion pura): angulo con signo hacia el objetivo; negativo = a la derecha.
        float dx = e.estela[objetivo].x - px, dz = e.estela[objetivo].z - pz;
        const float dn = std::hypot(dx, dz);
        float angulo = 0.0f;
        if (dn > 0.5f)
        {
            dx /= dn;
            dz /= dn;
            angulo = std::atan2(fx * dz - fz * dx, fx * dx + fz * dz);
        }
        float giro = std::clamp(-angulo * 2.5f, -1.0f, 1.0f); // +1 = todo a la derecha
        // Parado (choque, trompo): marcha atras un rato girando al reves (el morro va hacia el otro lado) y otra vez
        // hacia la estela.
        bool atras = ahora < e.recuperarHasta;
        if (!atras)
        {
            if (v >= 2.0f)
                e.paradoDesde = -1.0;
            else if (e.paradoDesde < 0.0)
                e.paradoDesde = ahora;
            else if (ahora - e.paradoDesde > 3.0)
            {
                e.recuperarHasta = ahora + 2.5;
                e.paradoDesde = -1.0;
                atras = true;
                std::cout << "[f1-06:piloto] t=" << ahora << " parado en (" << px << ", " << pz << "), a " << mejorD
                          << " m de la estela: marcha atras" << std::endl;
            }
        }
        uint8_t &botones = rdram[(datos + 3u) & PS2_RAM_MASK];
        bool acelerar = false;
        bool frenar = false;
        if (atras)
        {
            giro = -giro;
            frenar = true; // parado, el freno engrana la marcha atras
            botones = static_cast<uint8_t>(botones & ~(1u << 7)); // cuadrado
            rdram[(datos + 15u) & PS2_RAM_MASK] = 0xFFu;
        }
        else
        {
            acelerar = v < meta;
            frenar = v > meta + 3.0f && v > 4.0f; // parado, el freno engrana la marcha atras
            if (acelerar)
            {
                botones = static_cast<uint8_t>(botones & ~(1u << 6)); // cruz
                rdram[(datos + 14u) & PS2_RAM_MASK] = 0xFFu;
            }
            if (frenar)
            {
                botones = static_cast<uint8_t>(botones & ~(1u << 7)); // cuadrado
                rdram[(datos + 15u) & PS2_RAM_MASK] =
                    static_cast<uint8_t>(std::clamp(64.0f + 24.0f * (v - meta), 64.0f, 255.0f));
            }
        }
        rdram[(datos + 6u) & PS2_RAM_MASK] = static_cast<uint8_t>(std::lround(127.5f + giro * 127.5f));
        if (ahora - e.ultimoInforme >= 2.0)
        {
            e.ultimoInforme = ahora;
            std::cout << "[f1-06:piloto] t=" << ahora << " pos=(" << px << ", " << pz << ") v=" << v << " meta=" << meta
                      << " angulo=" << angulo << " giro=" << giro << (acelerar ? " acelera" : "")
                      << (atras ? " atras" : frenar ? " frena" : "") << " estela=" << e.estela.size()
                      << (e.cerrada ? " (cerrada)" : "") << " cercano=" << mejor << " a " << mejorD << " m guia="
                      << e.guia << std::endl;
        }
    }

    namespace volante
    {
        void espejo(const uint8_t *rdram, uint32_t datos);
    }

    void trazar_scePadRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t lecturas[2] = {0, 0};
        const uint32_t puerto = GPR_U32(ctx, 4);
        const uint32_t datos = GPR_U32(ctx, 6);
        g_scePadReadOriginal(rdram, ctx, runtime);
        const double ahora = segundosDesdeArranque();
        if (puerto < 2u && (lecturas[puerto]++ % 1000u) == 0u)
            std::cout << "[f1-06:pad] scePadRead puerto=" << puerto << " lectura=" << lecturas[puerto] << " t=" << ahora
                      << " v0=" << GPR_U32(ctx, 2) << " modo=0x" << std::hex << int(rdram[(datos + 1u) & PS2_RAM_MASK])
                      << " botones=0x" << (rdram[(datos + 2u) & PS2_RAM_MASK] | (rdram[(datos + 3u) & PS2_RAM_MASK] << 8))
                      << std::dec << std::endl;
        // Suceso "pad" del guion: la primera lectura del puerto 0, cuando sale el menu de idioma (su hora varia mucho
        // entre arranques). Ejemplo: F1_GUION='pad+3:cruz:0.5,pad+8:cruz:0.5,...'.
        if (puerto == 0u && lecturas[0] == 1u && !g_guion.empty())
            dispararGuion("pad");
        if (puerto == 0u)
            pilotar(rdram, datos, ahora);
        if (puerto != 0u)
            return;
        for (PulsacionProgramada &p : g_pulsaciones)
        {
            if (ahora >= p.segundo && (ahora < p.segundo + p.duracion || p.lecturas == 0u))
            {
                // Las pulsaciones largas (conducir) solo se registran la primera vez.
                if (p.lecturas++ == 0u)
                    std::cout << "[f1-06:pulsar] t=" << ahora << " byte " << p.byte << " bit " << int(p.bit)
                              << " durante " << p.duracion << " s" << std::endl;
                if (p.bit < 8u)
                    rdram[(datos + p.byte) & PS2_RAM_MASK] &= static_cast<uint8_t>(~(1u << p.bit));
                if (p.bytePresion >= 0)
                    rdram[(datos + static_cast<uint32_t>(p.bytePresion)) & PS2_RAM_MASK] = 0xFFu;
                if (p.byteStick >= 0)
                    rdram[(datos + static_cast<uint32_t>(p.byteStick)) & PS2_RAM_MASK] = p.valorStick;
            }
        }
        volante::espejo(rdram, datos); // F1_VOLANTE_FALSO: el volante del juego sigue a este mando
    }

    // Videos .pss (Movies\PAL\INTRO.pss tras elegir idioma, y el de la demo): los decodifica el HLE de sceMpeg de
    // PS2Recomp con FFmpeg (parche 0101: llama al callback de segundo plano del juego, 0x29ACE8, para que lea y
    // demultiplexe, y pone sys->isEnd para su sceMpegIsEnd, 0x2EAF18). Hasta el 0101 se saltaban: sceMpegIsEnd decia
    // "terminado" (salvo a la pregunta de Lua de los 2 primeros segundos, porque el grafo descarta un resultado que
    // llega durante una transicion y la pantalla se quedaba en blanco).
    // Fase 5 (T4): opciones.ini saltar_intro = 1 los corta. Son los unicos sceMpeg del juego (el disco solo tiene
    // Movies\PAL\INTRO.PSS: la intro y la demostracion de la pantalla de titulo, Attract); los fondos .ipf de los menus
    // y de los circuitos los decodifica el juego con el IPU y no pasan por aqui. Pasados kSegundosVideoSaltado desde su
    // sceMpegCreate, sceMpegIsEnd (0x2EAF18, codigo del juego) responde "terminado" al reproductor (func_29A778) y a la
    // pregunta del script (func_3D53A0), como al acabar el video; antes no, por lo de la transicion. La opcion se mira
    // en cada llamada (AJUSTES PC la cambia en caliente). Solo cambia v0 tras la funcion original, que ya
    // fija pc = ra (el atajo de antes del 0101 colgaba por no hacerlo).
    constexpr uint32_t kSceMpegCreate = 0x002EA2E0u;
    constexpr uint32_t kSceMpegIsEnd = 0x002EAF18u;
    constexpr double kSegundosVideoSaltado = 2.0;
    PS2Runtime::RecompiledFunction g_sceMpegCreateOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_sceMpegIsEndOriginal = nullptr;
    std::map<uint32_t, double> g_videosCreados; // mpeg -> segundo de su sceMpegCreate (hilo del EE)
    std::map<uint32_t, bool> g_videosSaltados;  // mpeg -> ya se registro el salto

    bool saltarIntro()
    {
#if defined(PS2X_ENABLE_DEBUG_UI)
        float valor = 0.0f;
        if (PS2OptionsMenu::gameOptionNumber("saltar_intro", valor))
            return valor >= 0.5f;
#endif
        const char *valor_entorno = std::getenv("PS2X_SALTAR_INTRO");
        return valor_entorno != nullptr && valor_entorno[0] == '1';
    }

    void trazarSceMpegCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t mpeg = GPR_U32(ctx, 4);
        g_videosCreados[mpeg] = segundosDesdeArranque();
        g_videosSaltados.erase(mpeg);
        g_sceMpegCreateOriginal(rdram, ctx, runtime);
    }

    void trazarSceMpegIsEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t mpeg = GPR_U32(ctx, 4);
        g_sceMpegIsEndOriginal(rdram, ctx, runtime);
        if (GPR_U32(ctx, 2) != 0u)
            return;
        const auto creado = g_videosCreados.find(mpeg);
        if (creado == g_videosCreados.end())
            return;
        const double ahora = segundosDesdeArranque();
        if (ahora - creado->second < kSegundosVideoSaltado || !saltarIntro())
            return;
        if (!g_videosSaltados[mpeg])
        {
            g_videosSaltados[mpeg] = true;
            std::cout << "[f1-06:intro] t=" << ahora << " video saltado a los " << (ahora - creado->second)
                      << " s (saltar_intro = 1)" << std::endl;
        }
        SET_GPR_U32(ctx, 2, 1u);
    }

    void instalarSaltoIntro(PS2Runtime &runtime)
    {
        instalarChivato(runtime, kSceMpegCreate, trazarSceMpegCreate, g_sceMpegCreateOriginal);
        instalarChivato(runtime, kSceMpegIsEnd, trazarSceMpegIsEnd, g_sceMpegIsEndOriginal);
        std::cout << "[f1-06:intro] saltar_intro = " << (saltarIntro() ? 1 : 0) << std::endl;
    }

    // Fase 5 (T9): el HUD de la carrera. func_1F6698 construye el HUD de cada jugador: sus fabricas (0x3B1CB0-0x3B3BB0)
    // crean los elementos (clases HUDElement*, base func_22C478 con la vtable 0x41B4C8) y los guardan en dos vectores de
    // punteros: hud + 0xC/0x10 (todos) y hud + 0x1C/0x20 (los activos). func_1F6C60(hud) rehace los activos: un
    // elemento entra si su metodo +0x1C dice que su opcion de SCREEN LAYOUT esta puesta en la copia del jugador en la
    // carrera (*0x3FE588 + jugador * 0x78 + 0x38: +0 posiciones, +8 vueltas, +0xC tiempos, +0x10 banderas, +0x1C
    // salpicadero, +0x34 mapa...; el orden de los campos del perfil *0x49ED94 + 0x1F8) y su mascara (+0x44) casa con la de
    // la vista (hud + 8); luego los inicia (+0x14, +0x24). Cada fotograma func_1F6448 (vtable 0x41686C, una vez por
    // vista) actualiza los activos con func_1F6B28 (metodo +0x2C) y los dibuja con func_1F6BB0(hud): para cada uno, si el
    // +0x3C dice que se ve, el +0x34 lo dibuja (los paquetes GIF del EE del parche 0148). Los sprites de varios elementos
    // (posiciones, salpicadero, mapa) se quedan en una lista comun que dibuja el +0x34 de cualquier otro: quitar
    // elementos solo del dibujo no los quita de la pantalla; hay que rehacer los activos.
    // opciones.ini hud = completo, minimo u oculto (se mira en cada fotograma). Oculto: no se llama a func_1F6BB0 (los
    // elementos se siguen actualizando y al volver el HUD esta al dia). Minimo: los activos se rehacen con
    // func_1F6C60 como si en SCREEN LAYOUT solo estuvieran puestos las posiciones, las banderas, el velocimetro y el
    // salpicadero (velocidad, marcha y ayudas); las opciones del jugador se dejan como estaban justo despues. Al entrar o
    // salir del minimo se rehacen otra vez. El semaforo de la salida, la pausa, los menus, la repeticion y avisos como
    // "Disqualified" no son elementos de este HUD. tecla_hud (H por defecto) cambia el modo en caliente
    // (ps2_options_menu.cpp).
    constexpr uint32_t kConstruirHud = 0x001F6698u;
    constexpr uint32_t kReconstruirHud = 0x001F6C60u;
    constexpr uint32_t kDibujarHud = 0x001F6BB0u;
    constexpr uint32_t kCopiaJugadoresCarrera = 0x003FE588u;
    PS2Runtime::RecompiledFunction g_construirHudOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_reconstruirHudOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_dibujarHudOriginal = nullptr;
    std::map<uint32_t, bool> g_hudMinimoAplicado; // hud -> sus activos se hicieron con hud = minimo (hilo del EE)

    // Las opciones de SCREEN LAYOUT que se apagan con hud = minimo (desplazamientos en la copia del jugador): la
    // procedencia (+4), las vueltas (+8), los tiempos (+0xC), la dinamica (+0x14), el estado del coche, la peticion de
    // boxes y el combustible (+0x20, +0x24, +0x28), las curvas (+0x2C), la marcha sugerida (+0x30) y el mapa (+0x34).
    constexpr uint32_t kOpcionesFueraMinimo[] = {0x04u, 0x08u, 0x0Cu, 0x14u, 0x20u, 0x24u, 0x28u, 0x2Cu, 0x30u, 0x34u};

    ps2x::HudMode modoHud()
    {
        if (ps2x::photoModeActive()) // fase 5 (T13): el modo foto, sin HUD
            return ps2x::HudMode::Hidden;
#if defined(PS2X_ENABLE_DEBUG_UI)
        float valor = 0.0f;
        if (PS2OptionsMenu::gameOptionNumber("hud", valor))
        {
            const long numero = std::lround(valor);
            return numero == 1 ? ps2x::HudMode::Minimal : numero == 2 ? ps2x::HudMode::Hidden : ps2x::HudMode::Full;
        }
#endif
        ps2x::HudMode modo = ps2x::HudMode::Full;
        if (const char *valor = std::getenv("PS2X_HUD"))
            ps2x::parseHudMode(valor, modo);
        return modo;
    }

    // func_1F6698(hud, ...): un HUD nuevo (o en la direccion de uno viejo) tiene los activos del juego.
    void construirHud(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t hud = GPR_U32(ctx, 4);
        g_construirHudOriginal(rdram, ctx, runtime);
        g_hudMinimoAplicado[hud] = false;
    }

    // func_1F6C60(hud): con hud = minimo, rehace los activos como si esas opciones estuvieran apagadas.
    void reconstruirHud(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t hud = GPR_U32(ctx, 4);
        const bool minimo = modoHud() == ps2x::HudMode::Minimal;
        g_hudMinimoAplicado[hud] = minimo;
        const uint32_t jugador = leer32(rdram, leer32(rdram, hud) + 0x24u);
        const uint32_t opciones = leer32(rdram, kCopiaJugadoresCarrera) + jugador * 0x78u + 0x38u;
        uint32_t guardadas[std::size(kOpcionesFueraMinimo)] = {};
        if (minimo)
            for (size_t i = 0; i < std::size(kOpcionesFueraMinimo); ++i)
            {
                guardadas[i] = leer32(rdram, opciones + kOpcionesFueraMinimo[i]);
                const uint32_t cero = 0u;
                std::memcpy(rdram + ((opciones + kOpcionesFueraMinimo[i]) & PS2_RAM_MASK), &cero, 4u);
            }
        g_reconstruirHudOriginal(rdram, ctx, runtime);
        if (minimo)
            for (size_t i = 0; i < std::size(kOpcionesFueraMinimo); ++i)
                std::memcpy(rdram + ((opciones + kOpcionesFueraMinimo[i]) & PS2_RAM_MASK), &guardadas[i], 4u);
        const uint32_t activos = (leer32(rdram, hud + 0x20u) - leer32(rdram, hud + 0x1Cu)) / 4u;
        std::cout << "[f1-06:hud] t=" << segundosDesdeArranque() << " activos del HUD 0x" << std::hex << hud << std::dec
                  << " rehechos" << (minimo ? " (minimo): " : ": ") << activos << std::endl;
    }

    void dibujarHud(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t hud = GPR_U32(ctx, 4);
        const ps2x::HudMode modo = modoHud();
        static ps2x::HudMode anterior = ps2x::HudMode::Full;
        if (modo != anterior)
        {
            std::cout << "[f1-06:hud] t=" << segundosDesdeArranque() << " hud = " << ps2x::hudModeOption(modo)
                      << std::endl;
            anterior = modo;
        }
        if (modo == ps2x::HudMode::Hidden)
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        // Al entrar o salir del minimo se rehacen los activos con la funcion del juego. Es la entrada de una funcion: los
        // registros temporales no importan y los guardados los respeta la del juego; se dejan todos como estaban.
        const auto aplicado = g_hudMinimoAplicado.find(hud);
        const bool minimoAplicado = aplicado != g_hudMinimoAplicado.end() && aplicado->second;
        if (minimoAplicado != (modo == ps2x::HudMode::Minimal))
        {
            __m128i registros[32];
            std::memcpy(registros, ctx->r, sizeof(registros));
            const uint32_t pc = ctx->pc;
            reconstruirHud(rdram, ctx, runtime);
            std::memcpy(ctx->r, registros, sizeof(registros));
            ctx->pc = pc;
        }
        g_dibujarHudOriginal(rdram, ctx, runtime);
    }

    void instalarHud(PS2Runtime &runtime)
    {
        instalarChivato(runtime, kConstruirHud, construirHud, g_construirHudOriginal);
        instalarChivato(runtime, kReconstruirHud, reconstruirHud, g_reconstruirHudOriginal);
        instalarChivato(runtime, kDibujarHud, dibujarHud, g_dibujarHudOriginal);
        std::cout << "[f1-06:hud] hud = " << ps2x::hudModeOption(modoHud()) << std::endl;
    }

    // Fase 5 (T12): el campo de vision de las camaras de la carrera. Una camara (clase en 0x2C5510-0x2C59E0) guarda su
    // medio angulo vertical en + 0x88 (func_2C5600 o func_2C5730: matriz de vista, panoramica, f12 = angulo, f13/f14 =
    // cerca/lejos en + 0x90/+ 0x94; el juego lo limita a [0,001, pi/2]) y el horizontal en + 0x84 = atan(tan(angulo) *
    // aspecto); aspecto = func_2C5510(panoramica) = 4/3 o 16/9 (por 0,875 si *0x4B1B5C == 2): la panoramica del juego ya
    // ve mas a los lados con el mismo angulo vertical (hor+, la imagen de 640 de ancho comprimida). En + 0x8C, el aspecto
    // de func_2C54A0 (el mismo). Cada fotograma la vista (func_2D2668) toma el horizontal (+ 0x84) para la proyeccion
    // (tan en + 0x3BC, el vertical = tan / aspecto) y para los planos del descarte por visibilidad (func_2C9590 en
    // vista + 0x1A0; los usa func_11E280 con la VU0): abrir el angulo abre tambien el descarte.
    // Las camaras de la carrera del jugador (cabina, casco, morro, persecucion cercana y lejana, la de la salida) pasan
    // cada fotograma por func_2C5948(camara, datos, panoramica, a3, f12 = angulo de sus datos + 0x90), llamada solo por
    // el metodo 0x1169E0 (vtable 0x412B54); angulos vistos en Silverstone: 0,26 (salida), 0,38, 0,48, 0,52 y 0,57.
    // Otras: func_325E38 (una vista fija de pi/4 por fotograma, camara 0x1C623B0), func_325460 (pi/4 al crearla) y las de
    // func_323580/func_323C28 y func_248628/func_248B50 (angulo de sus datos) no se tocan. opciones.ini fov (0 a 30 %):
    // el angulo se abre en func_2C5948 antes de llamarla (ps2x::widenedCameraFov), en cada fotograma; el juego vuelve a
    // pasar su angulo cada vez, asi que no se acumula y un cambio se ve en el siguiente fotograma. El HUD es 2D aparte.
    // F1_FOV_DIAG=1: [f1-06:fov] cada 5 s, quien llama a func_2C5730 y func_2C5600, la camara y sus angulos.
    constexpr uint32_t kCamaraCarrera = 0x002C5948u;
    constexpr uint32_t kCamaraVistaFov = 0x002C5730u;
    constexpr uint32_t kCamaraFov = 0x002C5600u;
    PS2Runtime::RecompiledFunction g_camaraCarreraOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_camaraVistaFovOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_camaraFovOriginal = nullptr;

    // Fase 5 (T13): el modo foto (ps2_photo_mode.h; tecla_modo_foto, F8 por defecto). La carrera avanza en pasos fijos:
    // cada fotograma func_144148 llama a func_143ED0(n) con n = los VBLANK desde el anterior (*0x3FBE14, lo cuenta
    // func_143D08), y func_143ED0 llama n veces al metodo + 0x134 de *0x3FE3EC (0x195900 en carrera);
    // luego func_1F2108 actualiza las vistas (camaras: metodo 0x1169E0 -> func_2C5948 -> func_2C5730) y dibuja
    // (func_1F23F0: HUD, escena). Congelar = no llamar a func_143ED0: el juego sigue dibujando la escena quieta cada
    // fotograma, sin pasos que recuperar al volver (n es de un solo fotograma). La camara: el metodo 0x1169E0 copia la
    // matriz de la camara del juego (sus datos + 0x10: filas 0-2 los ejes, fila 3 la posicion; el arriba del mundo es +y)
    // a su pila y llama a func_2C5948(camara = datos + 0xA0, copia, ...); func_2C5730 hace con ella la matriz de vista
    // (camara + 0x40, la inversa) y el angulo (+ 0x88, + 0x84), y func_2D2668 la proyeccion y los planos del descarte
    // (func_2C9590). Con el modo foto la copia de la pila se cambia por la de la camara libre antes de func_2C5948 (no se
    // toca nada del juego: al salir vuelve su camara, que no se ha movido) y f12 por su angulo, asi que el descarte sigue a
    // la camara libre. Solo la primera camara que pasa al entrar (en pantalla partida, la del jugador 1). En la repeticion
    // sus camaras de television pasan tambien por func_2C5948 (sus letreros no son del HUD; Camaras y pilotos T2: se quitan
    // aparte, hudRepeticionDibujar). La ventana pone y
    // quita el modo, lo quita si ninguna camara de carrera pasa en 2 s, silencia el sonido y deja el mando del juego en
    // reposo (ps2_runtime.cpp, ps2_pad.cpp). [f1-06:modo-foto] al entrar, al salir y cada segundo con la camara libre.
    constexpr uint32_t kPasosCarrera = 0x00143ED0u;
    PS2Runtime::RecompiledFunction g_pasosCarreraOriginal = nullptr;

    struct ModoFoto // hilo del EE
    {
        bool iniciado = false;
        uint32_t camara = 0u;
        ps2x::FreeCamera libre;
        std::chrono::steady_clock::time_point ultimo;
        double registro = 0.0;
        uint32_t otras = 0u;
        double cambio = -1.0; // Foto y repeticion T5: cuando cambiaron el coche o la camara (segundosDesdeArranque)
        bool movida = false;  // y si desde entonces se ha movido la camara libre
    };
    ModoFoto g_modoFoto;

    // Fase 5 (T14): atras, adelante y pausa en la repeticion (ps2_replay_scrub.h; J, L y K, o L2, R2 y SELECT). La
    // repeticion no guarda las posiciones de los coches: es la carrera simulada otra vez. El juego (*0x3FE3EC, el de la
    // carrera: metodo + 0x134 = 0x195900) tiene en + 0xC un gestor (func_1A40C8, creado por func_1A0EE8 con 0x780 pasos
    // y 2 ranuras): + 0x34 el modo (1 graba, 2 reproduce), + 0x8 las ranuras (16 bytes cada una en + 0x40: usada, la
    // instantanea, su paso), + 0xC los pasos que caben (1920 = 38,4 s a 50 pasos/s), + 0x1C los pasos grabados, + 0x28
    // cada cuanto una instantanea (la mitad: 960), + 0x38 la lista de las entradas grabadas y en la reproduccion + 0x20
    // su longitud y + 0x24 el cursor. Una instantanea es el estado entero de la carrera: func_1BFD58(lista, bufer) la
    // guarda (cada objeto registrado, metodo + 0x28, con el contador de pasos 0x4A0CB0 y *0x4A0D58) y func_1BFE00 la
    // restaura (metodo + 0x30). REPLAY (pause.event 2 -> metodo + 0xB4 = func_1965D0) pone + 0x24 del juego a 1 y
    // func_1A48E0 -> func_1A47B8 guarda el estado en vivo en una ranura (para STOP REPLAY, func_1A4910 -> func_1A4820) y
    // func_1A4668 restaura la instantanea mas antigua que siga dentro de lo grabado (func_1A4540, func_1A45B8: cursor 0,
    // longitud = grabados - su paso, las entradas desde ahi). Al final de cada paso func_1A1000 -> func_1A4888 ->
    // func_1A4710 avanza el cursor y, al llegar a la longitud, vuelve a empezar (func_1A4668): la repeticion da vueltas.
    // Pausa = cero pasos; adelante = n x 2 o x 4; atras = el cursor en longitud - 1 y 1 + destino pasos en este
    // fotograma: el primero hace que el propio juego empiece otra vez (su mismo camino del final) y los demas llegan
    // al destino sin dibujarse. Solo con la repeticion en marcha (+ 0x20 del juego = 4, sin el menu de pausa: + 0x568).
    // Cada paso cuesta al PC unos 3 ms (539 pasos en 1,6 s; el avance rapido del reloj de las cargas rapidas no cambia
    // nada: el EE no espera, calcula): por eso J mantenida solo mueve el destino y el salto se hace al soltarla.
    // [f1-06:repeticion] al entrar, al salir, cada segundo y en cada salto atras con lo que tarda (hasta el fotograma
    // siguiente: los pasos pueden ceder el hilo y seguir despues); con el piloto automatico (F1_PILOTO, que encuentra
    // la tabla de coches), la posicion del jugador cada 100 pasos del cursor: tras volver atras ha de repetirse.
    constexpr uint32_t kJuegoActual = 0x003FE3ECu;
    constexpr uint32_t kPasoCarreraMetodo = 0x00195900u;
    ps2x::ReplayScrubber g_repeticion; // hilo del EE

    bool punteroEe(uint32_t direccion)
    {
        return direccion >= 0x00100000u && direccion < 0x02000000u && (direccion & 3u) == 0u;
    }

    struct SaltoRepeticion
    {
        bool medir = false;
        uint32_t gestor = 0u, pasos = 0u;
        int32_t desde = 0, destino = 0;
        std::chrono::steady_clock::time_point inicio;
    };
    SaltoRepeticion g_saltoRepeticion; // el ultimo salto atras, hasta el fotograma siguiente

    // Al entrar en func_143ED0(n): cambia n (y el cursor para volver atras) segun los controles.
    SaltoRepeticion repeticionPasos(uint8_t *rdram, R5900Context *ctx)
    {
        static bool enRepeticion = false;
        static double registro = 0.0;
        ps2x::setReplayScrubSeeking(false);
        if (g_saltoRepeticion.medir)
        {
            const SaltoRepeticion &s = g_saltoRepeticion;
            std::cout << "[f1-06:repeticion] t=" << segundosDesdeArranque() << " atras de " << s.desde << " a "
                      << s.destino << ": " << s.pasos << " pasos en "
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s.inicio).count()
                      << " ms, cursor " << leer32(rdram, s.gestor + 0x24u) << " de " << leer32(rdram, s.gestor + 0x20u)
                      << std::endl;
            g_saltoRepeticion.medir = false;
        }
        SaltoRepeticion salto;
        const uint32_t juego = leer32(rdram, kJuegoActual);
        uint32_t gestor = 0u;
        bool valida = false, menuPausa = false;
        ps2x::ReplayPosition posicion;
        if (punteroEe(juego) && leer32(rdram, juego + 0x24u) != 0u && leer32(rdram, juego + 0x20u) == 4u)
        {
            const uint32_t vtabla = leer32(rdram, juego + 0x7Cu);
            gestor = leer32(rdram, juego + 0xCu);
            if (punteroEe(vtabla) && leer32(rdram, vtabla + 0x134u) == kPasoCarreraMetodo && punteroEe(gestor) &&
                leer32(rdram, gestor + 0x34u) == 2u)
            {
                posicion.position = static_cast<int32_t>(leer32(rdram, gestor + 0x24u));
                posicion.length = static_cast<int32_t>(leer32(rdram, gestor + 0x20u));
                valida = ps2x::validReplayPosition(posicion);
                menuPausa = leer32(rdram, juego + 0x568u) != 0u;
            }
        }
        const double t = segundosDesdeArranque();
        if (!valida)
        {
            if (enRepeticion)
            {
                std::cout << "[f1-06:repeticion] t=" << t << " sale de la repeticion" << std::endl;
                g_repeticion.reset();
                ps2x::setReplayScrubState(1, false);
            }
            enRepeticion = false;
            return salto;
        }
        if (!enRepeticion)
        {
            enRepeticion = true;
            std::cout << "[f1-06:repeticion] t=" << t << " entra: juego 0x" << std::hex << juego << " gestor 0x" << gestor
                      << std::dec << " longitud " << posicion.length << " pasos, cursor " << posicion.position
                      << ", ranuras " << leer32(rdram, gestor + 0x8u) << ", caben " << leer32(rdram, gestor + 0xCu)
                      << ", grabados " << leer32(rdram, gestor + 0x1Cu) << ", instantanea cada "
                      << leer32(rdram, gestor + 0x28u) << std::endl;
        }
        if (menuPausa) // el menu de pausa del juego: sus pasos solo mueven el menu
            return salto;
        const double pasosPorSegundo = leerFloat(rdram, kPasoLogica + 8u);
        const uint32_t n = GPR_U32(ctx, 4);
        const ps2x::ReplayStepPlan plan =
            g_repeticion.plan(ps2x::replayScrubControls(), posicion, n, pasosPorSegundo, t);
        if (plan.restart)
        {
            const uint32_t ultimo = static_cast<uint32_t>(posicion.length - 1);
            std::memcpy(rdram + ((gestor + 0x24u) & PS2_RAM_MASK), &ultimo, 4u);
            salto.medir = true;
            salto.gestor = gestor;
            salto.pasos = plan.steps;
            salto.desde = posicion.position;
            salto.destino = plan.target;
            salto.inicio = std::chrono::steady_clock::now();
            ps2x::setReplayScrubSeeking(true);
        }
        ps2x::replayScrubSeen({plan.speed == -1 ? plan.target : posicion.position, posicion.length}, pasosPorSegundo);
        SET_GPR_U32(ctx, 4, plan.steps);
        ps2x::setReplayScrubState(plan.speed, g_repeticion.muted(t));
        if (t - registro >= 1.0)
        {
            registro = t;
            std::cout << "[f1-06:repeticion] t=" << t << " cursor " << posicion.position << " de " << posicion.length
                      << " velocidad " << plan.speed << " pasos " << plan.steps << " (n " << n << ")" << std::endl;
        }
        if (posicion.position % 100 == 0 && plan.steps > 0u && g_piloto.tabla != 0u)
        {
            const uint32_t jugador = g_piloto.tabla + (kCoches - 1u) * 0x4F0u;
            std::cout << "[f1-06:repeticion] t=" << t << " cursor " << posicion.position << ": jugador en ("
                      << leerFloat(rdram, jugador) << ", " << leerFloat(rdram, jugador + 8u) << ")" << std::endl;
        }
        return salto;
    }

    // Fase 5 (T16): F1_IA_MEDIR=1 mide lo que avanzan los coches en tiempo de juego (pasos de la carrera, 50 por
    // segundo, no el reloj del PC: el PC puede ir cargado). Con la tabla de coches del piloto automatico (posiciones),
    // la salida es el primer paso en que un coche se ha movido mas de 1 m de la parrilla; desde ahi suma lo que recorre
    // cada coche en el plano (x, z) y cada 30 s de juego escribe [f1-06:ia-medida] con lo recorrido por cada uno (el
    // jugador, el ultimo de la tabla, aparte). Solo en carrera (sin la repeticion ni el menu de pausa).
    struct MedidaIa
    {
        uint32_t tabla = 0u;
        double ultimaBusqueda = -10.0;
        bool posiciones = false;
        bool salida = false;
        uint64_t pasos = 0u;
        uint64_t siguiente = 1500u;
        float x[kCoches] = {}, z[kCoches] = {}, x0[kCoches] = {}, z0[kCoches] = {};
        double recorrido[kCoches] = {};
    };
    MedidaIa g_medidaIa; // hilo del EE

    void medirIa(const uint8_t *rdram, uint32_t n)
    {
        const uint32_t juego = leer32(rdram, kJuegoActual);
        if (!punteroEe(juego) || leer32(rdram, juego + 0x24u) != 0u || leer32(rdram, juego + 0x568u) != 0u)
            return;
        const uint32_t vtabla = leer32(rdram, juego + 0x7Cu);
        if (!punteroEe(vtabla) || leer32(rdram, vtabla + 0x134u) != kPasoCarreraMetodo)
            return;
        MedidaIa &m = g_medidaIa;
        if (m.tabla == 0u || !pareceCoche(rdram, m.tabla))
        {
            const double t = segundosDesdeArranque();
            if (t - m.ultimaBusqueda < 1.0)
                return;
            m = MedidaIa{};
            m.ultimaBusqueda = t;
            m.tabla = buscarTablaCoches(rdram);
            if (m.tabla != 0u)
                std::cout << "[f1-06:ia-medida] t=" << t << " coches en 0x" << std::hex << m.tabla << std::dec << std::endl;
            return;
        }
        float x[kCoches], z[kCoches];
        for (uint32_t i = 0; i < kCoches; ++i)
        {
            x[i] = leerFloat(rdram, m.tabla + i * kCochePaso);
            z[i] = leerFloat(rdram, m.tabla + i * kCochePaso + 8u);
        }
        if (!m.posiciones)
        {
            std::copy(x, x + kCoches, m.x0);
            std::copy(z, z + kCoches, m.z0);
        }
        else if (!m.salida)
        {
            for (uint32_t i = 0; i + 1u < kCoches; ++i)
                m.salida = m.salida || std::hypot(x[i] - m.x0[i], z[i] - m.z0[i]) > 1.0f;
            if (m.salida)
                std::cout << "[f1-06:ia-medida] t=" << segundosDesdeArranque() << " salida" << std::endl;
        }
        else
        {
            m.pasos += n;
            for (uint32_t i = 0; i < kCoches; ++i)
            {
                const double d = std::hypot(x[i] - m.x[i], z[i] - m.z[i]);
                if (d < 100.0)
                    m.recorrido[i] += d;
            }
            while (m.pasos >= m.siguiente && m.siguiente <= 9000u)
            {
                double suma = 0.0, mas = 0.0, menos = 1e30;
                std::ostringstream cada;
                for (uint32_t i = 0; i + 1u < kCoches; ++i)
                {
                    suma += m.recorrido[i];
                    mas = std::max(mas, m.recorrido[i]);
                    menos = std::min(menos, m.recorrido[i]);
                    cada << ' ' << static_cast<int>(std::lround(m.recorrido[i]));
                }
                std::cout << "[f1-06:ia-medida] t=" << segundosDesdeArranque() << " juego " << m.siguiente / 50u
                          << " s (" << m.siguiente << " pasos): IA media " << std::lround(suma / (kCoches - 1u))
                          << " m, primero " << std::lround(mas) << " m, ultimo " << std::lround(menos) << " m, jugador "
                          << std::lround(m.recorrido[kCoches - 1u]) << " m; cada coche:" << cada.str() << std::endl;
                m.siguiente += 1500u;
            }
        }
        m.posiciones = true;
        std::copy(x, x + kCoches, m.x);
        std::copy(z, z + kCoches, m.z);
    }

    // F1_MARCHAS_DIAG=1 (B1, diagnostico del cambio automatico): [f1-06:marchas] en cada cambio de marcha del jugador
    // (el entero de su objeto en posicion - 0x98, el ultimo de la tabla de coches), paso a paso de la carrera (metodo
    // 0x195900), con la velocidad, el numero de pasos de ese fotograma y cuantos lleva en la marcha anterior; y cada
    // 30 s los saltos de mas de una marcha de todos los coches. F1_MARCHAS_DIAG=2 vigila ademas la marcha del jugador
    // con DR0 ([vigia] pc y ra de las primeras escrituras).
    PS2Runtime::RecompiledFunction g_pasoCarreraOriginal = nullptr;
    uint32_t fisicaJugador(const uint8_t *rdram);
    void informeAveria(const uint8_t *rdram, const char *que, uint32_t fisica, uint32_t ra, uint32_t bits);
    struct DiagMarchas
    {
        uint32_t tabla = 0u;
        double ultimaBusqueda = -10.0;
        uint64_t paso = 0u, pasoCambio = 0u;
        uint32_t nFotograma = 0u;
        int32_t marcha[kCoches] = {};
        bool hay = false;
        uint64_t cambios = 0u, saltos = 0u, saltosJugador = 0u;
        double informe = 0.0;
    };
    DiagMarchas g_marchas; // hilo del EE
    int g_marchasDiag = 0;
    std::atomic<uint32_t> g_marchasVigilar{0u};

    void pasoCarrera(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == kPasoCarreraMetodo;
        g_pasoCarreraOriginal(rdram, ctx, runtime);
        if (!entrada)
            return;
        DiagMarchas &m = g_marchas;
        const double t = segundosDesdeArranque();
        if (m.tabla == 0u || !pareceCoche(rdram, m.tabla))
        {
            if (t - m.ultimaBusqueda < 1.0)
                return;
            m = DiagMarchas{};
            m.ultimaBusqueda = t;
            m.tabla = buscarTablaCoches(rdram);
            if (m.tabla != 0u)
            {
                std::cout << "[f1-06:marchas] t=" << t << " coches en 0x" << std::hex << m.tabla << ", marcha del jugador en 0x"
                          << m.tabla + (kCoches - 1u) * kCochePaso - 0x98u << std::dec << std::endl;
                if (g_marchasDiag >= 2)
                    g_marchasVigilar.store(m.tabla + (kCoches - 1u) * kCochePaso - 0x98u);
            }
            return;
        }
        ++m.paso;
        for (uint32_t i = 0; i < kCoches; ++i)
        {
            const uint32_t coche = m.tabla + i * kCochePaso;
            const int32_t marcha = static_cast<int32_t>(leer32(rdram, coche - 0x98u));
            if (m.hay && marcha != m.marcha[i])
            {
                ++m.cambios;
                const bool salto = std::abs(marcha - m.marcha[i]) > 1;
                m.saltos += salto ? 1u : 0u;
                if (i == kCoches - 1u)
                {
                    m.saltosJugador += salto ? 1u : 0u;
                    const float vx = leerFloat(rdram, coche + 0x20u), vy = leerFloat(rdram, coche + 0x24u),
                                vz = leerFloat(rdram, coche + 0x28u);
                    std::cout << "[f1-06:marchas] t=" << t << " paso " << m.paso << " (n " << m.nFotograma << ") marcha "
                              << m.marcha[i] << " -> " << marcha << (salto ? " SALTO" : "") << " tras "
                              << m.paso - m.pasoCambio << " pasos, " << std::sqrt(vx * vx + vy * vy + vz * vz) * 2.23694f
                              << " mph" << std::endl;
                    m.pasoCambio = m.paso;
                }
            }
            m.marcha[i] = marcha;
        }
        m.hay = true;
        // Entradas del cambio de la fisica del jugador: + 0xF04 (bits de los mandos que lee func_1867E0), + 0xF28
        // (cambios pendientes), + 0xF2C (fase del embrague), + 0x10C8, + 0x93A; se anotan cuando cambian.
        if (const uint32_t fisica = fisicaJugador(rdram); punteroEe(fisica))
        {
            static uint32_t s_antes[5] = {~0u, ~0u, ~0u, ~0u, ~0u};
            int8_t modo = 0;
            std::memcpy(&modo, rdram + ((fisica + 0x93Au) & PS2_RAM_MASK), 1);
            const uint32_t ahora[5] = {leer32(rdram, fisica + 0xF04u), leer32(rdram, fisica + 0xF28u),
                                       leer32(rdram, fisica + 0xF2Cu), leer32(rdram, fisica + 0x10C8u),
                                       static_cast<uint32_t>(modo)};
            static uint32_t s_informes = 0u;
            if (std::memcmp(ahora, s_antes, sizeof(ahora)) != 0 && s_informes < 400u)
            {
                ++s_informes;
                int8_t marcha = 0;
                std::memcpy(&marcha, rdram + ((fisica + 0x934u) & PS2_RAM_MASK), 1);
                std::cout << "[f1-06:marchas] t=" << t << " paso " << m.paso << " cambio: entradas 0x" << std::hex
                          << ahora[0] << " pendientes " << std::dec << static_cast<int32_t>(ahora[1]) << " fase " << ahora[2]
                          << " 10c8 0x" << std::hex << ahora[3] << std::dec << " modo " << int(modo) << " marcha "
                          << int(marcha) << " rpm " << leerFloat(rdram, fisica + 0x8CCu) << " embrague "
                          << leerFloat(rdram, fisica + 0xF0Cu) << std::endl;
            }
            std::memcpy(s_antes, ahora, sizeof(ahora));
        }
        // F1_MARCHAS_CAPTURAS=<prefijo>: a los 20 pasos de cada marcha del jugador (de 1 a 7) pide una captura de la
        // ventana (PS2X_CAPTURA_PETICION, como medir-carrera.ps1) en <prefijo>-paso<p>-marcha<m>.png, hasta 60.
        static const char *s_capturas = std::getenv("F1_MARCHAS_CAPTURAS");
        static const char *s_peticion = std::getenv("PS2X_CAPTURA_PETICION");
        static uint32_t s_pedidas = 0u;
        const int32_t marchaJugador = m.marcha[kCoches - 1u];
        if (s_capturas != nullptr && s_peticion != nullptr && s_pedidas < 60u && m.paso - m.pasoCambio == 20u &&
            marchaJugador >= 1 && marchaJugador <= 7 && !std::filesystem::exists(s_peticion))
        {
            const std::string ruta = std::string(s_capturas) + "-paso" + std::to_string(m.paso) + "-marcha" +
                                     std::to_string(marchaJugador) + ".png";
            std::ofstream(s_peticion, std::ios::binary) << ruta;
            ++s_pedidas;
            std::cout << "[f1-06:marchas] t=" << t << " paso " << m.paso << " captura con la marcha " << marchaJugador
                      << ": " << ruta << std::endl;
        }
        if (t - m.informe >= 30.0)
        {
            m.informe = t;
            std::cout << "[f1-06:marchas] t=" << t << " " << m.paso << " pasos: " << m.cambios << " cambios, " << m.saltos
                      << " saltos de mas de una marcha (" << m.saltosJugador << " del jugador)" << std::endl;
            if (const uint32_t fisica = fisicaJugador(rdram); punteroEe(fisica))
                informeAveria(rdram, "estado", fisica, 0u, 0u);
        }
    }

    // Averias (B1): func_1854A0(fisica) rompe una marcha (+ 0xF50, que el cambio se salta; + 0x914 bit 0x800) y
    // func_1856F0(fisica, bits) aplica averias por bits. Se anotan con el coche (la fisica del jugador es
    // *(*(posicion - 0x160 + 0xC) + 0x1C)).
    constexpr uint32_t kRomperMarcha = 0x001854A0u;
    constexpr uint32_t kAplicarAverias = 0x001856F0u;
    PS2Runtime::RecompiledFunction g_romperMarchaOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_aplicarAveriasOriginal = nullptr;

    uint32_t fisicaJugador(const uint8_t *rdram)
    {
        if (g_marchas.tabla == 0u)
            return 0u;
        const uint32_t coche = g_marchas.tabla + (kCoches - 1u) * kCochePaso - 0x160u;
        const uint32_t a = leer32(rdram, coche + 0xCu);
        return punteroEe(a) ? leer32(rdram, a + 0x1Cu) : 0u;
    }

    void informeAveria(const uint8_t *rdram, const char *que, uint32_t fisica, uint32_t ra, uint32_t bits)
    {
        int8_t marcha = 0;
        std::memcpy(&marcha, rdram + ((fisica + 0x934u) & PS2_RAM_MASK), 1);
        std::cout << "[f1-06:marchas] t=" << segundosDesdeArranque() << " " << que << " fisica 0x" << std::hex << fisica
                  << (fisica == fisicaJugador(rdram) ? " (jugador)" : "") << " ra 0x" << ra << " bits 0x" << bits
                  << " averias 0x" << leer32(rdram, fisica + 0x914u) << " permitidas 0x" << leer32(rdram, fisica + 0x10CCu)
                  << std::dec << " marcha " << int(marcha) << " rota " << static_cast<int32_t>(leer32(rdram, fisica + 0xF50u))
                  << " ultima " << static_cast<int32_t>(leer32(rdram, fisica + 0x1018u)) << " desgaste "
                  << leerFloat(rdram, fisica + 0xF40u) << " umbral caja " << leerFloat(rdram, fisica + 0xF54u)
                  << " umbral motor " << leerFloat(rdram, fisica + 0xF58u) << " escala " << leerFloat(rdram, fisica + 0xF44u)
                  << " carga " << leerFloat(rdram, fisica + 0x360u) << " fiabilidad caja "
                  << leerFloat(rdram, fisica + 0x1020u) << std::endl;
    }

    // Umbrales de las averias (T1): func_184F38(fisica, mascara, escala) prepara el coche (+ 0x10CC, + 0xF44, umbrales
    // infinitos), func_184E00(fisica, bits, escala, base) pone el umbral de cada bit (0x800 la caja, + 0xF54),
    // func_185010(fisica, bits) repara esos bits y vuelve a poner sus umbrales (sin mirar + 0x10CC) y func_1850E0 lo
    // repara todo. Se anotan con ra (las 400 primeras).
    constexpr uint32_t kPrepararAverias = 0x00184F38u, kUmbralesAverias = 0x00184E00u, kRepararAverias = 0x00185010u,
                       kRepararTodo = 0x001850E0u;
    PS2Runtime::RecompiledFunction g_prepararAveriasOriginal = nullptr, g_umbralesAveriasOriginal = nullptr,
                                   g_repararAveriasOriginal = nullptr, g_repararTodoOriginal = nullptr;
    std::atomic<uint32_t> g_informesUmbrales{0u};

    void informeUmbrales(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t direccion, const char *que,
                         PS2Runtime::RecompiledFunction original)
    {
        const bool entrada = ctx->pc == direccion;
        const uint32_t fisica = GPR_U32(ctx, 4), bits = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
        const float f12 = ctx->f[12], f13 = ctx->f[13];
        original(rdram, ctx, runtime);
        if (!entrada || g_informesUmbrales.fetch_add(1u) >= 400u)
            return;
        std::cout << "[f1-06:umbrales] t=" << segundosDesdeArranque() << " " << que << " fisica 0x" << std::hex << fisica
                  << " ra 0x" << ra << " bits 0x" << bits << std::dec << " f12 " << f12 << " f13 " << f13 << " -> umbral caja "
                  << leerFloat(rdram, fisica + 0xF54u) << " motor " << leerFloat(rdram, fisica + 0xF58u) << " desgaste "
                  << leerFloat(rdram, fisica + 0xF40u) << " escala " << leerFloat(rdram, fisica + 0xF44u) << " permitidas 0x"
                  << std::hex << leer32(rdram, fisica + 0x10CCu) << std::dec << " fiabilidad caja "
                  << leerFloat(rdram, fisica + 0x1020u) << std::endl;
    }

    void prepararAverias(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        informeUmbrales(rdram, ctx, runtime, kPrepararAverias, "preparar", g_prepararAveriasOriginal);
    }

    void umbralesAverias(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        informeUmbrales(rdram, ctx, runtime, kUmbralesAverias, "umbrales", g_umbralesAveriasOriginal);
    }

    void repararAverias(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        informeUmbrales(rdram, ctx, runtime, kRepararAverias, "reparar", g_repararAveriasOriginal);
    }

    void repararTodo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        informeUmbrales(rdram, ctx, runtime, kRepararTodo, "reparar todo", g_repararTodoOriginal);
    }

    void romperMarcha(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == kRomperMarcha;
        const uint32_t fisica = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
        g_romperMarchaOriginal(rdram, ctx, runtime);
        if (entrada)
            informeAveria(rdram, "marcha rota", fisica, ra, 0u);
    }

    void aplicarAverias(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == kAplicarAverias;
        const uint32_t fisica = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31), bits = GPR_U32(ctx, 5);
        g_aplicarAveriasOriginal(rdram, ctx, runtime);
        if (entrada)
            informeAveria(rdram, "averias aplicadas", fisica, ra, bits);
    }

    // T1 (parche 0173): rand() del juego (0x2E00B8). El traductor lo ata a la funcion de biblioteca del runtime
    // (std::rand() del PC & 0x7FFF, sin la semilla del juego); aqui vuelve a ser el del juego (ps2x::gameRand: la
    // congruencia de la biblioteca de C sobre _rand_next, 31 bits). Con 15 bits los umbrales de las averias
    // (func_184D80) caian siempre al principio de su intervalo: la caja se rompia a los ~30 s de cada carrera y el cambio
    // automatico se saltaba la marcha rota (3 -> 5). F1_AZAR_ANFITRION=1 vuelve al de la biblioteca del runtime.
    constexpr uint32_t kRandJuego = 0x002E00B8u;
    PS2Runtime::RecompiledFunction g_randJuegoOriginal = nullptr;

    void randJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int32_t valor = 0;
        if (!ps2x::gameRand(rdram, PS2_RAM_SIZE, valor))
        {
            g_randJuegoOriginal(rdram, ctx, runtime);
            return;
        }
        SET_GPR_S32(ctx, 2, valor);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // T2 del volante nativo (parche 0174, odd/tasks/volante-nativo.md): la biblioteca Logitech del juego (liblgdev 1.11,
    // 0x2F6950-0x2F77F8 y lgDevASync 0x2F7880) se sustituye por el anfitrion: el juego ve un Driving Force Pro
    // (ps2x::lgdev::HostLibrary) alimentado desde el volante del PC (DirectInput, ps2x::readNativeWheel con la seccion
    // [volante] de controles.ini). Contrato en analisis/volante-liblgdev.md. Mientras tanto el volante no mueve el
    // DualShock 2 del jugador 1. F1_VOLANTE_STICK=1: como antes (la biblioteca del juego y el volante como stick).
    // F1_VOLANTE_FALSO=<direccion>,<acelerador>,<freno> (pruebas, sin hardware, tambien con PS2X_SIN_MANDOS=1): un
    // volante fijo con esos ejes (direccion -1..1, pedales 0..1); sus botones y su cruceta son los del mando del puerto 0
    // (los guiones F1_GUION/F1_PULSAR). F1_VOLANTE_FALSO=mando: todo del mando del puerto 0 (stick izquierdo = direccion,
    // presion de cruz = acelerador, de cuadrado = freno), para que F1_PILOTO conduzca con el volante del juego.
    // T4 (parche 0175): la fuerza de respuesta del juego (Download/Update/Start/Stop/Destroy y las propiedades 2, muelle
    // de centrado propio del volante, y 3, ganancia) se guarda en g_biblioteca y se publica en ps2x::lgdev::forceBoard;
    // el hilo que lee el volante la aplica por DirectInput (ps2_pad_win.cpp). F1_VOLANTE_DIAG=1: cada orden de fuerza
    // en el log, con el efecto descifrado (tambien sin volante). F1_VOLANTE_INVERTIR_FUERZA=1: las fuerzas al reves.
    // T6 (parche 0176): el giro como el Driving Force Pro del original, giro = 200 grados de tope a tope sobre los
    // grados_volante (900) del volante en G HUB, en [volante] de controles.ini (ps2x::lgdev::wheelInputFrom); el muelle
    // y el amortiguador del juego en la misma escala (g_escala). F1_VOLANTE_FALSO no pasa por esa escala: sus valores ya
    // son fracciones del giro del juego.
    namespace volante
    {
        std::mutex g_mutex;
        ps2x::lgdev::HostLibrary g_biblioteca;
        bool g_falso = false;      // F1_VOLANTE_FALSO con tres numeros
        bool g_falsoMando = false; // F1_VOLANTE_FALSO=mando
        bool g_diagnostico = false; // F1_VOLANTE_DIAG=1
        bool g_invertir = false;    // F1_VOLANTE_INVERTIR_FUERZA=1
        double g_escala = 1.0;      // giro / grados_volante, fijado al abrir el volante
        struct UltimaActualizacion
        {
            std::string texto;
            double segundo = -1.0;
            uint32_t calladas = 0u;
        };
        std::map<uint32_t, UltimaActualizacion> g_actualizaciones; // F1_VOLANTE_DIAG: lo ultimo escrito por efecto
        ps2x::lgdev::WheelInput g_entradaFalsa;
        ps2x::lgdev::WheelInput g_espejo; // el mando del puerto 0 como volante (g_mutex)

        bool leer(ps2x::lgdev::WheelInput &entrada)
        {
            if (g_falso || g_falsoMando)
            {
                std::lock_guard<std::mutex> cerrojo(g_mutex);
                entrada = g_falsoMando ? g_espejo : g_entradaFalsa;
                if (g_falso)
                {
                    entrada.buttons = g_espejo.buttons;
                    entrada.pov = g_espejo.pov;
                }
                return true;
            }
            return ps2x::readNativeWheel(entrada);
        }

        // Desde scePadRead (puerto 0), con el informe ya tocado por los guiones y el piloto.
        void espejo(const uint8_t *rdram, uint32_t datos)
        {
            if (!g_falso && !g_falsoMando)
                return;
            uint8_t informe[21];
            for (uint32_t i = 0; i < sizeof(informe); ++i)
                informe[i] = rdram[(datos + i) & PS2_RAM_MASK];
            const ps2x::lgdev::WheelInput entrada = ps2x::lgdev::wheelInputFromPadReport(informe);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            g_espejo = entrada;
        }

        void volver(R5900Context *ctx, uint32_t valor)
        {
            SET_GPR_S32(ctx, 2, static_cast<int32_t>(valor));
            ctx->pc = GPR_U32(ctx, 31);
        }

        uint8_t *en(uint8_t *rdram, uint32_t direccion)
        {
            return rdram + (direccion & PS2_RAM_MASK);
        }

        void escribir32(uint8_t *rdram, uint32_t direccion, uint32_t valor)
        {
            std::memcpy(en(rdram, direccion), &valor, sizeof(valor));
        }

        // lgDevInit (0x2F6950) y lgDevASync (0x2F7880): 0.
        void sinNada(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            volver(ctx, 0u);
        }

        // Con g_mutex: el estado de la fuerza para el hilo del volante.
        void publicarFuerza()
        {
            ps2x::lgdev::forceBoard().publish(g_biblioteca.forceState(g_invertir, g_escala));
        }

        // F1_VOLANTE_DIAG: una orden de fuerza en el log.
        void diagnostico(const char *orden, uint32_t id, int32_t resultado, const std::string &efecto = {})
        {
            if (!g_diagnostico)
                return;
            std::fprintf(stderr, "[volante] t=%.2f %s id=0x%x%s%s%s\n", segundosDesdeArranque(), orden,
                         static_cast<unsigned>(id), efecto.empty() ? "" : " ", efecto.c_str(),
                         resultado == 0 ? "" : " (error)");
        }

        void leerEfecto(const uint8_t *rdram, uint32_t direccion, uint8_t *efecto)
        {
            for (uint32_t i = 0; i < ps2x::lgdev::kEffectSize; ++i)
                efecto[i] = rdram[(direccion + i) & PS2_RAM_MASK];
        }

        void leerIds(const uint8_t *rdram, uint32_t direccion, uint32_t cuantos, std::vector<uint32_t> &ids)
        {
            ids.clear();
            for (uint32_t i = 0; i < cuantos && i < 64u; ++i)
                ids.push_back(leer32(rdram, direccion + i * 4u));
        }

        // EnumHint(*aviso) (0x2F6CE0): distinto de 0 al empezar y cuando el volante llega o se va.
        void enumHint(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t aviso = GPR_U32(ctx, 4);
            if (aviso == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            ps2x::lgdev::WheelInput entrada;
            const bool hay = leer(entrada);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            escribir32(rdram, aviso, g_biblioteca.enumHint(hay));
            volver(ctx, 0u);
        }

        // Enumerate(indice, desc) (0x2F6D38).
        void enumerar(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t indice = GPR_U32(ctx, 4), desc = GPR_U32(ctx, 5);
            if (desc == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            ps2x::lgdev::WheelInput entrada;
            const bool hay = leer(entrada);
            uint8_t datos[ps2x::lgdev::kDescriptorSize];
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            const int32_t r = g_biblioteca.enumerate(indice, hay, datos);
            if (r == 0)
                std::memcpy(en(rdram, desc), datos, sizeof(datos));
            volver(ctx, static_cast<uint32_t>(r));
        }

        // Open(indice, *handle) (0x2F6E20).
        void abrir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t indice = GPR_U32(ctx, 4), destino = GPR_U32(ctx, 5);
            if (destino == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            ps2x::lgdev::WheelInput entrada;
            const bool hay = leer(entrada);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            int32_t handle = -1;
            const int32_t r = g_biblioteca.open(indice, hay, handle);
            escribir32(rdram, destino, static_cast<uint32_t>(handle));
            if (r == 0)
            {
                std::fprintf(stderr, "[pad] el juego abre el volante nativo (Driving Force Pro, handle 0x%x)\n",
                             static_cast<unsigned>(handle));
                const ps2x::lgdev::WheelMap &mapa = ps2x::nativeWheelMap();
                g_escala = ps2x::lgdev::steeringScale(mapa);
                std::fprintf(stderr, "[pad] volante nativo: %s%s\n", ps2x::lgdev::describeSteering(mapa).c_str(),
                             g_falso || g_falsoMando ? " (F1_VOLANTE_FALSO: la direccion falsa no se escala)" : "");
                publicarFuerza();
            }
            volver(ctx, static_cast<uint32_t>(r));
        }

        // Close(handle) (0x2F6EE0).
        void cerrar(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            const int32_t handle = static_cast<int32_t>(GPR_U32(ctx, 4));
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            const int32_t r = g_biblioteca.close(handle);
            if (r == 0)
            {
                std::fprintf(stderr, "[pad] el juego cierra el volante nativo (handle 0x%x)\n",
                             static_cast<unsigned>(handle));
                publicarFuerza(); // se paran y se quitan todos sus efectos
                g_actualizaciones.clear();
            }
            volver(ctx, static_cast<uint32_t>(r));
        }

        // GetDeviceProperty(handle, prop, *valor) (0x2F6F68).
        void leerPropiedad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const int32_t handle = static_cast<int32_t>(GPR_U32(ctx, 4));
            const uint32_t propiedad = GPR_U32(ctx, 5), destino = GPR_U32(ctx, 6);
            if (destino == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            uint32_t valor = 0u;
            const int32_t r = g_biblioteca.getProperty(handle, propiedad, valor);
            if (r == 0)
                escribir32(rdram, destino, valor);
            volver(ctx, static_cast<uint32_t>(r));
        }

        // SetDeviceProperty(handle, prop, valor) (0x2F7038).
        void cambiarPropiedad(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            const int32_t handle = static_cast<int32_t>(GPR_U32(ctx, 4));
            const uint32_t propiedad = GPR_U32(ctx, 5), valor = GPR_U32(ctx, 6);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            const int32_t r = g_biblioteca.setProperty(handle, propiedad, valor);
            if (r == 0 && (propiedad == 2u || propiedad == 3u))
            {
                publicarFuerza();
                if (g_diagnostico)
                    std::fprintf(stderr, "[volante] t=%.2f propiedad %u = %u (%s)\n", segundosDesdeArranque(),
                                 propiedad, valor, propiedad == 2u ? "centrado propio del volante" : "ganancia");
            }
            volver(ctx, static_cast<uint32_t>(r));
        }

        // ReadMulti(n, handles[], modos[], posiciones) (0x2F70E0): la posicion actual de cada dispositivo, estado 0.
        void leerVarios(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t n = GPR_U32(ctx, 4), handles = GPR_U32(ctx, 5), modos = GPR_U32(ctx, 6),
                           posiciones = GPR_U32(ctx, 7);
            if (n < 1u || n > 4u || handles == 0u || modos == 0u || posiciones == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            ps2x::lgdev::WheelInput entrada;
            const bool hay = leer(entrada);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            int32_t resultado = 0;
            for (uint32_t i = 0; i < n; ++i)
            {
                const int32_t handle = static_cast<int32_t>(leer32(rdram, handles + i * 4u));
                uint8_t posicion[ps2x::lgdev::kPositionSize];
                const int32_t r = g_biblioteca.readPosition(handle, hay ? &entrada : nullptr, posicion);
                std::memcpy(en(rdram, posiciones + i * static_cast<uint32_t>(sizeof(posicion))), posicion,
                            sizeof(posicion));
                if (r < 0)
                    resultado = r;
            }
            volver(ctx, static_cast<uint32_t>(resultado));
        }

        // DownloadForceEffect(handle, *id, efecto) (0x2F72A8): el efecto queda cargado, sin sonar.
        void descargarEfecto(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const int32_t handle = static_cast<int32_t>(GPR_U32(ctx, 4));
            const uint32_t destino = GPR_U32(ctx, 5), origen = GPR_U32(ctx, 6);
            if (destino == 0u || origen == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            uint8_t efecto[ps2x::lgdev::kEffectSize];
            leerEfecto(rdram, origen, efecto);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            uint32_t id = 0u;
            const int32_t r = g_biblioteca.downloadEffect(handle, id, efecto);
            if (r == 0)
            {
                escribir32(rdram, destino, id);
                publicarFuerza();
            }
            diagnostico("carga", id, r, ps2x::lgdev::describeEffect(efecto));
            volver(ctx, static_cast<uint32_t>(r));
        }

        // UpdateForceEffect(id, efecto) (0x2F7408): el juego lo llama en cada fotograma de carrera; el diagnostico solo
        // escribe los cambios, como mucho uno por segundo y efecto.
        void actualizarEfecto(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t id = GPR_U32(ctx, 4), origen = GPR_U32(ctx, 5);
            if (origen == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            uint8_t efecto[ps2x::lgdev::kEffectSize];
            leerEfecto(rdram, origen, efecto);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            const int32_t r = g_biblioteca.updateEffect(id, efecto);
            if (r == 0)
                publicarFuerza();
            if (g_diagnostico)
            {
                UltimaActualizacion &u = g_actualizaciones[id];
                const std::string texto = ps2x::lgdev::describeEffect(efecto);
                const double ahora = segundosDesdeArranque();
                if (texto != u.texto && ahora - u.segundo >= 1.0)
                {
                    const std::string calladas =
                        u.calladas > 0u ? " (+" + std::to_string(u.calladas) + " sin escribir)" : std::string();
                    diagnostico("cambia", id, r, texto + calladas);
                    u.texto = texto;
                    u.segundo = ahora;
                    u.calladas = 0u;
                }
                else
                    ++u.calladas;
            }
            volver(ctx, static_cast<uint32_t>(r));
        }

        // StartForceEffectMulti(n, ids) (0x2F7550).
        void arrancarEfectos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t cuantos = GPR_U32(ctx, 4), ids = GPR_U32(ctx, 5);
            if (ids == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            std::vector<uint32_t> lista;
            leerIds(rdram, ids, cuantos, lista);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            const int32_t r = g_biblioteca.startEffects(lista.data(), static_cast<uint32_t>(lista.size()));
            publicarFuerza();
            for (uint32_t id : lista)
                diagnostico("arranca", id, r);
            volver(ctx, static_cast<uint32_t>(r));
        }

        // StopForceEffect(id) (0x2F7660).
        void pararEfecto(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t id = GPR_U32(ctx, 4);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            const int32_t r = g_biblioteca.stopEffects(&id, 1u);
            publicarFuerza();
            diagnostico("para", id, r);
            volver(ctx, static_cast<uint32_t>(r));
        }

        // StopForceEffectMulti(n, ids) (0x2F76E8).
        void pararEfectos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t cuantos = GPR_U32(ctx, 4), ids = GPR_U32(ctx, 5);
            if (ids == 0u)
                return volver(ctx, ps2x::lgdev::kErrInvalid);
            std::vector<uint32_t> lista;
            leerIds(rdram, ids, cuantos, lista);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            const int32_t r = g_biblioteca.stopEffects(lista.data(), static_cast<uint32_t>(lista.size()));
            publicarFuerza();
            for (uint32_t id : lista)
                diagnostico("para", id, r);
            volver(ctx, static_cast<uint32_t>(r));
        }

        // DestroyForceEffect(id) (0x2F77F8).
        void destruirEfecto(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            const uint32_t id = GPR_U32(ctx, 4);
            std::lock_guard<std::mutex> cerrojo(g_mutex);
            const int32_t r = g_biblioteca.destroyEffect(id);
            publicarFuerza();
            g_actualizaciones.erase(id);
            diagnostico("quita", id, r);
            volver(ctx, static_cast<uint32_t>(r));
        }

        void instalar(PS2Runtime &runtime)
        {
            if (const char *valor = std::getenv("F1_VOLANTE_STICK"); valor != nullptr && std::atoi(valor) != 0)
            {
                std::fprintf(stderr, "[pad] volante como stick (F1_VOLANTE_STICK)\n");
                return;
            }
            const char *falso = std::getenv("F1_VOLANTE_FALSO");
            g_falsoMando = falso != nullptr && std::strcmp(falso, "mando") == 0;
            const char *diag = std::getenv("F1_VOLANTE_DIAG");
            g_diagnostico = diag != nullptr && std::atoi(diag) != 0;
            const char *invertir = std::getenv("F1_VOLANTE_INVERTIR_FUERZA");
            g_invertir = invertir != nullptr && std::atoi(invertir) != 0;
            g_falso = !g_falsoMando && ps2x::lgdev::parseFakeWheel(falso, g_entradaFalsa);
            const struct
            {
                uint32_t direccion;
                PS2Runtime::RecompiledFunction funcion;
            } sustituciones[] = {
                {0x002F6950u, sinNada},          // lgDevInit
                {0x002F6CE0u, enumHint},         // lgDevEnumHint
                {0x002F6D38u, enumerar},         // lgDevEnumerate
                {0x002F6E20u, abrir},            // lgDevOpen
                {0x002F6EE0u, cerrar},           // lgDevClose
                {0x002F6F68u, leerPropiedad},    // lgDevGetDeviceProperty
                {0x002F7038u, cambiarPropiedad}, // lgDevSetDeviceProperty
                {0x002F70E0u, leerVarios},       // lgDevReadMulti
                {0x002F72A8u, descargarEfecto},  // lgDevDownloadForceEffect
                {0x002F7408u, actualizarEfecto}, // lgDevUpdateForceEffect
                {0x002F7550u, arrancarEfectos},  // lgDevStartForceEffectMulti
                {0x002F7660u, pararEfecto},      // lgDevStopForceEffect
                {0x002F76E8u, pararEfectos},     // lgDevStopForceEffectMulti
                {0x002F77F8u, destruirEfecto},   // lgDevDestroyForceEffect
                {0x002F7880u, sinNada},          // lgDevASync
            };
            bool todas = true;
            for (const auto &s : sustituciones)
                todas = runtime.replaceFunction(s.direccion, s.funcion) && todas;
            if (!todas)
                std::fprintf(stderr, "[pad] volante nativo: no se pudo sustituir alguna funcion de liblgdev\n");
            ps2x::lgdev::setNativeWheelEnabled(true);
            if (g_diagnostico)
                std::fprintf(stderr, "[volante] F1_VOLANTE_DIAG: se escriben las ordenes de fuerza del juego%s\n",
                             g_invertir ? " (F1_VOLANTE_INVERTIR_FUERZA)" : "");
            if (g_falso || g_falsoMando)
                std::fprintf(stderr, "[pad] volante nativo del juego: Driving Force Pro (falso: F1_VOLANTE_FALSO=%s)\n",
                             falso);
            else if (falso != nullptr && falso[0] != '\0')
                std::fprintf(stderr, "[pad] F1_VOLANTE_FALSO no valido ('%s'): <direccion>,<acelerador>,<freno> o mando\n",
                             falso);
        }
    }

    void arrancarVigiaMarchas()
    {
        std::thread([]
                    {
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                const uint32_t a = g_marchasVigilar.exchange(0u);
                if (a == 0u || !f1vigia::listo() || g_vigiaRdram == nullptr)
                {
                    if (a != 0u)
                        g_marchasVigilar.store(a);
                    continue;
                }
                g_vigiaAvisos.store(0u, std::memory_order_relaxed);
                g_vigiaEntrada = a;
                const bool ok = f1vigia::vigilar(reinterpret_cast<uintptr_t>(g_vigiaRdram + a), avisoVigia);
                std::fprintf(stderr, "[vigia] marcha del jugador en 0x%08x: vigilada %s t=%.1f\n", a, ok ? "si" : "NO",
                             segundosDesdeArranque());
            } })
            .detach();
    }

    // Foto y repeticion T5: el coche y la camara de la carrera en el modo foto. En cada fotograma de dibujo (tambien con la
    // carrera congelada) func_1F23F0 llama a func_22CB90(vistas), que pone al dia la vista de cada jugador: vistas + 0x14
    // el modo (2 en carrera y en la repeticion); la entrada vistas[0] + vistas[0x10] * vistas[0xC], con + 0x4C las vistas
    // y + 0x44 su tabla; el jugador 1 es *(*(*0x3FE5BC + 0x34)). La vista recibe en + 0xC el coche del jugador (+ 0x14) y
    // su camara es *(jugador[0x94] + jugador[0x90] * jugador[0xA0]), de jugador[0x98] (5): la 0 la de television (vtable
    // + 0x24 = 0x41EA38, la de la repeticion), la 2 la de conduccion (0x416D28) y la 4 la de la salida (0x41A840). La
    // camara se pone al dia con su metodo + 0x14(vista), y el metodo 0x1169E0 la pasa a func_2C5948 (camaraModoFoto). El
    // coche es una ranura de 0x5C bytes de una tabla seguida (22 en carrera): + 0x28 su vtable, + 0x2C "carNN", + 0x54
    // (byte) su indice, + 0x24 el coche (+ 0x5E, byte: su puesto en carrera, 0 el primero), + 0x4C el piloto (+ 0xC0 sus
    // textos, + 0xE8 donde empieza el apellido) y + 0x58 sus configuraciones de camara. La repeticion cambia de coche
    // escribiendo otra ranura en jugador + 0x14 (su cruceta; logs/fyr-t5). La camara de conduccion tiene en + 0x3C0 su
    // modo: + 0x80 el indice de la camara (el triangulo de la carrera lo sube y da la vuelta: 2, 3, 4, 0, 1), + 0x114 y
    // + 0x118 la lista de las camaras del coche y + 0x14 la ranura para la que se preparo; cada fotograma func_1FB470 la
    // compara con la de la vista y, si es otra, la prepara otra vez con func_1FB938(modo, + 0x80).
    // En el modo foto el coche y la camara elegidos (0 o 2) se escriben en el jugador, como hace la repeticion con su coche
    // (la carrera congelada no los ve); otra camara de conduccion se elige escribiendo + 0x80 y + 0x14 = 0 en su modo, y
    // el propio juego la prepara en ese fotograma. Al salir, antes del primer paso de la carrera (o en el dibujo, si llega
    // antes), vuelven el coche y la camara del jugador y + 0x80 (con + 0x14 = 0); la vista vuelve sola a su coche. (Poner
    // el coche solo durante func_22CB90 no vale: el codigo traducido puede dejarla a medias y seguirla sin el chivato.)
    // [f1-06:modo-foto] con cada cambio.
    // Camaras y pilotos T2: las camaras de la repeticion tambien. El constructor del jugador (func_1F7C68) hace la
    // camara 0 con func_2FB540("ReplayDirector", "replaycams"): un contenedor (vtable 0x41EA38) con + 0xC y + 0x10 el
    // vector de sus camaras, + 0x20 la que se ve y + 0x1C activo, y le pone dos (func_2FBBC8): la 0 "PlayerCam"
    // (func_1F9920(jugador, 2, 1), la misma clase que el modo de la de conduccion: vtable 0x416C90 en + 0xC, + 0x80 el
    // indice, + 0x114 y + 0x118 las camaras del coche: rueda, aleron...) y la 1 la de pista (func_202180, las camaras
    // "replaycameras" .bcam del circuito, vtable 0x417640: elige sola la camara de television por donde va el coche). El
    // triangulo lo mira func_1F9078(jugador, pausa) en cada fotograma de dibujo: con jugador + 0x1C, *pausa = 0 y la accion
    // de camara (bit 0x10000 de *(jugador + 0x28) + 0x18C + 4, func_1F4118) llama al metodo + 0x34 (siguiente) de la camara
    // que se ve: la de conduccion pasa a su modo siguiente; el contenedor (0x2FBAB0) pide el siguiente a la suya
    // (PlayerCam 0x2FC078: +1 hasta la ultima, luego no) y si no, pasa a la otra (activar y desactivar no hacen nada,
    // 0x3D70D0/0x3D70D8; PlayerCam empieza en su primera). El tipo de camara del jugador (+ 0x20, func_1F9480) pone + 0x90:
    // 0 en la repeticion, 4 -> 2 en carrera, 3 -> 4 en la salida. Una sola lista para el modo foto y la repeticion
    // (ps2x::RaceCameraSet): los modos de conduccion, los de PlayerCam y la de pista, escritos con los indices del juego
    // (+ 0x90, + 0x20 del contenedor y + 0x80 con + 0x14 = 0 en el objeto de modos, que el juego prepara solo). En la
    // repeticion el triangulo del juego (su accion, se pulse con lo que se pulse) se queda para el port y recorre esa
    // lista (controlesJugador). [f1-06:camaras] con cada cambio.
    // F1_CAMARA_DIAG=1: [f1-06:camaras] cada segundo y cuando cambian el modo, la camara, el coche o su camara.
    constexpr uint32_t kVistasCarrera = 0x0022CB90u;
    constexpr uint32_t kGlobalCarrera = 0x003FE5BCu;
    constexpr uint32_t kCamaraTelevisionVtabla = 0x0041EA38u;
    constexpr uint32_t kCamaraConduccionVtabla = 0x00416D28u;
    constexpr uint32_t kCamaraModosVtabla = 0x00416C90u; // en + 0xC: PlayerCam y el modo de la de conduccion
    constexpr uint32_t kCamaraPistaVtabla = 0x00417640u; // en + 0xC: la de pista de la repeticion
    constexpr uint32_t kCamaraTelevision = 0u, kCamaraConduccion = 2u;
    constexpr uint32_t kDirectorAutomatico = 0x510u, kDirectorQuieto = 0x514u; // del director de la repeticion (T4)
    constexpr uint32_t kRanuraPaso = 0x5Cu;
    constexpr uint32_t kRanurasMaximo = 32u;
    PS2Runtime::RecompiledFunction g_vistasCarreraOriginal = nullptr;

    struct VistaModoFoto // hilo del EE
    {
        bool activa = false;
        uint32_t jugador = 0u;
        uint32_t ranura = 0u;    // el coche elegido
        uint32_t indice = 0u;    // la camara del jugador elegida (kCamaraTelevision o kCamaraConduccion)
        uint32_t ranuraJuego = 0u, indiceJuego = 0u; // los del jugador al entrar
        int camara = -1;         // ps2x::RaceCameraSet (-1: otra del juego, la de la salida)
        uint32_t modo = 0u;      // el modo de la camara de conduccion
        uint32_t modoJuego = 0u; // su + 0x80 al entrar
        bool modoCambiado = false;
        // Camaras y pilotos T2: la de television (el contenedor), la suya al entrar (+ 0x20) y el + 0x80 de PlayerCam
        uint32_t television = 0u, subJuego = 0u, aBordo = 0u, aBordoJuego = 0u;
        int sub = -1; // la camara del contenedor elegida (-1: ninguna)
        bool televisionCambiada = false;
    };
    VistaModoFoto g_vistaModoFoto;

    void escribirPalabra(uint8_t *rdram, uint32_t direccion, uint32_t valor)
    {
        std::memcpy(rdram + (direccion & PS2_RAM_MASK), &valor, sizeof(valor));
    }

    uint8_t leerByte(const uint8_t *rdram, uint32_t direccion)
    {
        return rdram[direccion & PS2_RAM_MASK];
    }

    uint32_t jugadorCarrera(const uint8_t *rdram)
    {
        const uint32_t global = leer32(rdram, kGlobalCarrera);
        if (!punteroEe(global) || !punteroEe(leer32(rdram, global + 0x34u)))
            return 0u;
        const uint32_t jugador = leer32(rdram, leer32(rdram, global + 0x34u));
        return punteroEe(jugador) ? jugador : 0u;
    }

    struct CamarasJugador
    {
        uint32_t television = 0u, conduccion = 0u, modo = 0u;
        int modos = 0; // las camaras de conduccion del coche
        // Camaras y pilotos T2: las del contenedor de television: PlayerCam (sus modos) y la de pista, por su lugar
        uint32_t aBordo = 0u;
        int aBordoModos = 0; // 1 mientras no se ha preparado nunca (su lista vacia: el juego la llena al verla)
        int subABordo = -1, subPista = -1;
        ps2x::RaceCameraSet lista() const
        {
            return {modos, aBordo != 0u ? aBordoModos : 0, subPista >= 0};
        }
    };

    // Las camaras de un objeto de modos (el de la de conduccion o PlayerCam), si su lista y su indice valen; si no, 0.
    int modosCamara(const uint8_t *rdram, uint32_t modo)
    {
        if (!punteroEe(modo))
            return 0;
        const uint32_t inicio = leer32(rdram, modo + 0x114u), fin = leer32(rdram, modo + 0x118u);
        const uint32_t modos = (fin - inicio) / 4u;
        return punteroEe(inicio) && fin > inicio && modos <= 16u && leer32(rdram, modo + 0x80u) < modos
                   ? static_cast<int>(modos)
                   : 0;
    }

    CamarasJugador camarasJugador(const uint8_t *rdram, uint32_t jugador)
    {
        CamarasJugador c;
        const uint32_t tabla = leer32(rdram, jugador + 0x94u), n = leer32(rdram, jugador + 0x98u);
        if (!punteroEe(tabla) || leer32(rdram, jugador + 0xA0u) != 4u || n <= kCamaraConduccion || n > 16u)
            return c;
        const uint32_t television = leer32(rdram, tabla + 4u * kCamaraTelevision);
        const uint32_t conduccion = leer32(rdram, tabla + 4u * kCamaraConduccion);
        if (punteroEe(television) && leer32(rdram, television + 0x24u) == kCamaraTelevisionVtabla)
            c.television = television;
        if (punteroEe(conduccion) && leer32(rdram, conduccion + 0x24u) == kCamaraConduccionVtabla)
            c.conduccion = conduccion;
        if (c.television != 0u)
        {
            const uint32_t inicio = leer32(rdram, c.television + 0xCu), fin = leer32(rdram, c.television + 0x10u);
            const uint32_t subs = (fin - inicio) / 4u;
            for (uint32_t i = 0; punteroEe(inicio) && fin > inicio && subs <= 8u && i < subs; ++i)
            {
                const uint32_t sub = leer32(rdram, inicio + 4u * i);
                const uint32_t vtabla = punteroEe(sub) ? leer32(rdram, sub + 0xCu) : 0u;
                if (vtabla == kCamaraModosVtabla && c.subABordo < 0)
                {
                    c.subABordo = static_cast<int>(i);
                    c.aBordo = sub;
                    c.aBordoModos = std::max(1, modosCamara(rdram, sub));
                }
                else if (vtabla == kCamaraPistaVtabla && c.subPista < 0)
                    c.subPista = static_cast<int>(i);
            }
        }
        const uint32_t modo = c.conduccion != 0u ? leer32(rdram, c.conduccion + 0x3C0u) : 0u;
        if (const int modos = modosCamara(rdram, modo); modos > 0)
        {
            c.modo = modo;
            c.modos = modos;
        }
        return c;
    }

    // La camara que se ve, en la lista de c (-1: otra del juego, como la de la salida).
    int camaraActual(const uint8_t *rdram, uint32_t jugador, const CamarasJugador &c)
    {
        using K = ps2x::RaceCameraKind;
        const uint32_t indice = leer32(rdram, jugador + 0x90u);
        if (indice == kCamaraConduccion && c.modo != 0u)
            return ps2x::raceCameraIndex(c.lista(), {K::Driving, static_cast<int>(leer32(rdram, c.modo + 0x80u))});
        if (indice != kCamaraTelevision || c.television == 0u)
            return -1;
        const int sub = static_cast<int>(leer32(rdram, c.television + 0x20u));
        if (sub == c.subABordo && c.aBordo != 0u)
            return ps2x::raceCameraIndex(
                c.lista(), {K::Replay, modosCamara(rdram, c.aBordo) > 0 ? static_cast<int>(leer32(rdram, c.aBordo + 0x80u)) : 0});
        return sub == c.subPista ? ps2x::raceCameraIndex(c.lista(), {K::Tv, 0}) : -1;
    }

    // Pone la camara con los indices del juego: + 0x90 del jugador, + 0x20 del contenedor y el + 0x80 del objeto de modos
    // (con + 0x14 = 0, para que el juego la prepare en el fotograma). Devuelve el + 0x90 que le toca.
    uint32_t ponerCamara(uint8_t *rdram, uint32_t jugador, const CamarasJugador &c, const ps2x::RaceCamera &camara)
    {
        using K = ps2x::RaceCameraKind;
        const auto ponerModo = [&](uint32_t modo, int indice, bool siempre)
        {
            if (!siempre && leer32(rdram, modo + 0x80u) == static_cast<uint32_t>(indice))
                return;
            escribirPalabra(rdram, modo + 0x80u, static_cast<uint32_t>(indice));
            escribirPalabra(rdram, modo + 0x14u, 0u); // el juego la prepara otra vez (func_1FB470)
        };
        if (camara.kind == K::Driving)
        {
            if (c.modo != 0u)
                ponerModo(c.modo, camara.mode, false);
            escribirPalabra(rdram, jugador + 0x90u, kCamaraConduccion);
            return kCamaraConduccion;
        }
        const int sub = camara.kind == K::Replay ? c.subABordo : c.subPista;
        const bool otra = leer32(rdram, c.television + 0x20u) != static_cast<uint32_t>(sub);
        escribirPalabra(rdram, c.television + 0x20u, static_cast<uint32_t>(sub));
        if (camara.kind == K::Replay)
            ponerModo(c.aBordo, camara.mode, otra); // como el contenedor, que la empieza al pasar a ella
        escribirPalabra(rdram, jugador + 0x90u, kCamaraTelevision);
        return kCamaraTelevision;
    }

    // Las ranuras de la tabla de coches de la de ranura (las que siguen con su vtable y su indice) y, en coches, cuales
    // valen (con coche) y su puesto en carrera.
    std::vector<uint32_t> ranurasCarrera(const uint8_t *rdram, uint32_t ranura, std::vector<ps2x::PhotoModeCar> &coches)
    {
        std::vector<uint32_t> ranuras;
        coches.clear();
        if (!punteroEe(ranura) || leerByte(rdram, ranura + 0x54u) >= kRanurasMaximo)
            return ranuras;
        const uint32_t vtabla = leer32(rdram, ranura + 0x28u);
        const uint32_t base = ranura - leerByte(rdram, ranura + 0x54u) * kRanuraPaso;
        for (uint32_t k = 0; k < kRanurasMaximo; ++k)
        {
            const uint32_t r = base + k * kRanuraPaso;
            if (leer32(rdram, r + 0x28u) != vtabla || leerByte(rdram, r + 0x54u) != k)
                break;
            const uint32_t coche = leer32(rdram, r + 0x24u);
            ranuras.push_back(r);
            coches.push_back({punteroEe(coche), punteroEe(coche) ? static_cast<int>(leerByte(rdram, coche + 0x5Eu)) : -1});
        }
        for (ps2x::PhotoModeCar &coche : coches)
            if (coche.position >= static_cast<int>(coches.size()))
                coche.position = -1;
        return ranuras;
    }

    // El apellido del piloto de la ranura, como lo guarda el juego (Latin-1); vacio si no se encuentra.
    std::string apellidoPiloto(const uint8_t *rdram, uint32_t ranura)
    {
        const uint32_t piloto = leer32(rdram, ranura + 0x4Cu);
        if (!punteroEe(piloto))
            return {};
        const uint32_t textos = leer32(rdram, piloto + 0xC0u), desde = leer32(rdram, piloto + 0xE8u);
        if (!punteroEe(textos) || desde > 0x200u)
            return {};
        std::string apellido;
        for (uint32_t i = 0; i < 32u; ++i)
        {
            const uint8_t c = leerByte(rdram, textos + desde + i);
            if (c == 0u)
                return apellido;
            if (c < 0x20u)
                return {};
            apellido.push_back(static_cast<char>(c));
        }
        return {};
    }

    // Camaras y pilotos T2: el rotulo del piloto como el de la repeticion. Su HUD (vtable 0x4176D8; func_203F18 cada 30
    // fotogramas, al cambiar la ranura del jugador + 0x14) pone el nombre con func_20F020: el del piloto (ranura + 0x4C)
    // en + 0x48 si + 0x4C no es 0 (un nombre propio) y si no el texto + 0xF4 de sus textos (+ 0xC0) ("Button"); y el
    // equipo con func_20F040: el texto + 0x34 de los textos (+ 0x8) del equipo (ranura + 0x48) ("Honda"). -1: ninguno.
    std::string textoJuego(const uint8_t *rdram, uint32_t direccion)
    {
        if (direccion < 0x00100000u || direccion >= 0x02000000u)
            return {};
        std::string texto;
        for (uint32_t i = 0; i < 48u; ++i)
        {
            const uint8_t c = leerByte(rdram, direccion + i);
            if (c == 0u)
                return texto;
            if (c < 0x20u)
                return {};
            texto.push_back(static_cast<char>(c));
        }
        return {};
    }

    std::string textoDeTabla(const uint8_t *rdram, uint32_t objeto, uint32_t tabla, uint32_t desplazamiento)
    {
        if (!punteroEe(objeto))
            return {};
        const uint32_t textos = leer32(rdram, objeto + tabla), desde = leer32(rdram, objeto + desplazamiento);
        return punteroEe(textos) && desde < 0x10000u ? textoJuego(rdram, textos + desde) : std::string();
    }

    ps2x::PhotoModeCaption rotuloPiloto(const uint8_t *rdram, uint32_t ranura, int puesto)
    {
        ps2x::PhotoModeCaption rotulo;
        rotulo.position = puesto;
        const uint32_t piloto = leer32(rdram, ranura + 0x4Cu);
        if (punteroEe(piloto) && leer32(rdram, piloto + 0x4Cu) != 0u)
            rotulo.name = textoJuego(rdram, leer32(rdram, piloto + 0x48u));
        if (rotulo.name.empty())
            rotulo.name = textoDeTabla(rdram, piloto, 0xC0u, 0xF4u);
        rotulo.team = textoDeTabla(rdram, leer32(rdram, ranura + 0x48u), 0x8u, 0x34u);
        return rotulo;
    }

    // El rotulo del coche de la ranura (con su puesto en carrera) a la ventana.
    void enviarRotulo(const uint8_t *rdram, uint32_t ranura)
    {
        std::vector<ps2x::PhotoModeCar> coches;
        const std::vector<uint32_t> ranuras = ranurasCarrera(rdram, ranura, coches);
        const auto esta = std::find(ranuras.begin(), ranuras.end(), ranura);
        const int puesto = esta == ranuras.end() ? -1 : coches[static_cast<size_t>(esta - ranuras.begin())].position;
        const ps2x::PhotoModeCaption rotulo = rotuloPiloto(rdram, ranura, puesto + 1);
        ps2x::showPhotoModeCaption(rotulo);
        std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " rotulo: " << rotulo.name << " / " << rotulo.team
                  << " / " << rotulo.position << " (apellido " << apellidoPiloto(rdram, ranura) << ")" << std::endl;
    }

    void devolverVistaModoFoto(uint8_t *rdram, uint32_t jugador)
    {
        VistaModoFoto &v = g_vistaModoFoto;
        if (!v.activa)
            return;
        // Solo si es la misma carrera (si ha terminado con el modo foto puesto, sus objetos ya no estan).
        const bool misma = jugador == v.jugador && jugador != 0u;
        if (misma)
        {
            escribirPalabra(rdram, jugador + 0x14u, v.ranuraJuego);
            escribirPalabra(rdram, jugador + 0x90u, v.indiceJuego);
        }
        const CamarasJugador camaras = misma ? camarasJugador(rdram, jugador) : CamarasJugador{};
        if (misma && v.modoCambiado && camaras.modo == v.modo)
        {
            escribirPalabra(rdram, v.modo + 0x80u, v.modoJuego);
            escribirPalabra(rdram, v.modo + 0x14u, 0u);
        }
        // Camaras y pilotos T2: y la del contenedor de television con el modo de PlayerCam.
        if (misma && v.televisionCambiada && camaras.television == v.television)
        {
            escribirPalabra(rdram, v.television + 0x20u, v.subJuego);
            if (camaras.aBordo == v.aBordo && v.aBordo != 0u)
            {
                escribirPalabra(rdram, v.aBordo + 0x80u, v.aBordoJuego);
                escribirPalabra(rdram, v.aBordo + 0x14u, 0u);
            }
        }
        std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque()
                  << (misma ? " vuelven el coche y la camara del juego (coche 0x" : " otra carrera: nada que devolver (coche 0x")
                  << std::hex << v.ranuraJuego << std::dec << ", camara " << v.indiceJuego
                  << (v.modoCambiado ? ", de conduccion " + std::to_string(v.modoJuego) : std::string())
                  << (v.televisionCambiada
                          ? ", de television " + std::to_string(v.subJuego) + " (a bordo " + std::to_string(v.aBordoJuego) + ")"
                          : std::string())
                  << ")" << std::endl;
        v = {};
    }

    void vistasCarreraDiag(const uint8_t *rdram, uint32_t vistas)
    {
        static double registro = 0.0;
        static uint32_t anterior[12] = {};
        const uint32_t entrada = leer32(rdram, vistas) + leer32(rdram, vistas + 0x10u) * leer32(rdram, vistas + 0xCu);
        const uint32_t vista = leer32(rdram, leer32(rdram, entrada + 0x44u));
        const uint32_t jugador = jugadorCarrera(rdram);
        const uint32_t camara = punteroEe(vista) ? leer32(rdram, vista + 0x40u) : 0u;
        const CamarasJugador camaras = jugador != 0u ? camarasJugador(rdram, jugador) : CamarasJugador{};
        // Camaras y pilotos T4: el director de la repeticion (+ 0xC su estado, + 0x508 su coche, + 0x510 automatico,
        // + 0x514 quieto) y el coche del jugador por su numero (+ 0x18) y el pedido (*(+ 0x8C) + 0x250).
        const uint32_t director = jugador != 0u ? leer32(rdram, jugador + 0x2Cu) : 0u;
        const uint32_t pedido = jugador != 0u ? leer32(rdram, jugador + 0x8Cu) : 0u;
        const auto delDirector = [&](uint32_t desplazamiento)
        { return punteroEe(director) ? leer32(rdram, director + desplazamiento) : 0xFFFFFFFFu; };
        const uint32_t ahora[12] = {leer32(rdram, vistas + 0x14u), camara, jugador != 0u ? leer32(rdram, jugador + 0x90u) : 0u,
                                    punteroEe(vista) ? leer32(rdram, vista + 0xCu) : 0u,
                                    camaras.modo != 0u ? leer32(rdram, camaras.modo + 0x80u) : 0u,
                                    camaras.television != 0u ? leer32(rdram, camaras.television + 0x20u) : 0u,
                                    camaras.aBordo != 0u ? leer32(rdram, camaras.aBordo + 0x80u) : 0u,
                                    jugador != 0u ? (leer32(rdram, jugador + 0x18u) & 0xFFFFu) : 0u,
                                    delDirector(0xCu), delDirector(0x508u), delDirector(kDirectorAutomatico),
                                    delDirector(kDirectorQuieto)};
        const double t = segundosDesdeArranque();
        const bool cambio = std::memcmp(ahora, anterior, sizeof(ahora)) != 0;
        if (!cambio && t - registro < 1.0)
            return;
        std::memcpy(anterior, ahora, sizeof(ahora));
        registro = t;
        std::cout << "[f1-06:camaras] t=" << t << (cambio ? " CAMBIO" : "") << std::hex << " vistas 0x" << vistas << " modo "
                  << ahora[0] << " n " << leer32(rdram, entrada + 0x4Cu) << " vista 0x" << vista << " coche 0x" << ahora[3]
                  << " camara 0x" << camara;
        if (punteroEe(camara))
            std::cout << " (vt 0x" << leer32(rdram, camara + 0x24u) << ")";
        if (jugador != 0u)
            std::cout << " jugador 0x" << jugador << " +14 0x" << leer32(rdram, jugador + 0x14u) << " +1c "
                      << leer32(rdram, jugador + 0x1Cu) << " tipo " << leer32(rdram, jugador + 0x20u) << " pedido "
                      << (punteroEe(leer32(rdram, jugador + 0x8Cu)) ? leer32(rdram, leer32(rdram, jugador + 0x8Cu) + 0x240u) : 0u)
                      << " +90 " << ahora[2]
                      << " television 0x" << camaras.television << " conduccion 0x" << camaras.conduccion << " modo 0x"
                      << camaras.modo << " [80] " << ahora[4] << " de " << std::dec << camaras.modos << std::hex
                      << " | contenedor [1c] " << (camaras.television != 0u ? leer32(rdram, camaras.television + 0x1Cu) : 0u)
                      << " [20] " << ahora[5] << " a bordo 0x" << camaras.aBordo << " (" << camaras.subABordo << ") [80] "
                      << ahora[6] << " de " << std::dec << camaras.aBordoModos << " pista " << camaras.subPista
                      << " lista " << camaras.lista().driving << "+" << camaras.lista().replay << "+"
                      << (camaras.lista().tv ? 1 : 0) << " actual " << camaraActual(rdram, jugador, camaras) << std::hex
                      << " | +18 " << ahora[7] << " pedido250 "
                      << (punteroEe(pedido) ? (leer32(rdram, pedido + 0x250u) & 0xFFFFu) : 0xFFFFu) << " director 0x"
                      << director << " [c] " << ahora[8] << " [508] 0x" << ahora[9] << " [510] " << ahora[10]
                      << " [514] " << ahora[11];
        std::cout << std::dec << std::endl;
    }

    void cambiarCocheModoFoto(const uint8_t *rdram, int pasos)
    {
        VistaModoFoto &v = g_vistaModoFoto;
        std::vector<ps2x::PhotoModeCar> coches;
        const std::vector<uint32_t> ranuras = ranurasCarrera(rdram, v.ranura, coches);
        const auto actual = std::find(ranuras.begin(), ranuras.end(), v.ranura);
        const int siguiente = ps2x::stepPhotoModeCar(
            coches, actual == ranuras.end() ? -1 : static_cast<int>(actual - ranuras.begin()), pasos);
        if (siguiente < 0)
        {
            std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " no hay coches que elegir" << std::endl;
            return;
        }
        v.ranura = ranuras[static_cast<size_t>(siguiente)];
        const int puesto = coches[static_cast<size_t>(siguiente)].position;
        // Camaras y pilotos T2: el rotulo del piloto (el de la repeticion) en lugar del aviso con el apellido.
        enviarRotulo(rdram, v.ranura);
        std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " coche " << siguiente << " de " << ranuras.size()
                  << " (ranura 0x" << std::hex << v.ranura << std::dec << ", puesto " << puesto + 1 << ")" << std::endl;
    }

    // Camaras y pilotos T2: la camara pasos lugares mas alla en la lista de la carrera (ps2x::RaceCameraSet); desde otra
    // del juego (la de la salida), la de conduccion que tiene el jugador. -1 si no hay ninguna.
    int siguienteCamara(const uint8_t *rdram, uint32_t jugador, const CamarasJugador &camaras, int actual, int pasos)
    {
        if (actual < 0 && camaras.modo != 0u)
            actual = static_cast<int>(leer32(rdram, camaras.modo + 0x80u)) - 1;
        return ps2x::stepRaceCamera(actual, camaras.lista(), pasos);
    }

    void cambiarCamaraModoFoto(uint8_t *rdram, uint32_t jugador, const CamarasJugador &camaras, int pasos)
    {
        VistaModoFoto &v = g_vistaModoFoto;
        const int siguiente = siguienteCamara(rdram, jugador, camaras, v.camara, pasos);
        ps2x::RaceCamera camara;
        if (siguiente < 0 || !ps2x::raceCameraAt(camaras.lista(), siguiente, camara))
        {
            std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " no hay camaras que elegir" << std::endl;
            return;
        }
        // Lo que hay que devolver al salir, antes de tocarlo por primera vez.
        if (camara.kind == ps2x::RaceCameraKind::Driving && (!v.modoCambiado || v.modo != camaras.modo))
        {
            v.modo = camaras.modo;
            v.modoJuego = camaras.modo != 0u ? leer32(rdram, camaras.modo + 0x80u) : 0u;
            v.modoCambiado = camaras.modo != 0u;
        }
        if (camara.kind != ps2x::RaceCameraKind::Driving && (!v.televisionCambiada || v.television != camaras.television))
        {
            v.television = camaras.television;
            v.subJuego = leer32(rdram, camaras.television + 0x20u);
            v.aBordo = camaras.aBordo;
            v.aBordoJuego = camaras.aBordo != 0u ? leer32(rdram, camaras.aBordo + 0x80u) : 0u;
            v.televisionCambiada = true;
        }
        v.indice = ponerCamara(rdram, jugador, camaras, camara);
        v.sub = camara.kind == ps2x::RaceCameraKind::Driving ? -1
                : camara.kind == ps2x::RaceCameraKind::Replay ? camaras.subABordo
                                                               : camaras.subPista;
        v.camara = siguiente;
        ps2x::showPhotoModeMessage(ps2x::raceCameraNotice(camaras.lista(), siguiente, ps2x::hostLanguage()));
        std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " camara " << siguiente << " de "
                  << ps2x::raceCameraCount(camaras.lista()) << " ("
                  << (camara.kind == ps2x::RaceCameraKind::Driving  ? "conduccion "
                      : camara.kind == ps2x::RaceCameraKind::Replay ? "repeticion "
                                                                     : "television")
                  << (camara.kind == ps2x::RaceCameraKind::Tv ? std::string() : std::to_string(camara.mode)) << "; "
                  << camaras.modos << " de conduccion, " << camaras.aBordoModos << " de repeticion)" << std::endl;
    }

    void vistasCarrera(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static const bool s_diag = std::getenv("F1_CAMARA_DIAG") != nullptr;
        const uint32_t vistas = GPR_U32(ctx, 4);
        if (ctx->pc != kVistasCarrera || !punteroEe(vistas)) // a medias (o sin vistas): sigue como iba
        {
            g_vistasCarreraOriginal(rdram, ctx, runtime);
            return;
        }
        VistaModoFoto &v = g_vistaModoFoto;
        const uint32_t jugador = jugadorCarrera(rdram);
        if (!ps2x::photoModeActive() || jugador == 0u)
        {
            devolverVistaModoFoto(rdram, jugador);
            g_vistasCarreraOriginal(rdram, ctx, runtime);
            if (s_diag)
                vistasCarreraDiag(rdram, vistas);
            return;
        }
        const CamarasJugador camaras = camarasJugador(rdram, jugador);
        if (v.activa && v.jugador != jugador)
            devolverVistaModoFoto(rdram, jugador);
        if (!v.activa)
        {
            v = {};
            v.activa = true;
            v.jugador = jugador;
            v.ranura = v.ranuraJuego = leer32(rdram, jugador + 0x14u);
            v.indice = v.indiceJuego = leer32(rdram, jugador + 0x90u);
            v.camara = camaraActual(rdram, jugador, camaras);
            const ps2x::RaceCameraSet lista = camaras.lista();
            std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " coche 0x" << std::hex << v.ranura
                      << std::dec << ", camara del jugador " << v.indice << " (" << lista.driving << " de conduccion, "
                      << lista.replay << " de repeticion, television " << (lista.tv ? "si" : "no") << "; la "
                      << v.camara << ")" << std::endl;
            enviarRotulo(rdram, v.ranura); // Camaras y pilotos T2: el piloto que se ve al entrar
        }
        const ps2x::PhotoModeViewRequest pedido = ps2x::takePhotoModeViewRequest();
        if (pedido.car != 0)
            cambiarCocheModoFoto(rdram, pedido.car);
        if (pedido.camera != 0)
            cambiarCamaraModoFoto(rdram, jugador, camaras, pedido.camera);
        if (pedido.car != 0 || pedido.camera != 0)
        {
            g_modoFoto.cambio = segundosDesdeArranque();
            g_modoFoto.movida = false;
        }
        escribirPalabra(rdram, jugador + 0x14u, v.ranura);
        escribirPalabra(rdram, jugador + 0x90u, v.indice);
        if (v.sub >= 0 && camaras.television != 0u)
            escribirPalabra(rdram, camaras.television + 0x20u, static_cast<uint32_t>(v.sub));
        g_vistasCarreraOriginal(rdram, ctx, runtime);
        if (s_diag)
            vistasCarreraDiag(rdram, vistas);
    }

    // Camaras y pilotos T2: el triangulo de la repeticion. func_1F9078(jugador, pausa), en cada fotograma de dibujo, rehace
    // las acciones de los controles del jugador (func_1F2C80: *(jugador + 0x28) + 0x18C + 4, de cero con las pulsaciones
    // nuevas; el bit 0x10000 es la de camara, se pulse con lo que se pulse) y, con *pausa = 0, las pide (func_1F4118) y con
    // + 0x1C pasa a la siguiente camara. En la repeticion el port se queda esa accion: al volver de func_1F4118 le quita el
    // bit (el juego no pasa a la suya) y la camara pasa a la siguiente de la lista de la carrera (PlayerCam, pista,
    // conduccion y vuelta): las de conduccion se pueden ver en la repeticion, de cualquier coche. (Quitarlo al entrar en
    // func_1F9078 no vale: la accion se rehace dentro.) Fuera de la repeticion y en el modo foto, como el juego.
    // Camaras y pilotos T4: el director de la repeticion (jugador + 0x2C; func_2162D8 en cada fotograma: func_216708 ->
    // func_216928) corta solo a la camara de pista (func_2FB738(contenedor, "replaycameras"): + 0x20 del contenedor)
    // cuando el coche sale del tramo de la que se ve (func_216570 -> func_202DE8), salvo con + 0x514; con + 0x510 (la
    // television automatica) elige tambien el coche y PlayerCam. El triangulo del juego, tras pasar a la siguiente, pone
    // + 0x510 = 0 y + 0x514 = 1 si la que se ve es PlayerCam (func_2FB8C0); el del port no lo hacia: tras pasar a la de
    // la repeticion, el siguiente corte del director volvia a poner la de pista y el triangulo leia la television (en la
    // repeticion del usuario siempre: "camara 4 -> 5" y luego "7 -> 0"). kDirectorAutomatico y kDirectorQuieto, arriba.
    constexpr uint32_t kControlesJugador = 0x001F4118u; // func_1F4118(controles): controles + 0x18C
    constexpr uint32_t kVueltaControles = 0x001F91F8u;  // en func_1F9078, tras pedirlas (jugador en s2)
    constexpr uint32_t kAccionCamara = 0x00010000u;
    PS2Runtime::RecompiledFunction g_controlesJugadorOriginal = nullptr;

    void controlesJugador(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == kControlesJugador;
        const uint32_t vuelta = GPR_U32(ctx, 31), jugador = GPR_U32(ctx, 18);
        g_controlesJugadorOriginal(rdram, ctx, runtime);
        if (!entrada || vuelta != kVueltaControles || ps2x::photoModeActive() || !ps2x::replayScrubActive() ||
            !punteroEe(jugador) || jugador != jugadorCarrera(rdram) || leer32(rdram, jugador + 0x1Cu) == 0u)
            return;
        const uint32_t acciones = GPR_U32(ctx, 2) + 4u;
        const uint32_t bits = leer32(rdram, acciones);
        if (!punteroEe(acciones) || (bits & kAccionCamara) == 0u)
            return;
        escribirPalabra(rdram, acciones, bits & ~kAccionCamara);
        const CamarasJugador camaras = camarasJugador(rdram, jugador);
        const int actual = camaraActual(rdram, jugador, camaras);
        const int siguiente = siguienteCamara(rdram, jugador, camaras, actual, 1);
        ps2x::RaceCamera camara;
        if (siguiente < 0 || !ps2x::raceCameraAt(camaras.lista(), siguiente, camara))
        {
            std::cout << "[f1-06:camaras] t=" << segundosDesdeArranque() << " repeticion: no hay camaras que elegir"
                      << std::endl;
            return;
        }
        const uint32_t indice = ponerCamara(rdram, jugador, camaras, camara);
        // Camaras y pilotos T4: y al director lo que le deja el triangulo del juego (ps2x::replayDirectorAfterCamera).
        // (Tambien con el director apagado, + 4 = 0, como esta con las de conduccion: al volver a la television sigue.)
        const uint32_t director = leer32(rdram, jugador + 0x2Cu);
        const bool conDirector = punteroEe(director) && leer32(rdram, jugador + 0x20u) != 1u;
        if (conDirector)
        {
            const ps2x::ReplayDirectorWrite d = ps2x::replayDirectorAfterCamera(camara.kind);
            escribirPalabra(rdram, director + kDirectorAutomatico, d.automatic);
            escribirPalabra(rdram, director + kDirectorQuieto, d.hold);
        }
        ps2x::showPhotoModeMessage(ps2x::raceCameraNotice(camaras.lista(), siguiente, ps2x::hostLanguage()));
        std::cout << "[f1-06:camaras] t=" << segundosDesdeArranque() << " repeticion: triangulo, camara " << actual << " -> "
                  << siguiente << " de " << ps2x::raceCameraCount(camaras.lista()) << " (+90 " << indice << ", "
                  << camaras.modos << " de conduccion, " << camaras.aBordoModos << " de repeticion, pista "
                  << (camaras.subPista >= 0 ? "si" : "no") << ", director "
                  << (conDirector ? (camara.kind == ps2x::RaceCameraKind::Tv ? "corta" : "quieto") : "no") << ")"
                  << std::endl;
    }

    // Al empezar la repeticion otra vez (su vuelta al final y el HUD de la repeticion, func_2045B0) el juego llama a
    // func_1F9460(jugador) = func_1F9480(jugador, + 0x90): toma la camara que se ve por el tipo, que en la repeticion es
    // siempre la 0 (television). Con una de conduccion (+ 0x90 = 2) seria el tipo 2: la camara 1 (la de pista sola) con el
    // triangulo apagado (+ 0x1C = 0; logs/cyp-t2). Entonces hace lo del tipo 4 (la de conduccion con el triangulo, + 0x1C =
    // 1 y + 0x90 = 2): la camara elegida sigue.
    constexpr uint32_t kCamaraOtraVez = 0x001F9460u;
    PS2Runtime::RecompiledFunction g_camaraOtraVezOriginal = nullptr;

    void camaraOtraVez(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t jugador = GPR_U32(ctx, 4);
        if (ctx->pc != kCamaraOtraVez || !punteroEe(jugador) || leer32(rdram, jugador + 0x90u) != kCamaraConduccion ||
            !ps2x::replayScrubActive() || jugador != jugadorCarrera(rdram))
        {
            g_camaraOtraVezOriginal(rdram, ctx, runtime);
            return;
        }
        escribirPalabra(rdram, jugador + 0x1Cu, 1u);
        escribirPalabra(rdram, jugador + 0x90u, kCamaraConduccion);
        std::cout << "[f1-06:camaras] t=" << segundosDesdeArranque()
                  << " repeticion: el juego pone otra vez la camara; sigue la de conduccion" << std::endl;
        ctx->pc = GPR_U32(ctx, 31);
    }

    // F1_CAMARA_DIAG=1: [f1-06:camaras] con quien llama al metodo siguiente del contenedor (0x2FBAB0), de PlayerCam
    // (0x2FC078) y de la camara de conduccion (0x1FD458).
    constexpr uint32_t kSiguientesCamara[] = {0x002FBAB0u, 0x002FC078u, 0x001FD458u};
    PS2Runtime::RecompiledFunction g_siguientesCamaraOriginal[3] = {};

    template <int N>
    void siguienteCamaraDiag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kSiguientesCamara[N])
            std::cout << "[f1-06:camaras] t=" << segundosDesdeArranque() << " siguiente "
                      << (N == 0 ? "contenedor" : N == 1 ? "PlayerCam" : "conduccion") << " 0x" << std::hex
                      << GPR_U32(ctx, 4) << " desde 0x" << GPR_U32(ctx, 31) << std::dec << std::endl;
        g_siguientesCamaraOriginal[N](rdram, ctx, runtime);
    }

    // Camaras y pilotos T4: func_1F8FF8(jugador, pausa), en cada fotograma, enciende el director de la repeticion con la
    // television (+ 0x90 < 2: func_216138) y lo apaga con otra camara: func_216250(director) pone + 4 = 0 y el coche que se
    // ve (jugador + 0x14) el propio (jugador + 0x10). En la repeticion del juego no pasa (+ 0x90 es siempre 0); con una de
    // conduccion de la lista de la carrera si: al pasar de la television (o de las de la repeticion) a ella volvia el
    // coche del jugador, y el piloto (manos y volante: func_22EE60, del coche por su numero, + 0x18, que no cambia) era
    // el del coche elegido (logs/usuario/prueba-cyp/captura-repeticion.png; logs/cyp-t4: "coche 0x76d1d0" y, al pasar
    // a la de conduccion, "0x76d22c" sin func_1F9368 ni func_216890). En la repeticion, con la de conduccion, el coche
    // elegido se queda. [f1-06:camaras] cuando lo deja.
    constexpr uint32_t kApagarDirector = 0x00216250u;
    PS2Runtime::RecompiledFunction g_apagarDirectorOriginal = nullptr;

    void apagarDirector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t director = GPR_U32(ctx, 4);
        const uint32_t jugador = ctx->pc == kApagarDirector && punteroEe(director) ? leer32(rdram, director) : 0u;
        const bool quedarse = punteroEe(jugador) && jugador == jugadorCarrera(rdram) && ps2x::replayScrubActive() &&
                              leer32(rdram, jugador + 0x90u) == kCamaraConduccion;
        const uint32_t coche = quedarse ? leer32(rdram, jugador + 0x14u) : 0u;
        g_apagarDirectorOriginal(rdram, ctx, runtime);
        if (!quedarse || leer32(rdram, jugador + 0x14u) == coche)
            return;
        escribirPalabra(rdram, jugador + 0x14u, coche);
        std::cout << "[f1-06:camaras] t=" << segundosDesdeArranque()
                  << " repeticion: el director se apaga con la de conduccion; sigue el coche 0x" << std::hex << coche
                  << std::dec << std::endl;
    }

    // Camaras y pilotos T4 (F1_CAMARA_DIAG=1): [f1-06:camaras] con quien cambia el coche del jugador (+ 0x14):
    // func_1F9368(jugador, numero) (el pedido), func_216890(director, ranura) (el director), func_1F9410(jugador, ranura),
    // y func_1F9078(jugador, pausa) si lo cambia por otro camino.
    constexpr uint32_t kCochesCamaraDiag[] = {0x001F9368u, 0x00216890u, 0x001F9410u, 0x001F9078u};
    PS2Runtime::RecompiledFunction g_cochesCamaraDiagOriginal[4] = {};

    template <int N>
    void cocheCamaraDiag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == kCochesCamaraDiag[N];
        const uint32_t a0 = GPR_U32(ctx, 4), a1 = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
        const uint32_t jugador = jugadorCarrera(rdram);
        const uint32_t antes = jugador != 0u ? leer32(rdram, jugador + 0x14u) : 0u;
        g_cochesCamaraDiagOriginal[N](rdram, ctx, runtime);
        const uint32_t despues = jugador != 0u ? leer32(rdram, jugador + 0x14u) : 0u;
        if (entrada && (N != 3 || antes != despues))
            std::cout << "[f1-06:camaras] t=" << segundosDesdeArranque() << " coche: func_" << std::hex
                      << kCochesCamaraDiag[N] << "(0x" << a0 << ", 0x" << a1 << ") desde 0x" << ra << ": +14 0x" << antes
                      << " -> 0x" << despues << std::dec << std::endl;
    }

    // Camaras y pilotos T2: los letreros de la repeticion (Replay, TV MODE, el logotipo y el rotulo del piloto) son su HUD
    // (vtable 0x4176D8, dibujado por su metodo + 0x4C, func_204360), no el de la carrera: en el modo foto no se dibujan,
    // asi que no salen en las fotos. Al salir vuelven como estaban (se siguen actualizando).
    constexpr uint32_t kHudRepeticionDibujar = 0x00204360u;
    PS2Runtime::RecompiledFunction g_hudRepeticionDibujarOriginal = nullptr;

    void hudRepeticionDibujar(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static bool s_oculto = false;
        const bool foto = ctx->pc == kHudRepeticionDibujar && ps2x::photoModeActive();
        if (ctx->pc == kHudRepeticionDibujar && foto != s_oculto)
        {
            s_oculto = foto;
            std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " letreros de la repeticion "
                      << (foto ? "fuera" : "otra vez") << " (hud 0x" << std::hex << GPR_U32(ctx, 4) << std::dec << ")"
                      << std::endl;
        }
        if (foto) // solo al entrar en la funcion
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        g_hudRepeticionDibujarOriginal(rdram, ctx, runtime);
    }

    void pasosCarrera(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static bool congelado = false;
        if (ctx->pc == kPasosCarrera)
            g_marchas.nFotograma = GPR_U32(ctx, 4);
        // Nativizacion T10: MODO FOTO de la pausa del juego; se enciende cuando la pausa ya se ha cerrado (+ 0x568).
        if (ctx->pc == kPasosCarrera)
        {
            const uint32_t juego = leer32(rdram, kJuegoActual);
            ps2x::servePhotoModeRequest(punteroEe(juego) && leer32(rdram, juego + 0x568u) != 0u);
        }
        const bool congelar = ps2x::photoModeActive() && ctx->pc == kPasosCarrera;
        if (congelar != congelado)
        {
            std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << (congelar ? " carrera congelada" : " carrera en marcha")
                      << std::endl;
            congelado = congelar;
        }
        if (congelar)
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        if (ctx->pc == kPasosCarrera && g_vistaModoFoto.activa) // Foto y repeticion T5: antes del primer paso
            devolverVistaModoFoto(rdram, jugadorCarrera(rdram));
        if (ctx->pc == kPasosCarrera)
            if (const SaltoRepeticion salto = repeticionPasos(rdram, ctx); salto.medir)
                g_saltoRepeticion = salto;
        const uint32_t pasos = GPR_U32(ctx, 4);
        const bool entrada = ctx->pc == kPasosCarrera;
        g_pasosCarreraOriginal(rdram, ctx, runtime);
        static const bool s_medirIa = std::getenv("F1_IA_MEDIR") != nullptr;
        if (s_medirIa && entrada)
            medirIa(rdram, pasos);
    }

    // En func_2C5948(camara, matriz, panoramica, a3, f12 = angulo): con el modo foto, la matriz y el angulo de la libre.
    void camaraModoFoto(uint8_t *rdram, R5900Context *ctx)
    {
        if (ctx->pc != kCamaraCarrera) // solo al entrar en la funcion, con sus argumentos
            return;
        ps2x::photoModeCameraFrame();
        ps2x::noteRaceCameraFrame(); // Aspecto T2: la carrera en pantalla, para la letra de los avisos (la del HUD)
        if (!ps2x::photoModeActive())
        {
            if (g_modoFoto.iniciado)
                std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " vuelve la camara del juego" << std::endl;
            g_modoFoto.iniciado = false;
            return;
        }
        const uint32_t camara = GPR_U32(ctx, 4), direccion = GPR_U32(ctx, 5);
        float matriz[16];
        for (uint32_t i = 0; i < 16u; ++i)
            matriz[i] = leerFloat(rdram, direccion + 4u * i);
        const auto ahora = std::chrono::steady_clock::now();
        // Foto y repeticion T5: tras cambiar el coche o la camara, la libre sale otra vez de la del juego y la sigue un rato
        // (sus camaras se asientan en el coche nuevo) hasta que se mueve.
        const ps2x::FreeCameraInput entrada = ps2x::photoModeInput();
        if (ps2x::freeCameraInputMoves(entrada))
            g_modoFoto.movida = true;
        const bool seguir = g_modoFoto.iniciado && camara == g_modoFoto.camara &&
                            ps2x::photoModeFollows(segundosDesdeArranque() - g_modoFoto.cambio, g_modoFoto.movida);
        if (seguir)
        {
            g_modoFoto.libre = ps2x::freeCameraFromMatrix(matriz, ctx->f[12]);
            g_modoFoto.ultimo = ahora;
        }
        if (!g_modoFoto.iniciado)
        {
            g_modoFoto.iniciado = true;
            g_modoFoto.cambio = -1.0;
            g_modoFoto.movida = false;
            g_modoFoto.camara = camara;
            g_modoFoto.libre = ps2x::freeCameraFromMatrix(matriz, ctx->f[12]);
            g_modoFoto.ultimo = ahora;
            g_modoFoto.registro = 0.0;
            g_modoFoto.otras = 0u;
            std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " camara 0x" << std::hex << camara << std::dec
                      << " en (" << matriz[12] << ", " << matriz[13] << ", " << matriz[14] << ") mirando (" << matriz[8]
                      << ", " << matriz[9] << ", " << matriz[10] << ") angulo " << ctx->f[12] << std::endl;
        }
        if (camara != g_modoFoto.camara)
        {
            ++g_modoFoto.otras;
            if (g_modoFoto.otras == 1u || g_modoFoto.otras % 50u == 0u)
                std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " otra camara 0x" << std::hex << camara
                          << std::dec << " (" << g_modoFoto.otras << ") en (" << matriz[12] << ", " << matriz[13] << ", "
                          << matriz[14] << ") angulo " << ctx->f[12] << std::endl;
            return;
        }
        const float segundos = std::chrono::duration<float>(ahora - g_modoFoto.ultimo).count();
        g_modoFoto.ultimo = ahora;
        ps2x::moveFreeCamera(g_modoFoto.libre, entrada, segundos);
        ps2x::freeCameraMatrix(g_modoFoto.libre, matriz);
        for (uint32_t i = 0; i < 16u; ++i)
        {
            uint32_t bits;
            std::memcpy(&bits, &matriz[i], 4u);
            std::memcpy(rdram + ((direccion + 4u * i) & PS2_RAM_MASK), &bits, 4u);
        }
        ctx->f[12] = g_modoFoto.libre.halfAngle;
        const double t = segundosDesdeArranque();
        if (t - g_modoFoto.registro >= 1.0)
        {
            g_modoFoto.registro = t;
            const ps2x::FreeCamera &libre = g_modoFoto.libre;
            std::cout << "[f1-06:modo-foto] t=" << t << " libre en (" << libre.position[0] << ", " << libre.position[1]
                      << ", " << libre.position[2] << ") giro " << libre.yaw << " cabeceo " << libre.pitch << " alabeo "
                      << libre.roll << " angulo " << libre.halfAngle << std::endl;
        }
    }

    // Aspecto T1: con la carrera congelada el juego sigue dibujando los efectos de la velocidad del coche, que se ven en el
    // modo foto (los bordes borrosos con la velocidad). Las opciones de camara del perfil (*0x49ED94: highspeedblur
    // + 0x234, highspeedzoom + 0x238, enginevibration + 0x23C, staticcockpitcam + 0x240; tabla Lua 0x3FC700) no se leen
    // mientras se corre: al montar la carrera (y en updateInGameOptions, func_163508 -> func_1E86D0) func_1E7898 las pasa
    // a bits del byte de cada jugador *0x3FE588 + j * 0x78 + 0x14 (1 desenfoque, 2 zoom, 4 vibracion, 8 cabina fija) y
    // func_30F040 hace con ese byte la mascara del objeto de los efectos de velocidad (+ 0xC: 4 desenfoque, 2 zoom, 1
    // vibracion). Cada fotograma func_30F080 (su metodo + 0x24, vtable 0x41EF68) dibuja con esa mascara el desenfoque
    // (con la velocidad guardada en 0x477598) y el zoom (0x477558, 0x47755C). En el modo foto func_30F080 se llama con la
    // mascara sin esos bits (ps2x::photoModeSpeedEffectsMask) y despues vuelve a ser la de antes: las opciones del jugador
    // y la carrera no cambian. (El byte del jugador lo mira tambien cada fotograma func_1FBA18, la camara de cabina: 4
    // vibracion, 8 cabina fija; su matriz la sustituye la camara libre.) [f1-06:modo-foto] al entrar (la mascara y como se
    // dibuja) y al salir (la mascara, el byte del jugador 1 y las tres opciones del perfil, que no cambian).
    constexpr uint32_t kEfectosVelocidad = 0x0030F080u;
    constexpr uint32_t kEfectosVelocidadMascara = 0xCu;
    constexpr uint32_t kPunteroConfiguracion = 0x0049ED94u; // el objeto de configuracion (opcion panoramica, mas abajo)
    PS2Runtime::RecompiledFunction g_efectosVelocidadOriginal = nullptr;

    void efectosVelocidad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t objeto = GPR_U32(ctx, 4);
        const bool foto = ps2x::photoModeActive();
        static bool anterior = false;
        if (ctx->pc == kEfectosVelocidad && punteroEe(objeto) && foto != anterior)
        {
            anterior = foto;
            const uint32_t mascara = leer32(rdram, objeto + kEfectosVelocidadMascara);
            std::cout << "[f1-06:modo-foto] t=" << segundosDesdeArranque() << " efectos de velocidad 0x" << std::hex << objeto
                      << ": mascara 0x" << mascara;
            if (foto)
                std::cout << " dibujada como 0x" << ps2x::photoModeSpeedEffectsMask(mascara, true);
            else
            {
                const uint32_t copia = leer32(rdram, kCopiaJugadoresCarrera);
                const uint32_t configuracion = leer32(rdram, kPunteroConfiguracion);
                if (punteroEe(copia))
                    std::cout << ", byte del jugador 1 0x" << static_cast<unsigned>(rdram[(copia + 0x14u) & PS2_RAM_MASK]);
                if (punteroEe(configuracion))
                    std::cout << std::dec << ", perfil: highspeedblur=" << leer32(rdram, configuracion + 0x234u)
                              << " highspeedzoom=" << leer32(rdram, configuracion + 0x238u)
                              << " enginevibration=" << leer32(rdram, configuracion + 0x23Cu);
            }
            std::cout << std::dec << std::endl;
        }
        if (ctx->pc != kEfectosVelocidad || !foto || !punteroEe(objeto)) // solo al entrar en la funcion
        {
            g_efectosVelocidadOriginal(rdram, ctx, runtime);
            return;
        }
        uint8_t *mascara = rdram + ((objeto + kEfectosVelocidadMascara) & PS2_RAM_MASK);
        uint32_t antes;
        std::memcpy(&antes, mascara, 4u);
        const uint32_t sin = ps2x::photoModeSpeedEffectsMask(antes, true);
        std::memcpy(mascara, &sin, 4u);
        g_efectosVelocidadOriginal(rdram, ctx, runtime);
        std::memcpy(mascara, &antes, 4u);
    }

    // Aspecto T1: el piloto (manos, casco y volante) tampoco se queda quieto con la carrera congelada. Su estado lo pone
    // al dia cada fotograma de dibujo func_22EE60(?, coche) (desde func_1F9078, no desde func_143ED0): func_2346E0
    // suaviza la cabeza y el cuerpo (piloto + 0xC8, + 0xCC, + 0xD8, un 20 % por fotograma) y func_22F320 le suma un ruido
    // (piloto + 0x60) con una amplitud que sale de la velocidad del coche (+ 0x88, + 0x8C); func_2343F0 y func_234370 son
    // las otras ramas. Con el coche congelado a mucha velocidad el ruido no para: el volante "baila". El juego se salta
    // todo func_22EE60 cuando *(*0x3FE5BC + 0x60) no es cero (su pausa); el modo foto hace lo mismo: no se llama, el
    // piloto se queda como estaba y al salir sigue desde ahi.
    constexpr uint32_t kAnimacionPiloto = 0x0022EE60u;
    PS2Runtime::RecompiledFunction g_animacionPilotoOriginal = nullptr;

    void animacionPiloto(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kAnimacionPiloto && ps2x::photoModeActive()) // solo al entrar en la funcion
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        g_animacionPilotoOriginal(rdram, ctx, runtime);
    }

    int porcentajeFov()
    {
#if defined(PS2X_ENABLE_DEBUG_UI)
        float valor = 0.0f;
        if (PS2OptionsMenu::gameOptionNumber("fov", valor))
            return static_cast<int>(std::lround(valor));
#endif
        const char *variable = std::getenv("PS2X_FOV");
        return variable != nullptr ? std::atoi(variable) : 0;
    }

    // Lejania T2: func_2C5948 receives fov, near and far in f12, f13 and f14 (from the camera data +0x90/+0x94/+0x98
    // read by 0x1169E0). F1_LEJOS_DIAG=1 prints each new (caller, near, far) once, up to 200 lines, so the values of
    // every camera can be read from a run. F1_LEJOS=<percent> scales far for exploration only (100 = as the game).
    void anotarLejos(const R5900Context *ctx)
    {
        static const bool activo = std::getenv("F1_LEJOS_DIAG") != nullptr;
        if (!activo)
            return;
        static std::set<std::tuple<uint32_t, int32_t, int32_t>> vistos;
        const uint32_t llamador = GPR_U32(ctx, 31);
        const auto clave = std::make_tuple(llamador, static_cast<int32_t>(std::lround(ctx->f[13] * 100.0f)),
                                           static_cast<int32_t>(std::lround(ctx->f[14])));
        if (vistos.size() >= 200u || !vistos.insert(clave).second)
            return;
        std::cout << "[f1-06:lejos] t=" << segundosDesdeArranque() << " llamador=0x" << std::hex << llamador << std::dec
                  << " camara=0x" << std::hex << GPR_U32(ctx, 4) << std::dec << " fov=" << ctx->f[12]
                  << " cerca=" << ctx->f[13] << " lejos=" << ctx->f[14] << std::endl;
    }

    float escalaLejos()
    {
        static const float escala = []
        {
            const char *variable = std::getenv("F1_LEJOS");
            const int porcentaje = variable != nullptr ? std::atoi(variable) : 100;
            return porcentaje > 0 ? porcentaje / 100.0f : 1.0f;
        }();
        return escala;
    }

    // Lejania T5: the option distancia_circuito (GRAFICOS PC row DISTANCIA DEL ESCENARIO, PS2X_DISTANCIA_CIRCUITO, 100
    // by default; 150 and 200) as a factor of the track's fade distances (see fundidoCircuito). The windows of A/B
    // modes 183 and 184 use 1.5 and 2 whatever the option says.
    float factorCircuito()
    {
        const int modo = g_ps2AbMode.load(std::memory_order_relaxed);
        if (modo == 183 || modo == 184)
            return modo == 183 ? 1.5f : 2.0f;
        int porcentaje = 100;
#if defined(PS2X_ENABLE_DEBUG_UI)
        float valor = 0.0f;
        if (PS2OptionsMenu::gameOptionNumber("distancia_circuito", valor))
            porcentaje = static_cast<int>(std::lround(valor));
        else
#endif
        if (const char *variable = std::getenv("PS2X_DISTANCIA_CIRCUITO"))
            porcentaje = std::atoi(variable);
        return std::clamp(porcentaje, 100, 200) / 100.0f;
    }

    void camaraCarrera(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        anotarLejos(ctx);
        // The far plane (2000 in a race) grows with distancia_circuito, so it never cuts the scenery before its fade
        // ends (1200 m x 2 = 2400 for the farthest type). L3: on its own it changes neither the picture nor the cost.
        ctx->f[14] *= escalaLejos() * factorCircuito();
        const int porcentaje = porcentajeFov();
        static int anterior = 0;
        if (porcentaje != anterior)
        {
            std::cout << "[f1-06:fov] t=" << segundosDesdeArranque() << " fov = " << porcentaje << std::endl;
            anterior = porcentaje;
        }
        ctx->f[12] = ps2x::widenedCameraFov(ctx->f[12], porcentaje);
        camaraModoFoto(rdram, ctx);
        g_camaraCarreraOriginal(rdram, ctx, runtime);
    }

    // Lejania T2/T4: level of detail of the cars. func_11E7E8 (a0 = the scene of the cars, 0x47C020 in a race) sets
    // [obj+0x2B0] (zoom, from the camera +0x38C) and [obj+0x2B4] = 6 - 5 * [obj+0xDC], where +0xDC follows the
    // frame-time quality factor v1 (0x4116CC, see trazarMedirFotograma): 1 when frames fit, halved towards 0 when
    // they overrun, so the scale goes from 1 to 6 (every level sqrt(6) = 2.45 times closer). Then func_11E280 (only
    // caller 0x11E900) walks the pieces [obj+0x2CC] (0x30 bytes each; 22 cars x 16 pieces + 10 more in a race): each
    // has a 32-byte descriptor at [[obj+0x2D0]] with four thresholds at +0x0..+0xC (-1 = unused), the position at
    // [+0x18] and the instance at +0x1C, and gets as level the first threshold above d^2 * zoom * scale (-1 past the
    // last one: not drawn), stored in 16-byte results at [0x4A40C8] + 0x140 (+0 visibility, +1 level, +4 d^2 * zoom,
    // +8 fade, +0xC instance).
    // func_11D538 then forces level 0 when [inst+0x84] != 0 and queues the groups of [model+0x90 + level * 12]. The
    // instance lists drawn by func_11D800 / func_11E7E8 itself (0x11DC3C, 0x11DDEC, 0x11EB54) are empty in a race.
    // Lejania T5, detalle_coches above the PS2 (nivelDetalleCoches): 2 (high) divides +0x2B4 by 4 for the call, so each
    // level, the close-only pieces included, switches twice as far (40/80/120 m, 80/160 m, 40 or 80 m, divided by
    // sqrt(zoom) as before); 3 (maximum) sets it to 0, so d^2 * zoom * scale is 0 and every piece with thresholds gets
    // level 0 at any distance, close-only pieces included, with full fade (+8 = 1): what func_11D538 does with the
    // forced pieces of the player's car (level min(0, levels - 1) whatever the distance), without writing [inst+0x84],
    // which is the game's own state. Pieces without thresholds (models with no levels) stay at -1 either way, and the
    // visibility (+0, the view test before) does not change. The game's scale is put back after the call (func_11E7E8
    // sets it again every frame).
    // F1_LOD_COCHES=<percent of distance> (100 = as the game) divides +0x2B4 by k^2 too, so each level is drawn k times
    // further, for exploration only. F1_LOD_DIAG=1 prints every 10 s (40 reports at most) the scale, each threshold
    // set with the levels chosen and the ones the distance gives without the forcing, the 3-4 threshold pieces by
    // distance and the forced ones; once, each model's levels.
    constexpr uint32_t kLodCoches = 0x0011E280u;
    PS2Runtime::RecompiledFunction g_lodCochesOriginal = nullptr;
    const bool g_lodDiag = std::getenv("F1_LOD_DIAG") != nullptr;

    float factorLod(const char *variable)
    {
        const char *valor = std::getenv(variable);
        const int porcentaje = valor != nullptr ? std::atoi(valor) : 100;
        return porcentaje > 0 ? porcentaje / 100.0f : 1.0f;
    }

    void escribirFloat(uint8_t *rdram, uint32_t direccion, float valor)
    {
        std::memcpy(rdram + (direccion & PS2_RAM_MASK), &valor, sizeof(valor));
    }

    struct LodConjunto
    {
        uint32_t piezas = 0u, ocultas = 0u, forzadas = 0u;
        std::array<uint32_t, 5> elegidos{}, naturales{};
        std::array<float, 5> minimo{}, maximo{}; // indexed by level + 1: -1 (none, not drawn) to 3
        std::set<uint32_t> modelos;
    };

    // func_11D538 (a0 = the scene, called at 0x11E620) reads the results of func_11E280 right after they are made:
    // they live in the scratchpad ([0x4A40C8] = 0x70000000) and are overwritten once func_11E280 goes on drawing,
    // so they are read here, before the call (the level the distance gives), and the chosen level afterwards from
    // [inst+0x9C].
    constexpr uint32_t kLodPiezas = 0x0011D538u;
    PS2Runtime::RecompiledFunction g_lodPiezasOriginal = nullptr;
    bool g_lodInforme = false;

    void lodPiezas(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!g_lodInforme)
        {
            g_lodPiezasOriginal(rdram, ctx, runtime);
            return;
        }
        g_lodInforme = false;
        const uint32_t objeto = GPR_U32(ctx, 4);
        const uint32_t vector = leer32(rdram, objeto + 0x2CCu);
        const uint32_t piezas = std::min((leer32(rdram, vector + 4u) - leer32(rdram, vector)) / 0x30u, 1024u);
        const uint32_t descriptores = leer32(rdram, leer32(rdram, objeto + 0x2D0u));
        const uint32_t resultados = leer32(rdram, 0x004A40C8u) + 0x140u;
        const float zoom = std::max(leerFloat(rdram, objeto + 0x2B0u), 1e-6f);
        PS2Memory &memoria = runtime->memory();
        struct Pieza
        {
            uint32_t instancia;
            int visible, natural;
            float distancia;
        };
        std::vector<Pieza> vistas;
        for (uint32_t s = 0; s < piezas; ++s)
        {
            const uint32_t resultado = resultados + s * 0x10u, cabecera = memoria.read32(resultado);
            const uint32_t bits = memoria.read32(resultado + 4u);
            float d2zoom = 0.0f;
            std::memcpy(&d2zoom, &bits, sizeof(d2zoom));
            vistas.push_back({memoria.read32(resultado + 0xCu), static_cast<int8_t>(cabecera & 0xFFu),
                              static_cast<int8_t>((cabecera >> 8) & 0xFFu), std::sqrt(std::max(0.0f, d2zoom / zoom))});
        }
        g_lodPiezasOriginal(rdram, ctx, runtime);
        std::map<std::array<float, 4>, LodConjunto> conjuntos;
        std::vector<std::tuple<float, int, int, bool>> detalladas;
        std::vector<std::pair<float, uint32_t>> forzadas;
        static std::set<uint32_t> modelosVistos;
        for (uint32_t s = 0; s < piezas; ++s)
        {
            const Pieza &pieza = vistas[s];
            std::array<float, 4> umbrales{};
            for (uint32_t i = 0; i < 4u; ++i)
                umbrales[i] = leerFloat(rdram, descriptores + s * 0x20u + i * 4u);
            LodConjunto &conjunto = conjuntos[umbrales];
            ++conjunto.piezas;
            const uint32_t modelo = leer32(rdram, pieza.instancia);
            conjunto.modelos.insert(modelo);
            if (modelosVistos.size() < 64u && modelosVistos.insert(modelo).second)
            {
                const uint32_t inicio = leer32(rdram, modelo + 0x90u), fin = leer32(rdram, modelo + 0x94u);
                std::cout << "[f1-06:lod] modelo=0x" << std::hex << modelo << std::dec << " niveles="
                          << (fin - inicio) / 12u << " grupos=";
                for (uint32_t e = inicio, i = 0; e < fin && i < 8u; e += 12u, ++i)
                    std::cout << (i ? "," : "") << leer32(rdram, e) << "-" << leer32(rdram, e + 4u);
                std::cout << " umbrales de la pieza=" << umbrales[0] << "," << umbrales[1] << "," << umbrales[2] << ","
                          << umbrales[3] << std::endl;
            }
            if (pieza.visible < 0)
            {
                ++conjunto.ocultas;
                continue;
            }
            // Past the last threshold the level stays -1 and the piece is not drawn.
            const int natural = std::clamp(pieza.natural, -1, 3);
            const int elegido = std::clamp<int>(static_cast<int8_t>(leerByte(rdram, pieza.instancia + 0x9Cu)), -1, 3);
            ++conjunto.elegidos[elegido + 1];
            if (conjunto.naturales[natural + 1]++ == 0u || pieza.distancia < conjunto.minimo[natural + 1])
                conjunto.minimo[natural + 1] = pieza.distancia;
            conjunto.maximo[natural + 1] = std::max(conjunto.maximo[natural + 1], pieza.distancia);
            const bool forzada = leer32(rdram, pieza.instancia + 0x84u) != 0u;
            if (forzada)
            {
                ++conjunto.forzadas;
                forzadas.emplace_back(pieza.distancia, pieza.instancia);
            }
            if (umbrales[2] != -1.0f)
                detalladas.emplace_back(pieza.distancia, elegido, natural, forzada);
        }
        const double ahora = segundosDesdeArranque();
        for (const auto &[umbrales, conjunto] : conjuntos)
        {
            std::cout << "[f1-06:lod] t=" << ahora << " umbrales=" << umbrales[0] << "," << umbrales[1] << ","
                      << umbrales[2] << "," << umbrales[3] << " piezas=" << conjunto.piezas << " modelos="
                      << conjunto.modelos.size() << " ocultas=" << conjunto.ocultas << " forzadas=" << conjunto.forzadas
                      << " elegido(-1..3)=";
            for (size_t i = 0; i < conjunto.elegidos.size(); ++i)
                std::cout << (i ? "," : "") << conjunto.elegidos[i];
            std::cout << " por distancia:";
            for (size_t i = 0; i < conjunto.naturales.size(); ++i)
                if (conjunto.naturales[i] != 0u)
                    std::cout << " " << static_cast<int>(i) - 1 << "=" << conjunto.naturales[i] << "("
                              << std::lround(conjunto.minimo[i]) << "-" << std::lround(conjunto.maximo[i]) << "m)";
            std::cout << std::endl;
        }
        std::sort(detalladas.begin(), detalladas.end());
        std::cout << "[f1-06:lod] t=" << ahora << " zoom*escala=" << zoom * leerFloat(rdram, objeto + 0x2B4u)
                  << " piezas de 3-4 umbrales elegido/por distancia@m:";
        for (size_t i = 0; i < detalladas.size() && i < 40u; ++i)
            std::cout << " " << std::get<1>(detalladas[i]) << "/" << std::get<2>(detalladas[i]) << "@"
                      << std::lround(std::get<0>(detalladas[i])) << (std::get<3>(detalladas[i]) ? "F" : "");
        std::cout << std::endl;
        std::sort(forzadas.begin(), forzadas.end());
        std::cout << "[f1-06:lod] t=" << ahora << " forzadas=" << forzadas.size() << ":";
        for (size_t i = 0; i < forzadas.size() && i < 16u; ++i)
            std::cout << " 0x" << std::hex << forzadas[i].second << std::dec << "@" << std::lround(forzadas[i].first);
        std::cout << std::endl;
    }

    void lodCoches(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static const float exploracion = factorLod("F1_LOD_COCHES") * factorLod("F1_LOD_COCHES");
        const int nivel = nivelDetalleCoches();
        const uint32_t objeto = GPR_U32(ctx, 4);
        const float escalaJuego = leerFloat(rdram, objeto + 0x2B4u);
        const float usada = nivel >= 3 ? 0.0f : escalaJuego / (nivel == 2 ? exploracion * 4.0f : exploracion);
        escribirFloat(rdram, objeto + 0x2B4u, usada);
        static double siguiente = 0.0;
        static uint32_t informes = 0u;
        const double ahora = segundosDesdeArranque();
        if (g_lodDiag && ahora >= siguiente && informes < 40u)
        {
            siguiente = ahora + 10.0;
            ++informes;
            g_lodInforme = true;
            std::cout << "[f1-06:lod] t=" << ahora << " objeto=0x" << std::hex << objeto << std::dec
                      << " detalle=" << leerFloat(rdram, objeto + 0xDCu) << " zoom=" << leerFloat(rdram, objeto + 0x2B0u)
                      << " escala del juego=" << escalaJuego << " usada=" << usada << " nivel=" << nivel
                      << " calidad v0,v1=" << leerFloat(rdram, 0x4116C8u) << "," << leerFloat(rdram, 0x4116CCu)
                      << std::endl;
        }
        g_lodCochesOriginal(rdram, ctx, runtime);
        escribirFloat(rdram, objeto + 0x2B4u, escalaJuego);
    }

    // Lejania T2/T4: the track. It is drawn by func_327DA8 (a0 = the world, 0xEFDF70 in a race; called from 0x1192BC
    // and 0x326588), not by func_11E7E8. Its scenery objects fade with the distance on the ground by type (func_3D9AD0,
    // lhu +0xE): func_328DB0(world, a1, camera, mode) gives for each of the 4 types the fade end
    // (f12 * T[i] * max(1, [camera+0x388]))^2 at world+0x12C, the start with T[i] - 75 at a1+0x14 and 1 / (end - start)
    // at a1+0x28, with T the table 0x46F1B8 (0x46F1A0 and no camera factor when mode != 0) and f12 = 0.6 + 0.4 * v0
    // (the quality factor 0x4116C8, func_2FC730), so the scenery ends up to 40 % closer when frames are lost; f13
    // (= f12) scales world+0x13C/+0x140 (450 and 375). Lejania T5: the hook multiplies f12 and f13 by factorCircuito
    // (distancia_circuito / 100: 1, 1.5 or 2), read on every call, so the fade of each type starts and ends that much
    // further (the window between them grows with it: 75 m x k). F1_LOD_CIRCUITO=<percent of distance> scales them
    // too, for exploration only; F1_LOD_DIAG=1 prints the distances every 10 s (40 reports at most).
    constexpr uint32_t kFundidoCircuito = 0x00328DB0u;
    PS2Runtime::RecompiledFunction g_fundidoCircuitoOriginal = nullptr;

    void fundidoCircuito(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static const float exploracion = factorLod("F1_LOD_CIRCUITO");
        const float factor = exploracion * factorCircuito();
        ctx->f[12] *= factor;
        ctx->f[13] *= factor;
        const uint32_t mundo = GPR_U32(ctx, 4), datos = GPR_U32(ctx, 5), modo = GPR_U32(ctx, 7);
        const float f12 = ctx->f[12];
        g_fundidoCircuitoOriginal(rdram, ctx, runtime);
        static double siguiente = 0.0;
        static uint32_t informes = 0u;
        const double ahora = segundosDesdeArranque();
        if (!g_lodDiag || ahora < siguiente || informes >= 40u)
            return;
        siguiente = ahora + 10.0;
        ++informes;
        std::cout << "[f1-06:lod] t=" << ahora << " circuito mundo=0x" << std::hex << mundo << std::dec << " modo=" << modo
                  << " f12=" << f12 << " tabla=";
        for (uint32_t i = 0; i < 4u; ++i)
            std::cout << (i ? "," : "") << leerFloat(rdram, (modo != 0u ? 0x0046F1A0u : 0x0046F1B8u) + i * 4u);
        std::cout << " fundido por tipo (m)=";
        for (uint32_t i = 0; i < 4u; ++i)
            std::cout << (i ? " " : "") << std::lround(std::sqrt(std::max(0.0f, leerFloat(rdram, datos + 0x14u + i * 4u))))
                      << "-" << std::lround(std::sqrt(std::max(0.0f, leerFloat(rdram, mundo + 0x12Cu + i * 4u))));
        std::cout << " 0x13C,0x140 (m)=" << std::sqrt(std::max(0.0f, leerFloat(rdram, mundo + 0x13Cu))) << ","
                  << std::sqrt(std::max(0.0f, leerFloat(rdram, mundo + 0x140u))) << std::endl;
    }
    struct FovVisto
    {
        float fov = 0.0f, horizontal = 0.0f;
        uint32_t veces = 0u, panoramica = 0u, llamador2 = 0u;
    };
    std::map<std::pair<uint32_t, uint32_t>, FovVisto> g_fovVistos;

    void anotarFov(uint8_t *rdram, R5900Context *ctx, uint32_t llamador, uint32_t camara, uint32_t panoramica)
    {
        FovVisto &visto = g_fovVistos[{llamador, camara}];
        visto.fov = leerFloat(rdram, camara + 0x88u);
        visto.horizontal = leerFloat(rdram, camara + 0x84u);
        visto.panoramica = panoramica;
        ++visto.veces;
        (void)ctx;
        static double ultimo = 0.0;
        const double ahora = segundosDesdeArranque();
        if (ahora - ultimo < 5.0)
            return;
        ultimo = ahora;
        for (const auto &[clave, v] : g_fovVistos)
            std::cout << "[f1-06:fov] t=" << ahora << " llamador 0x" << std::hex << clave.first << " camara 0x"
                      << clave.second << std::dec << " fov " << v.fov << " horizontal " << v.horizontal
                      << " panoramica " << v.panoramica << " veces " << v.veces << std::endl;
        g_fovVistos.clear();
    }

    void camaraVistaFov(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t camara = GPR_U32(ctx, 4), panoramica = GPR_U32(ctx, 6), llamador = GPR_U32(ctx, 31);
        g_camaraVistaFovOriginal(rdram, ctx, runtime);
        anotarFov(rdram, ctx, llamador, camara, panoramica);
    }

    void camaraFov(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t camara = GPR_U32(ctx, 4), panoramica = GPR_U32(ctx, 5), llamador = GPR_U32(ctx, 31);
        g_camaraFovOriginal(rdram, ctx, runtime);
        anotarFov(rdram, ctx, llamador, camara, panoramica);
    }

    // ---- Chivatos de funciones nativas llamadas desde Lua (front-end) ----
    // En una funcion C de Lua 5.0, a0 = lua_State; L->top (+8) y L->base (+12) delimitan los argumentos (TObject de
    // 8 bytes: tipo y valor). Las cadenas (tipo 4) son TString con un puntero al texto en +16. Los numeros son float.
    // Las funciones nativas estan en analisis/lua-funciones-nativas.txt (graph = tabla 0x456968).
    std::string argumentoLua(const uint8_t *rdram, uint32_t L, uint32_t indice)
    {
        const uint32_t base = leer32(rdram, L + 12u);
        const uint32_t top = leer32(rdram, L + 8u);
        const uint32_t objeto = base + 8u * indice;
        if (objeto >= top)
            return "";
        const uint32_t tipo = leer32(rdram, objeto);
        const uint32_t valor = leer32(rdram, objeto + 4u);
        switch (tipo)
        {
        case 0:
            return "nil";
        case 1:
            return valor ? "true" : "false";
        case 3:
        {
            float numero = 0.0f;
            std::memcpy(&numero, &valor, sizeof(numero));
            char texto[32];
            std::snprintf(texto, sizeof(texto), "%g", numero);
            return texto;
        }
        case 4:
            return "\"" + leerCadena(rdram, leer32(rdram, valor + 16u)) + "\"";
        default:
            return "<tipo " + std::to_string(tipo) + ">";
        }
    }

    // luaD_throw de Lua 5.0 (0x29F878: con L->errorJmp, en +0x50, hace longjmp; sin el, panic y exit(1)). Registra
    // cada error de Lua con su mensaje (la cima de la pila, L->top - 8) para saber por que falla un script.
    constexpr uint32_t kLuaThrow = 0x0029F878u;
    PS2Runtime::RecompiledFunction g_luaThrowOriginal = nullptr;

    void trazarLuaThrow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        const uint32_t L = GPR_U32(ctx, 4);
        if (veces++ < 200u)
        {
            const uint32_t top = leer32(rdram, L + 8u);
            const uint32_t base = leer32(rdram, L + 12u);
            std::string mensaje = "?";
            if (top >= base + 8u)
            {
                // argumentoLua lee desde L->base: el indice de la cima es (top - 8 - base) / 8.
                mensaje = argumentoLua(rdram, L, (top - 8u - base) / 8u);
            }
            std::cout << "[f1-06:lua-error] t=" << segundosDesdeArranque() << " codigo=" << GPR_U32(ctx, 5)
                      << " protegido=" << (leer32(rdram, L + 0x50u) != 0u ? "si" : "NO") << " mensaje=" << mensaje
                      << std::endl;
            if (GPR_U32(ctx, 5) == 4u) // LUA_ERRMEM: el asignador del juego (G->frealloc, G+0xC, con G->ud en +0x10)
            {
                const uint32_t G = leer32(rdram, L + 0x10u);
                std::cout << "[f1-06:lua-error]   sin memoria: frealloc=0x" << std::hex << leer32(rdram, G + 0xCu)
                          << " ud=0x" << leer32(rdram, G + 0x10u) << std::dec << " totalLua=" << leer32(rdram, G + 0x4Cu)
                          << std::endl;
            }
            imprimirPila(rdram, GPR_U32(ctx, 31), GPR_U32(ctx, 29), 12);
        }
        g_luaThrowOriginal(rdram, ctx, runtime);
    }

    // luaM_realloc de Lua 5.0 (0x2A2D38(L, bloque, tamanoViejo, tamanoNuevo)): F1_LUA_MEMORIA=1 registra cada segundo
    // los bytes de Lua (G->nblocks, G+0x4C) y el umbral del recolector (G->GCthreshold, G+0x48).
    constexpr uint32_t kLuaRealloc = 0x002A2D38u;
    PS2Runtime::RecompiledFunction g_luaReallocOriginal = nullptr;

    void trazarLuaRealloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static double s_ultimo = 0.0;
        const double ahora = segundosDesdeArranque();
        if (ahora - s_ultimo >= 1.0)
        {
            s_ultimo = ahora;
            const uint32_t G = leer32(rdram, GPR_U32(ctx, 4) + 0x10u);
            std::cout << "[f1-06:lua-mem] t=" << ahora << " bytes=" << leer32(rdram, G + 0x4Cu)
                      << " umbral=" << leer32(rdram, G + 0x48u) << " pide=" << GPR_U32(ctx, 7) << " heap=0x" << std::hex
                      << leer32(rdram, 0x0040495Cu) << " frealloc=0x" << leer32(rdram, G + 0xCu) << std::dec << std::endl;
        }
        g_luaReallocOriginal(rdram, ctx, runtime);
    }

    // Montones de Lua: func_29B740(tamano, 4) destruye el anterior y crea uno nuevo (puntero en 0x40495C; func_29C728
    // sube 1 MB a 1,5 MB). Los fija el script por la funcion nativa de 0x149AF0: 2 MB en los menus y 1 MB en la carrera.
    // El monton solo sirve los bloques de hasta 1 KB (func_29C858); los mayores salen del malloc del juego.
    // Nativizacion T4: el de los menus (2 MB) pasa a 4 MB; el buffer sale del malloc general, al que en el peor menu
    // medido le quedaban ~7,2 MB libres con 2 MB (~5 MB con 4). La carrera y la pausa no se tocan (alli el general va
    // justo).
    // F1_LUA_MENU_MB=2 vuelve al tamano original (vale cualquier valor de 1 a 8).
    constexpr uint32_t kCrearMontonLua = 0x0029B740u;
    // RAM extra T4: con la region alta, el de la carrera (1 MB) pasa a g_montonLuaCarrera (ver ram_alta).
    constexpr uint32_t kMontonLuaMenus = 0x00200000u;
    constexpr uint32_t kMontonLuaCarrera = 0x00100000u;
    PS2Runtime::RecompiledFunction g_crearMontonLuaOriginal = nullptr;
    uint32_t g_montonLuaMenus = 0x00400000u;
    uint32_t g_montonLuaCarrera = 0x00400000u;

    void trazarCrearMontonLua(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pedido = GPR_U32(ctx, 4);
        if (pedido == kMontonLuaMenus)
            SET_GPR_U32(ctx, 4, g_montonLuaMenus);
        else if (pedido == kMontonLuaCarrera && ram_alta::g_activa)
            SET_GPR_U32(ctx, 4, g_montonLuaCarrera);
        std::cout << "[f1-06:lua-monton] t=" << segundosDesdeArranque() << " tamano=0x" << std::hex << pedido
                  << " usado=0x" << GPR_U32(ctx, 4) << " ra=0x" << GPR_U32(ctx, 31) << std::dec << std::endl;
        g_crearMontonLuaOriginal(rdram, ctx, runtime);
    }

    // lua_load de Lua 5.0 (0x29E030(L, lector, datos, nombre)): con F1_LUA_MEMORIA, cada trozo cargado con su nombre y
    // los bytes de Lua antes y despues.
    // Fase 3 T6 (menus del juego en Lua). Casi todos los trozos llegan con uno de dos lectores de una sola vez cuyos
    // datos son {puntero, tamano}: getS de luaL_loadbuffer (0x2AC568) y el de los scripts del front-end (0x285AA8, con
    // el bufer que saca func_2854E0 de los .luc; lo devuelve entero y deja los datos vacios). Asi el trozo se lee de la
    // RAM sin llamar al lector del juego (llamar codigo del juego desde aqui no es seguro: puede ceder el hilo en un
    // punto de control). Los otros lectores (0x29B608, archivo a trozos de 16 KB; 0x2AD1B0) no se vuelcan.
    // F1_LUA_VOLCAR=<carpeta>: guarda cada trozo distinto en <carpeta>/<nombre del trozo> (una segunda version distinta
    // con el mismo nombre, en <nombre>~2...) y anota en <carpeta>/indice.txt nombre, tamano, formato (texto o bytecode
    // 0x1B) y primeros bytes.
    // Mods: si <mods>/<nombre del trozo> existe (PS2Runtime::getIoPaths().modsRoot, PS2X_MODS en desarrollo), se carga
    // ese archivo en su lugar con el mismo nombre: el lector pasa a ser getS y el chivato de getS sirve el archivo por
    // trozos de 2 KB desde un bufer del monton del runtime (f_parser compila el texto; un 0x1B al principio es
    // bytecode). Sin F1_LUA_MEMORIA, F1_LUA_VOLCAR, trozos .lua en los mods ni las opciones del PC en OPTIONS (fase 3 T6,
    // servirOpcionesJuego) no se instala nada.
    constexpr uint32_t kLuaLoad = 0x0029E030u;
    constexpr uint32_t kLuaLectorGetS = 0x002AC568u;
    constexpr uint32_t kLuaLectorFrontEnd = 0x00285AA8u;
    constexpr uint32_t kBuferModLua = 0x800u;
    PS2Runtime::RecompiledFunction g_luaLoadOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_luaGetSOriginal = nullptr;
    bool g_luaMemoria = false;
    std::filesystem::path g_luaVolcado;
    std::map<std::string, std::vector<uint64_t>> g_luaVolcados; // nombre -> huellas de las versiones ya guardadas
    std::map<std::string, std::filesystem::path> g_modsLua;      // nombre del trozo en minusculas -> archivo

    // Trozo de los mods que se esta sirviendo: los datos de su lua_load (la clave) y lo que queda.
    struct TrozoModLua
    {
        uint32_t datos = 0u;
        std::vector<uint8_t> bytes;
        size_t servidos = 0u;
    };
    TrozoModLua g_trozoModLua;
    uint32_t g_buferModLua = 0u;

    std::string claveTrozoLua(std::string nombre)
    {
        std::replace(nombre.begin(), nombre.end(), '\\', '/');
        while (!nombre.empty() && (nombre.front() == '/' || nombre.front() == '@'))
            nombre.erase(nombre.begin());
        std::transform(nombre.begin(), nombre.end(), nombre.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return nombre;
    }

    // Nombre del trozo como ruta relativa segura: solo [A-Za-z0-9._-] y '/', sin ".." ni ruta absoluta.
    std::string rutaTrozoLua(const std::string &nombre)
    {
        std::string ruta;
        for (char c : nombre)
        {
            const bool valido = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
            ruta.push_back(valido ? c : (c == '/' || c == '\\') ? '/' : '_');
        }
        std::string limpia, parte;
        std::stringstream partes(ruta);
        while (std::getline(partes, parte, '/'))
        {
            if (parte.empty() || parte == "." || parte == "..")
                continue;
            limpia += (limpia.empty() ? "" : "/") + parte;
        }
        return limpia.empty() ? "_sin_nombre" : limpia;
    }

    void volcarTrozoLua(const uint8_t *rdram, uint32_t lector, uint32_t datos, const std::string &nombre)
    {
        static uint32_t sinVolcar = 0;
        if (lector != kLuaLectorGetS && lector != kLuaLectorFrontEnd)
        {
            if (sinVolcar++ < 50u)
                std::cout << "[f1-06:lua-volcado] " << nombre << ": lector 0x" << std::hex << lector << std::dec
                          << " sin volcar" << std::endl;
            return;
        }
        const uint32_t puntero = leer32(rdram, datos);
        const uint32_t tamano = leer32(rdram, datos + 4u);
        if (tamano == 0u || tamano > 0x00400000u || (puntero & PS2_RAM_MASK) + tamano > PS2_RAM_SIZE)
            return;
        const uint8_t *bytes = rdram + (puntero & PS2_RAM_MASK);
        uint64_t huella = 1469598103934665603ull;
        for (uint32_t i = 0; i < tamano; ++i)
            huella = (huella ^ bytes[i]) * 1099511628211ull;
        std::vector<uint64_t> &versiones = g_luaVolcados[nombre];
        if (std::find(versiones.begin(), versiones.end(), huella) != versiones.end())
            return;
        versiones.push_back(huella);
        std::string relativa = rutaTrozoLua(nombre);
        if (versiones.size() > 1u)
            relativa += "~" + std::to_string(versiones.size());
        const std::filesystem::path archivo = g_luaVolcado / std::filesystem::path(relativa);
        std::error_code error;
        std::filesystem::create_directories(archivo.parent_path(), error);
        std::ofstream(archivo, std::ios::binary).write(reinterpret_cast<const char *>(bytes), tamano);
        std::string primeros;
        for (uint32_t i = 0; i < tamano && i < 24u; ++i)
            primeros.push_back(bytes[i] >= 0x20 && bytes[i] < 0x7F ? static_cast<char>(bytes[i]) : '.');
        std::ofstream indice(g_luaVolcado / "indice.txt", std::ios::app);
        indice << nombre << '\t' << tamano << '\t' << (bytes[0] == 0x1Bu ? "bytecode" : "texto") << '\t' << relativa << '\t'
               << primeros << '\n';
    }

    void trazarLuaGetS(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t datos = GPR_U32(ctx, 5);
        if (g_trozoModLua.datos == 0u || datos != g_trozoModLua.datos)
        {
            g_luaGetSOriginal(rdram, ctx, runtime);
            return;
        }
        const size_t quedan = g_trozoModLua.bytes.size() - g_trozoModLua.servidos;
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(quedan, kBuferModLua));
        std::memcpy(rdram + (g_buferModLua & PS2_RAM_MASK), g_trozoModLua.bytes.data() + g_trozoModLua.servidos, n);
        g_trozoModLua.servidos += n;
        std::memcpy(rdram + (GPR_U32(ctx, 6) & PS2_RAM_MASK), &n, sizeof(n));
        SET_GPR_U32(ctx, 2, n != 0u ? g_buferModLua : 0u);
        if (n == 0u)
            g_trozoModLua = {};
        ctx->pc = GPR_U32(ctx, 31);
    }

    // Los mods en Lua: cada archivo .lua de la carpeta de mods (recursivo) por su ruta relativa.
    void buscarModsLua()
    {
        const std::filesystem::path &raiz = PS2Runtime::getIoPaths().modsRoot;
        std::error_code error;
        if (raiz.empty() || !std::filesystem::is_directory(raiz, error))
            return;
        for (std::filesystem::recursive_directory_iterator it(raiz, error), fin; !error && it != fin; it.increment(error))
        {
            if (!it->is_regular_file(error))
                continue;
            std::string extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".lua")
                continue;
            const std::string relativa = std::filesystem::relative(it->path(), raiz, error).generic_string();
            g_modsLua[claveTrozoLua(relativa)] = it->path();
        }
        if (!g_modsLua.empty())
            std::cout << "[mods-lua] " << g_modsLua.size() << " trozos de Lua en " << raiz.string() << std::endl;
    }

    // ---- Fase 3 T6: nuestras opciones dentro del menu OPTIONS del propio juego ----
    // OPTIONS (menu principal y pausa) gana la entrada "GRAFICOS PC" ("PC GRAPHICS" si el juego no esta en espanol), que
    // abre la misma pantalla que DISPLAY SETTINGS (nodo DisplaySettings del grafo; los .bgf no se tocan) con nuestras
    // filas: los mismos ClassConfigMenuToggleItem y ClassSlider del juego (fuentes, sonidos, flechas, ayuda abajo), Atras
    // deshace (revert de cada fila) y CONFIRM guarda opciones.ini.
    // El bytecode del juego no se cambia: al cargar uno de los trozos de kExtensionesLua, el gancho de lua_load guarda
    // sus bytes y sirve en su lugar un texto nuestro que primero ejecuta el original, pedido con
    // loadstring("", "=ps2x:<trozo>") (el mismo gancho reconoce ese nombre y sirve los bytes guardados por getS; f_parser
    // ve el 0x1B y lo carga como bytecode) en el entorno del trozo (setfenv(f, getfenv(1))), y despues nuestra extension,
    // que envuelve funciones del original. Asi no hace falta crear cadenas de Lua desde aqui (lua_pushlstring llamaria
    // al asignador del juego).
    // Puente: os.getenv (0x2AF258; los scripts del juego no lo usan) con claves "ps2x:<...>" devuelve un numero:
    // "ps2x:<opcion>" el valor de la opcion (ps2x::menuOptionNumber) y "ps2x:<opcion>", v la cambia en caliente (sobre el
    // modelo de PS2OptionsMenu: PS2OptionsMenu::setGameOptionNumber, aplicado en el siguiente fotograma); "ps2x:pagina"[, v]
    // dice si DisplaySettings abre nuestras filas (1 GRAFICOS PC, 2 AJUSTES PC, 3 PANTALLA PC, 4 JUEGO EN RED PC, 5 la
    // lista OPCIONES PC que lleva a las otras cuatro) o las del juego (0); "ps2x:abrir" toma los valores que F11/F10 hayan cambiado y "ps2x:guardar" escribe opciones.ini (las
    // claves de PANTALLA PC y JUEGO EN RED PC, en puentePantallaPc y puenteJuegoEnRed). Las demas claves van a la funcion
    // original.
    // F1_OPCIONES_JUEGO=0 lo desactiva (los trozos se cargan tal cual).
    constexpr uint32_t kOsGetenv = 0x002AF258u;
    constexpr const char *kPrefijoOriginalLua = "=ps2x:";
    PS2Runtime::RecompiledFunction g_osGetenvOriginal = nullptr;
    bool g_opcionesJuego = false;
    int g_paginaOpciones = 0;
    std::map<std::string, std::vector<uint8_t>> g_trozosOriginales; // clave del trozo -> su bytecode

    // OPTIONS del menu principal: la entrada nueva va detras de DISPLAY SETTINGS (las entradas se crean en cada
    // customActivate con addMenuItem(indice, texto, salida, ayuda, llamada); ClassTextMenu.userSelect llama a la
    // llamada y despues emite la salida). Foto y repeticion T2: una sola entrada, OPCIONES PC (pagina 5), con las cuatro
    // paginas del PC dentro; con cuatro entradas mas, OPTIONS tenia 11 y la ultima quedaba cortada por la banda de abajo.
    constexpr const char *kExtensionOpciones = R"lua(
if Menu and Menu.addMenuItem then
  local G = os.getenv
  local anadir = Menu.addMenuItem
  Menu.addMenuItem = function(self, i, texto, salida, ayuda, llamada)
    if salida ~= "DisplaySettings" then
      return anadir(self, i, texto, salida, ayuda, llamada)
    end
    i = anadir(self, i, texto, salida, ayuda, function(m)
      G("ps2x:pagina", 0)
      if llamada then llamada(m) end
    end)
    local titulo = "PC OPTIONS"
    local nota = "The PC's graphics, display, settings and netplay."
    if game.language() == game.spanish then
      titulo = "OPCIONES PC"
      nota = "Gr\195\161ficos, pantalla, ajustes y juego en red del PC."
    end
    return anadir(self, i, titulo, salida, nota, function() G("ps2x:pagina", 5) end)
  end
end
)lua";

    // OPTIONS de la pausa: sus tablas se llenan al cargar el trozo; la entrada nueva va detras de goTVSettings (DISPLAY
    // SETTINGS), con la misma salida. Nativizacion T10: con modo de pantalla y volumen.
    constexpr const char *kExtensionOpcionesPausa = R"lua(
if Menu and Menu.mMenuOutcomeItems and Menu.mMenuTextItems and Menu.mMenuCallbackItems then
  local G = os.getenv
  local k = nil
  for n = 1, table.getn(Menu.mMenuOutcomeItems) do
    if Menu.mMenuOutcomeItems[n] == "goTVSettings" then k = n end
  end
  if k then
    local llamada = Menu.mMenuCallbackItems[k]
    Menu.mMenuCallbackItems[k] = function(m)
      G("ps2x:pagina", 0)
      if llamada then return llamada(m) end
    end
    local titulo = "PC GRAPHICS"
    local nota = "PC graphics: image filter, supersampling, vertical sync, FPS counter, screen mode, volume and car detail."
    if game.language() == game.spanish then
      titulo = "GR\195\129FICOS PC"
      nota = "Gr\195\161ficos del PC: filtro, supersampling, sincronizaci\195\179n vertical, contador de FPS, pantalla, volumen y detalle de los coches."
    end
    table.insert(Menu.mMenuTextItems, k + 1, titulo)
    table.insert(Menu.mMenuOutcomeItems, k + 1, "goTVSettings")
    if Menu.mHelpText then table.insert(Menu.mHelpText, k + 1, nota) end
    if Menu.mMenuSelectableItems then table.insert(Menu.mMenuSelectableItems, k + 1, true) end
    table.insert(Menu.mMenuCallbackItems, k + 1, function() G("ps2x:pagina", 1) end)
  end
end
)lua";

    // Nativizacion T10: MODO FOTO (PHOTO MODE) en el menu de pausa de la carrera, detras de REPLAY (PAU_RPLYS): cierra la
    // pausa como CONTINUE (actionContinue: graph.outcome y pause.continue al acabar el fundido) y pide el modo foto
    // ("ps2x:modo_foto"); el hilo del EE lo enciende en cuanto la pausa se ha cerrado (pasosCarrera), con el mismo
    // interruptor que la tecla. Sale cuando CONTINUE sale (pause.option_enabled(pause.continue)). Vale para todo trozo
    // FrontEnd/Scripts/PauseMenu/<modo>/PauseMenu.lua que tenga esa forma (extensionLua).
    constexpr const char *kExtensionPausa = R"lua(
if PauseMenu and PauseMenu.mMenuTextItems and PauseMenu.mMenuCallbackItems and PauseMenu.actionContinue then
  local k = nil
  for n = 1, table.getn(PauseMenu.mMenuTextItems) do
    if PauseMenu.mMenuTextItems[n] == "PAU_RPLYS" then k = n end
  end
  if k and table.getn(PauseMenu.mMenuCallbackItems) >= k then
    local titulo = "PHOTO MODE"
    if game.language() == game.spanish then titulo = "MODO FOTO" end
    table.insert(PauseMenu.mMenuTextItems, k + 1, titulo)
    table.insert(PauseMenu.mMenuCallbackItems, k + 1, function(m)
      os.getenv("ps2x:modo_foto", 1)
      return PauseMenu.actionContinue(m)
    end)
    if PauseMenu.mMenuValidators and table.getn(PauseMenu.mMenuValidators) >= k then
      table.insert(PauseMenu.mMenuValidators, k + 1, function() return pause.option_enabled(pause.continue) end)
    end
  end
end
)lua";

    // Nativizacion T10: el menu de pausa de la repeticion (START durante REPLAY; sus tablas se llenan en onInit) gana,
    // detras de CONTINUAR LA REPETICION (PAU_CR), MODO FOTO (como en la pausa de la carrera) y CONTROLES, que explica
    // con el aviso del juego (gConfirm, un solo boton) los mandos de la repeticion: L2, R2 y SELECT, o J, L y K.
    // Foto y repeticion T4: tambien los del juego (triangulo, la cruceta) y los botones de los sticks (R3 modo foto, L3
    // la barra de ayuda). Camaras y pilotos T2: el triangulo pasa por las de la repeticion y despues las de conduccion.
    constexpr const char *kExtensionRepeticion = R"lua(
if ReplayMenu and ReplayMenu.onInit and ReplayMenu.actionContinue then
  local iniciar = ReplayMenu.onInit
  ReplayMenu.onInit = function(self)
    iniciar(self)
    local textos, llamadas = ReplayMenu.mMenuTextItems, ReplayMenu.mMenuCallbackItems
    if not (textos and llamadas and textos[1] == "PAU_CR" and table.getn(llamadas) >= 1) then return end
    local foto, controles = "PHOTO MODE", "CONTROLS"
    local texto = "Triangle: camera, the replay's and then the driving ones. D-pad left and right: another car; " ..
      "up and down: the car ahead or behind. " ..
      "Hold R2: fast-forward x2 and, after a second, x4. Hold L2: rewind (it jumps there when let go). " ..
      "SELECT: pause and resume. R3: photo mode. L3: show or hide the help. Keyboard: L, J and K."
    if game.language() == game.spanish then
      foto, controles = "MODO FOTO", "CONTROLES"
      texto = "Tri\195\161ngulo: c\195\161mara, las de la repetici\195\179n y despu\195\169s las de conducci\195\179n. " ..
        "Cruceta izquierda y derecha: otro coche; arriba y abajo: el de delante o " ..
        "el de detr\195\161s. R2 mantenido: adelante x2 y, tras un segundo, x4. L2 mantenido: atr\195\161s (al soltarlo " ..
        "salta all\195\173). SELECT: pausa y sigue. R3: modo foto. L3: muestra u oculta la ayuda. Teclado: L, J y K."
    end
    table.insert(textos, 2, foto)
    table.insert(llamadas, 2, function(m)
      os.getenv("ps2x:modo_foto", 1)
      return ReplayMenu.actionContinue(m)
    end)
    table.insert(textos, 3, controles)
    table.insert(llamadas, 3, function(m)
      gConfirm.start(texto, function()
        graph.pop()
        ReplayMenu.mOutcomeSent = false
      end, localised.string("MSC_OK"))
    end)
    local validos = ReplayMenu.mMenuValidators
    if validos and table.getn(validos) >= 1 then
      table.insert(validos, 2, validos[1])
      table.insert(validos, 3, function() return true end)
    end
  end
end
)lua";

    // DISPLAY SETTINGS: con "ps2x:pagina" = 1, customActivate (menu principal) y pauseCustomActivate (pausa) ponen
    // nuestras filas en lugar de las del juego, con nuestro titulo. Las filas se crean al abrir la pagina y se sueltan
    // al salir (en la carrera el monton de Lua va casi lleno). hasChanged = false: no cuentan en getNumChanged, asi que
    // CONFIRM no pide guardar el perfil en la tarjeta por ellas (se guardan en opciones.ini). En la pausa van menos filas
    // (cabe un dialogo mas pequeno y la memoria es justa).
    // PANTALLA PC (pagina 3, Nativizacion T5): su fila VALORES POR DEFECTO los pone en el anfitrion y la pagina se
    // rehace tras la pulsacion (onItemIncrease/onItemDecrease) para mostrarlos; como las filas nuevas toman los
    // valores por defecto como los de antes, Atras y RESTABL. (onBack, onDeSelect) vuelven ademas a los que habia al
    // abrir la pagina ("ps2x:recordar" / "ps2x:descartar"). JUEGO EN RED PC (pagina 4, Nativizacion T6) igual, y ademas
    // se rehace cuando cambia el anfitrion o cuando el anfitrion no deja cambiar una fila. GRAFICOS PC y AJUSTES PC
    // (Nativizacion T7) igual en el menu principal; la pausa no lleva esa fila ni recuerda nada: las filas de mas de
    // GRAFICOS PC van en un trozo aparte (=ps2x:graficos-pc), que la pausa tambien carga desde Nativizacion T10 (modo
    // de pantalla y volumen, sin VALORES POR DEFECTO).
    // OPCIONES PC (pagina 5, Foto y repeticion T2): la lista de las cuatro paginas del menu principal, con filas
    // ClassConfigMenuButton (como LOAD en DownloadContent: X elige la fila) y los botones SELEC./ATRAS. X abre la pagina
    // en el mismo nodo (se rehace como tras VALORES POR DEFECTO); en esas paginas, CONFIRM (guarda) y Atras/RESTABL.
    // (deshacen) vuelven a OPCIONES PC con el cursor en su fila, y Atras en OPCIONES PC vuelve a OPTIONS.
    // El Lua del juego no admite mas de 24 locales en la funcion principal de un trozo (contando ps2x_original de
    // textoConExtension): este lleva 23.
    constexpr const char *kExtensionPantalla = R"lua(
if Menu and Menu.customActivate and Menu.pauseCustomActivate and Menu.insertItem then
  local G = os.getenv
  local filas = nil
  local espaciado = nil
  local ultimaPagina = nil
  local rehacer = false
  local recordada = false
  local principal = false
  local filaPc = 1
  local botones = Menu.mButtonAdjustFunc
  local trozos = {[2] = "=ps2x:ajustes-pc", [3] = "=ps2x:pantalla-pc", [4] = "=ps2x:juego-en-red", [5] = "=ps2x:opciones-pc"}

  -- El menu recuerda la fila elegida; al pasar a una pagina con menos filas (de la 10 de AJUSTES PC a las 6 del
  -- juego) onActivate indexaria una fila que no existe: al cambiar de pagina se empieza por la primera.
  local function cambiarPagina(self, n)
    if n ~= ultimaPagina then
      self.mCurrentMenuItem = 1
      self.mPreviousMenuItem = 1
      ultimaPagina = n
    end
  end

  local function espanol()
    return game.language() == game.spanish
  end

  local function crearFilas(pausa, n)
    local es = espanol()
    local function T(a, b)
      if es then return a end
      return b
    end
    local lista = {}
    local function fila(clave, nombre, valores, ayuda)
      local item = ClassConfigMenuToggleItem()
      item.mTitle = nombre
      item.mValueMappings = valores
      item.setGetFunction = function(v)
        if v ~= nil then
          G("ps2x:" .. clave, v)
        else
          return G("ps2x:" .. clave)
        end
      end
      item.hasChanged = function() return false end
      table.insert(lista, {item, ayuda})
    end
    local onoff = {{"VAR_OFF", 0}, {"VAR_ON", 1}}
    if trozos[n] then
      -- AJUSTES PC, PANTALLA PC, JUEGO EN RED PC y OPCIONES PC: sus filas se compilan al abrir la pagina
      -- (=ps2x:ajustes-pc, =ps2x:pantalla-pc, =ps2x:juego-en-red, =ps2x:opciones-pc) y se sueltan al salir.
      local ajustes = loadstring("", trozos[n])
      if ajustes then
        setfenv(ajustes, getfenv(1))
        ajustes()(fila, T, onoff, lista, G, function() rehacer = true end)
      end
      return lista
    end
    fila("filtro", T("FILTRO DE IMAGEN", "IMAGE FILTER"),
      {{T("CERCANO", "NEAREST"), 0}, {T("BILINEAL", "BILINEAR"), 1}, {T("N\195\141TIDO", "SHARP"), 2}, {"FSR 1", 3}, {"CRT", 4}},
      T("C\195\179mo se ampl\195\173a la imagen: p\195\173xeles, suave, n\195\173tida, FSR 1 (ampl\195\173a y enfoca) o CRT (televisi\195\179n antigua).",
        "How the picture is scaled: pixels, smooth, sharp, FSR 1 (upscales and sharpens) or CRT (old TV look)."))
    fila("supersampling", "SUPERSAMPLING", {{"X1", 1}, {"X2", 2}, {"X4", 4}, {"X8", 8}, {"X16", 16}},
      T("Bordes m\195\161s suaves. Cuanto m\195\161s alto, m\195\161s trabajo para la tarjeta gr\195\161fica.",
        "Smoother edges. The higher, the more work for the graphics card."))
    fila("alta_resolucion", T("DOBLE RESOLUCI\195\147N", "DOUBLE RESOLUTION"), onoff,
      T("La imagen con el doble de ancho y de alto. Necesita supersampling X4 o m\195\161s.",
        "The picture at twice the width and height. Needs supersampling X4 or more."))
    fila("vsync", T("SINCRONIZACI\195\147N VERTICAL", "VERTICAL SYNC"), onoff,
      T("La imagen cambia al ritmo del monitor, sin cortes. El juego va a la misma velocidad.",
        "The picture changes with the monitor, without tearing. The game runs at the same speed."))
    fila("mostrar_fps", T("CONTADOR DE FPS", "FPS COUNTER"), onoff,
      T("Los fotogramas por segundo del juego (p. ej. 50 FPS), arriba a la derecha.",
        "The game's frames per second (e.g. 50 FPS), at the top right."))
    local graficos = loadstring("", "=ps2x:graficos-pc")
    if graficos then
      setfenv(graficos, getfenv(1))
      graficos()(fila, T, onoff, lista, G, function() rehacer = true end, pausa)
    end
    return lista
  end

  local function titulo(n)
    if n == 2 then
      if espanol() then return "AJUSTES PC" end
      return "PC SETTINGS"
    end
    if n == 3 then
      if espanol() then return "PANTALLA PC" end
      return "PC DISPLAY"
    end
    if n == 4 then
      if espanol() then return "JUEGO EN RED PC" end
      return "PC NETPLAY"
    end
    if n == 5 then
      if espanol() then return "OPCIONES PC" end
      return "PC OPTIONS"
    end
    if espanol() then return "GR\195\129FICOS PC" end
    return "PC GRAPHICS"
  end

  -- rehaciendo (tras VALORES POR DEFECTO): sin "ps2x:abrir", que tomaria la forma de la imagen y el modo de pantalla
  -- que el runtime aun no ha aplicado (lo hace en el siguiente fotograma) y desharia los valores por defecto.
  local function pagina(self, pausa, n, rehaciendo)
    if not rehaciendo and n ~= 5 then
      G("ps2x:abrir")
      if recordada then G("ps2x:recordar") end
    end
    filas = crearFilas(pausa, n)
    self.mMenuObjects = nil
    self.mMenuObjects = {}
    self.mHelpText = nil
    self.mHelpText = {}
    local i = 1
    for n = 1, table.getn(filas) do
      i = self:insertItem(i, filas[n][1], filas[n][2])
    end
    -- Caben 9 filas: con mas, mas juntas en el mismo alto (se deshace al volver a activar el menu).
    if table.getn(filas) > 9 then
      espaciado = espaciado or self.mMenuSpacingY
      self.mMenuSpacingY = espaciado * 8 / (table.getn(filas) - 1)
    end
    self:onActivate()
  end

  local activar = Menu.customActivate
  local activarPausa = Menu.pauseCustomActivate
  local desactivar = Menu.customDeactivate
  local seleccionar = onSelect
  -- Pagina n del menu principal (0: la del juego); fila, la del cursor al volver a OPCIONES PC.
  local function mostrar(self, n, fila)
    G("ps2x:pagina", n)
    filas = nil
    if espaciado then
      self.mMenuSpacingY = espaciado
      espaciado = nil
    end
    cambiarPagina(self, n)
    if fila then
      self.mCurrentMenuItem = fila
      self.mPreviousMenuItem = fila
    end
    rehacer = false
    recordada = n == 1 or n == 2 or n == 3 or n == 4
    principal = recordada or n == 5
    if n == 5 then self.mButtonAdjustFunc = gNavigationButtons.setSelectBack else self.mButtonAdjustFunc = botones end
    if principal then
      self.mMenuTitle = titulo(n)
      pagina(self, false, n)
      if self.mButtonAdjustFunc then self.mButtonAdjustFunc() end
      return
    end
    self.mMenuTitle = "MEN_DS"
    return activar(self)
  end
  Menu.customActivate = function(self)
    return mostrar(self, G("ps2x:pagina"))
  end
  Menu.pauseCustomActivate = function(self)
    filas = nil
    recordada = false
    principal = false
    cambiarPagina(self, G("ps2x:pagina"))
    if G("ps2x:pagina") == 1 then
      self.mMenuTitle = titulo(1)
      if Dialog then Dialog.mDialogTitle = self.mMenuTitle end
      return pagina(self, true, 1)
    end
    self.mMenuTitle = "MEN_DS"
    if Dialog then Dialog.mDialogTitle = localised.string("MEN_DS") end
    return activarPausa(self)
  end
  if desactivar then
    Menu.customDeactivate = function(self)
      desactivar(self)
      filas = nil
    end
  end
  if seleccionar then
    onSelect = function(p)
      -- Una fila con una funcion en la tercera columna (CONTRASENA de JUEGO EN RED PC) hace lo suyo con X; le pasa
      -- como rehacer la pagina al volver (al cerrar el teclado del juego no se llama a customActivate).
      if p == 1 and filas ~= nil and principal then
        local f = filas[Menu.mCurrentMenuItem]
        if f and type(f[3]) == "function" then
          return f[3](function()
            pagina(Menu, false, ultimaPagina, true)
            if Menu.mButtonAdjustFunc then Menu.mButtonAdjustFunc() end
          end)
        end
      end
      if p == 1 and filas ~= nil and principal and ultimaPagina == 5 then
        local f = filas[Menu.mCurrentMenuItem]
        if f and f[3] then
          sfx.play(sfx.accept)
          filaPc = Menu.mCurrentMenuItem
          mostrar(Menu, f[3])
        end
        return
      end
      if p == 1 and filas ~= nil then G("ps2x:guardar") end
      if p == 1 and filas ~= nil and principal then
        sfx.play(sfx.accept)
        return mostrar(Menu, 5, filaPc)
      end
      return seleccionar(p)
    end
  end
  local function trasCambiar(original)
    return function(p)
      local r = original(p)
      if rehacer and filas ~= nil then
        rehacer = false
        pagina(Menu, false, ultimaPagina, true)
      end
      return r
    end
  end
  local function trasDeshacer(original, sonido)
    return function(p)
      if p == 1 and filas ~= nil and recordada then
        Menu:revertValues()
        G("ps2x:descartar")
        sfx.play(sfx[sonido])
        return mostrar(Menu, 5, filaPc)
      end
      return original(p)
    end
  end
  if onItemIncrease and onItemDecrease then
    onItemIncrease = trasCambiar(onItemIncrease)
    onItemDecrease = trasCambiar(onItemDecrease)
  end
  if onBack then onBack = trasDeshacer(onBack, "cancel") end
  if onDeSelect then onDeSelect = trasDeshacer(onDeSelect, "back") end
end
)lua";

    // OPCIONES PC (Foto y repeticion T2): las filas de la pagina 5, una por pagina del PC; la tercera columna de cada
    // fila es la pagina que abre X (kExtensionPantalla). DisplaySettings no incluye ClassConfigMenuButton.lua: cada
    // fila es lo que crea ClassConfigMenuButton(), los metodos de gClassConfigMenuButton (global, de NavigationBars),
    // con setItemPositionX vacio como en esa clase.
    constexpr const char *kTrozoOpcionesPc = "opciones-pc";
    constexpr const char *kPaginaOpcionesPc = R"lua(
return function(fila, T, onoff, lista)
  local function boton(n, nombre, ayuda)
    local item = {}
    for k, f in pairs(gClassConfigMenuButton) do item[k] = f end
    item.setItemPositionX = function() end
    item.mTitle = nombre
    item.hasChanged = function() return false end
    table.insert(lista, {item, ayuda, n})
  end
  boton(1, T("GR\195\129FICOS PC", "PC GRAPHICS"),
    T("Gr\195\161ficos del PC: filtro, supersampling, sincronizaci\195\179n vertical, contador de FPS, pantalla, volumen y detalle de los coches.",
      "PC graphics: image filter, supersampling, vertical sync, FPS counter, screen mode, volume and car detail."))
  boton(3, T("PANTALLA PC", "PC DISPLAY"),
    T("Pantalla del PC: forma de la imagen, tama\195\177o de la ventana y dibujo de los gr\195\161ficos (al reiniciar).",
      "PC display: picture shape, window size and graphics renderer (on restart)."))
  boton(2, T("AJUSTES PC", "PC SETTINGS"),
    T("Ajustes del PC: v\195\173deos de inicio, latencia y sonido mono, vibraci\195\179n, daltonismo y tecla de foto.",
      "PC settings: intro videos, audio latency, mono sound, vibration, colour blindness filter and photo key."))
  boton(4, T("JUEGO EN RED PC", "PC NETPLAY"),
    T("Juego en red del PC: anfitri\195\179n, puerto, calidad de la imagen, direcciones p\195\186blicas, estado y contrase\195\177a.",
      "PC netplay: host, port, picture quality, public addresses, state and password."))
end
)lua";

    // AJUSTES PC (fase 5, tanda A): las filas de la pagina 2 de DISPLAY SETTINGS, en un trozo aparte que la pagina
    // pide al abrirse (loadstring("", "=ps2x:ajustes-pc"), servido por servirOpcionesJuego): su codigo y sus textos
    // solo ocupan el monton de Lua mientras la pagina esta abierta (en la carrera va casi lleno). Devuelve una funcion
    // que anade las filas con los ayudantes de crearFilas. Solo en el menu principal: la pausa no la ofrece.
    // La fila JUEGO EN RED (red_anfitrion, fase 5 T15) paso a la pagina JUEGO EN RED PC (Nativizacion T6). VALORES POR
    // DEFECTO (Nativizacion T7), como en PANTALLA PC: los de las once filas ("ps2x:por_defecto") y rehace.
    constexpr const char *kTrozoAjustesPc = "ajustes-pc";
    constexpr const char *kPaginaAjustesPc = R"lua(
return function(fila, T, onoff, lista, G, rehacer)
  fila("saltar_intro", T("SALTAR V\195\141DEOS DE INICIO", "SKIP INTRO VIDEOS"), onoff,
    T("La presentaci\195\179n y la demostraci\195\179n de la pantalla de t\195\173tulo se cortan a los dos segundos.",
      "The intro and the title screen's demo stop after two seconds."))
  fila("latencia_audio", T("LATENCIA DEL SONIDO", "AUDIO LATENCY"),
    {{T("AUTOM\195\129TICA", "AUTOMATIC"), 0}, {T("BAJA", "LOW"), 1}, {T("ALTA", "HIGH"), 2}},
    T("Autom\195\161tica: de 43 a 128 ms seg\195\186n el PC. Baja: 43 ms. Alta: 128 ms, para un PC lento.",
      "Automatic: 43 to 128 ms as the PC needs. Low: 43 ms. High: 128 ms, for a slow PC."))
  fila("audio_mono", T("SALIDA DE SONIDO", "SOUND OUTPUT"), {{T("EST\195\137REO", "STEREO"), 0}, {"MONO", 1}},
    T("Mono: los dos canales juntos, para un solo altavoz o un solo o\195\173do.",
      "Mono: both channels together, for a single speaker or one ear."))
  local vibracion = ClassSlider()
  vibracion.mTitle = T("VIBRACI\195\147N", "VIBRATION")
  vibracion.setGetFunction = function(v)
    if v ~= nil then
      G("ps2x:vibracion", math.floor(v * 100 + 0.5))
    else
      local actual = G("ps2x:vibracion")
      if actual then return actual / 100 end
    end
  end
  vibracion.hasChanged = function() return false end
  table.insert(lista, {vibracion, T("La fuerza de la vibraci\195\179n del mando y del volante. Al m\195\173nimo, sin vibraci\195\179n.",
    "How strong the pad and wheel vibration is. At the minimum, no vibration.")})
  fila("daltonismo", T("FILTRO PARA DALTONISMO", "COLOUR BLINDNESS FILTER"),
    {{"VAR_OFF", 0}, {"PROTANOPIA", 1}, {"DEUTERANOPIA", 2}, {"TRITANOPIA", 3}},
    T("Mueve los colores que se confunden (rojo, verde o azul) a otros que se distinguen.",
      "Moves the colours that look alike (red, green or blue) to others that can be told apart."))
  fila("tecla_foto", T("TECLA DE FOTO", "PHOTO KEY"), {{"F12", 0}, {"F9", 1}, {"F8", 2}, {"VAR_OFF", 3}},
    T("Guarda la imagen del juego a su resoluci\195\179n interna en la carpeta capturas.",
      "Saves the game's picture at its internal resolution in the capturas folder."))
  fila("hud", T("HUD DE LA CARRERA", "RACE HUD"), {{T("COMPLETO", "FULL"), 0}, {T("M\195\141NIMO", "MINIMAL"), 1}, {T("OCULTO", "HIDDEN"), 2}},
    T("M\195\173nimo: posiciones, banderas, velocidad y marcha. Oculto: nada, para fotos. La tecla del HUD lo cambia en carrera.",
      "Minimal: positions, flags, speed and gear. Hidden: nothing, for photos. The HUD key changes it in a race."))
  fila("tecla_hud", T("TECLA DEL HUD", "HUD KEY"), {{"H", 0}, {"F7", 1}, {"F6", 2}, {"VAR_OFF", 3}},
    T("Cambia el HUD en carrera: completo, m\195\173nimo, oculto y otra vez completo.",
      "Changes the HUD in a race: full, minimal, hidden and full again."))
  fila("cargas_rapidas", T("CARGAS R\195\129PIDAS", "FAST LOADING"), onoff,
    T("Las pantallas de carga acaban en cuanto el juego ha cargado. Los fundidos y los consejos se ven igual.",
      "Loading screens end as soon as the game has loaded. Fades and tips look the same."))
  fila("fov", T("CAMPO DE VISI\195\147N", "FIELD OF VIEW"), {{"NORMAL", 0}, {"+5 %", 5}, {"+10 %", 10}, {"+15 %", 15},
    {"+20 %", 20}, {"+25 %", 25}, {"+30 %", 30}},
    T("Las c\195\161maras de la carrera m\195\161s abiertas: se ve m\195\161s a los lados. El HUD y los men\195\186s no cambian.",
      "Wider race cameras: you see more to the sides. The HUD and the menus do not change."))
  fila("tecla_modo_foto", T("TECLA DEL MODO FOTO", "PHOTO MODE KEY"), {{"F8", 0}, {"F5", 1}, {"P", 2}, {"VAR_OFF", 3}},
    T("En carrera congela el juego y deja mover la c\195\161mara: W S A D, R F, flechas, Q E, Z X o los sticks.",
      "In a race it freezes the game and frees the camera: W S A D, R F, arrows, Q E, Z X or the sticks."))
  local aplicar = T("APLICAR", "APPLY")
  local defecto = ClassConfigMenuToggleItem()
  defecto.mTitle = T("VALORES POR DEFECTO", "DEFAULT VALUES")
  defecto.mValueMappings = {{aplicar, 1}, {aplicar, 0}, {aplicar, 2}}
  defecto.setGetFunction = function(v)
    if v == nil then return 0 end
    if v ~= 0 then
      G("ps2x:por_defecto")
      rehacer()
    end
  end
  defecto.hasChanged = function() return false end
  table.insert(lista, {defecto, T("Izquierda o derecha: las filas de esta p\195\161gina vuelven a sus valores por defecto. CONFIRM los guarda.",
    "Left or right: the rows of this page go back to their default values. CONFIRM saves them.")})
end
)lua";

    // GRAFICOS PC (Nativizacion T7): modo de pantalla, volumen, detalle de los coches (Lejania T4), distancia del
    // escenario (Lejania T5) y VALORES POR DEFECTO (que pone los de las nueve filas), en un trozo que la pagina pide al
    // abrirse (=ps2x:graficos-pc): solo ocupan el monton de Lua con la pagina abierta. En la pausa (Nativizacion T10,
    // con el monton de la carrera de 4 MB en la RAM alta) van las cuatro primeras; VALORES POR DEFECTO no, porque rehace
    // la pagina del menu principal.
    constexpr const char *kTrozoGraficosPc = "graficos-pc";
    constexpr const char *kPaginaGraficosPc = R"lua(
return function(fila, T, onoff, lista, G, rehacer, pausa)
  fila("pantalla", T("MODO DE PANTALLA", "SCREEN MODE"),
    {{T("VENTANA", "WINDOWED"), 0}, {T("SIN BORDES", "BORDERLESS"), 1}, {T("COMPLETA", "FULL SCREEN"), 2}},
    T("Ventana, ventana sin bordes del tama\195\177o del monitor o pantalla completa.",
      "A window, a borderless window the size of the monitor or full screen."))
  local volumen = ClassSlider()
  volumen.mTitle = T("VOLUMEN GENERAL", "MASTER VOLUME")
  volumen.setGetFunction = function(v)
    if v ~= nil then
      G("ps2x:volumen", math.floor(v * 100 + 0.5))
    else
      local actual = G("ps2x:volumen")
      if actual then return actual / 100 end
    end
  end
  volumen.hasChanged = function() return false end
  table.insert(lista, {volumen, T("El volumen de todo el sonido del juego.", "The volume of all the game's sound.")})
  fila("detalle_coches", T("DETALLE DE LOS COCHES", "CAR DETAIL"),
    {{T("REDUCIDO", "REDUCED"), 0}, {T("COMO EN PS2", "AS ON PS2"), 1}, {T("ALTO", "HIGH"), 2},
     {T("M\195\129XIMO", "MAXIMUM"), 3}},
    T("Como en PS2: el detalle de la consola. Reducido: menos. Alto: al doble de distancia. M\195\161ximo: todos como el del jugador.",
      "As on PS2: the console's detail. Reduced: less. High: twice as far. Maximum: every car like the player's."))
  fila("distancia_circuito", T("DISTANCIA DEL ESCENARIO", "SCENERY DISTANCE"),
    {{T("COMO EN PS2", "AS ON PS2"), 100}, {T("LEJOS", "FAR"), 150}, {T("MUY LEJOS", "VERY FAR"), 200}},
    T("Hasta d\195\179nde se ven gradas, \195\161rboles y carteles: como en PS2, 1,5 veces m\195\161s lejos o el doble.",
      "How far stands, trees and boards are drawn: as on PS2, 1.5 times as far or twice as far."))
  if pausa then return end
  local aplicar = T("APLICAR", "APPLY")
  local defecto = ClassConfigMenuToggleItem()
  defecto.mTitle = T("VALORES POR DEFECTO", "DEFAULT VALUES")
  defecto.mValueMappings = {{aplicar, 1}, {aplicar, 0}, {aplicar, 2}}
  defecto.setGetFunction = function(v)
    if v == nil then return 0 end
    if v ~= 0 then
      G("ps2x:por_defecto")
      rehacer()
    end
  end
  defecto.hasChanged = function() return false end
  table.insert(lista, {defecto, T("Izquierda o derecha: las filas de esta p\195\161gina vuelven a sus valores por defecto. CONFIRM los guarda.",
    "Left or right: the rows of this page go back to their default values. CONFIRM saves them.")})
end
)lua";

    // PANTALLA PC (Nativizacion T5): las opciones de la pantalla que antes solo estaban en el F2 y en opciones.ini, en
    // un trozo que la pagina pide al abrirse (=ps2x:pantalla-pc), como AJUSTES PC. Solo en el menu principal. Los
    // tamanos de la ventana los da el anfitrion ("ps2x:ventanas", "ps2x:ventana_ancho"/"_alto", i; la fila es la
    // posicion en esa lista) y gs dice "(AL REINICIAR)" en el valor que no es el que esta en uso ("ps2x:gs_en_uso").
    // VALORES POR DEFECTO: cualquier pulsacion a los lados pone los de las tres filas ("ps2x:por_defecto") y rehace.
    constexpr const char *kTrozoPantallaPc = "pantalla-pc";
    constexpr const char *kPaginaPantallaPc = R"lua(
return function(fila, T, onoff, lista, G, rehacer)
  fila("aspecto", T("FORMA DE LA IMAGEN", "PICTURE SHAPE"),
    {{T("AUTOM\195\129TICA", "AUTOMATIC"), 0}, {"4:3", 1}, {"16:9", 2}, {T("P\195\141XELES CUADRADOS", "SQUARE PIXELS"), 3}},
    T("Autom\195\161tica: la de OPCIONES DE PANTALLA (4:3 o panor\195\161mica). P\195\173xeles cuadrados: sin ajustar. F10 la cambia jugando.",
      "Automatic: the one in DISPLAY SETTINGS (4:3 or widescreen). Square pixels: not adjusted. F10 changes it while playing."))
  local tamanos = {}
  for i = 0, (G("ps2x:ventanas") or 0) - 1 do
    table.insert(tamanos, {G("ps2x:ventana_ancho", i) .. " x " .. G("ps2x:ventana_alto", i), i})
  end
  if table.getn(tamanos) > 0 then
    fila("tamano_ventana", T("TAMA\195\145O DE LA VENTANA", "WINDOW SIZE"), tamanos,
      T("En modo ventana (MODO DE PANTALLA, en GR\195\129FICOS PC). Si no cabe en el escritorio, se ajusta a \195\169l.",
        "In windowed mode (SCREEN MODE, in PC GRAPHICS). If it does not fit on the desktop, it is fitted to it."))
  end
  local reiniciar = T(" (AL REINICIAR)", " (ON RESTART)")
  local cpu, gpu = "CPU", "GPU"
  if G("ps2x:gs_en_uso") == 1 then cpu = cpu .. reiniciar else gpu = gpu .. reiniciar end
  fila("gs", T("DIBUJO DE LOS GR\195\129FICOS", "GRAPHICS RENDERER"), {{cpu, 0}, {gpu, 1}},
    T("GPU: en la tarjeta gr\195\161fica (Vulkan), lo normal. CPU: modo Software, m\195\161s lento. Se aplica al reiniciar el juego.",
      "GPU: on the graphics card (Vulkan), the usual. CPU: Software mode, slower. It takes effect when the game restarts."))
  local aplicar = T("APLICAR", "APPLY")
  local defecto = ClassConfigMenuToggleItem()
  defecto.mTitle = T("VALORES POR DEFECTO", "DEFAULT VALUES")
  defecto.mValueMappings = {{aplicar, 1}, {aplicar, 0}, {aplicar, 2}}
  defecto.setGetFunction = function(v)
    if v == nil then return 0 end
    if v ~= 0 then
      G("ps2x:por_defecto")
      rehacer()
    end
  end
  defecto.hasChanged = function() return false end
  table.insert(lista, {defecto, T("Izquierda o derecha: las tres filas de esta p\195\161gina vuelven a sus valores por defecto. CONFIRM los guarda.",
    "Left or right: the three rows of this page go back to their default values. CONFIRM saves them.")})
end
)lua";

    // JUEGO EN RED PC (Nativizacion T6): el anfitrion desde el juego (red_anfitrion, que antes estaba en AJUSTES PC) y
    // las opciones de red que antes solo estaban en el F2 y en opciones.ini, en un trozo que la pagina pide al abrirse
    // (=ps2x:juego-en-red). Solo en el menu principal. CONTRASENA (Nativizacion T16): X abre el teclado del juego
    // (Keyboard.fxg, en estrellas, de 8 a 32 caracteres); al confirmar, el texto va al anfitrion caracter a caracter
    // ("ps2x:red_clave_empezar", "ps2x:red_clave_letra", codigo, "ps2x:red_clave_poner"; asi no queda ningun trozo
    // como clave en el monton de Lua) y la variable se suelta (el texto sigue en el monton hasta que pase el GC);
    // Cancelar no cambia nada. Izquierda o derecha la deja vacia ("ps2x:red_clave_azar": una al azar al encender, como
    // el F2 de antes). La fila la muestra ("ps2x:red_clave_ver", caracter a caracter): la del anfitrion encendido o
    // la escrita; con el anfitrion encendido no cambia. Nunca se guarda. Con el anfitrion
    // encendido o pedido, el anfitrion no deja cambiar el puerto, la calidad ni las direcciones publicas (se aplican al
    // empezar): si una fila no cambia, la pagina se rehace con su valor. ESTADO y DIRECCION son de solo lectura
    // ("ps2x:red_estado" toma el estado; "ps2x:red_direcciones", "ps2x:red_octeto", i, "ps2x:red_puerto_activo"; el
    // motivo del fallo y la direccion del invitado, "ps2x:red_motivo" y "ps2x:red_invitado", caracter a caracter); a los
    // lados, se actualizan. Encender o apagar el anfitrion rehace la pagina.
    constexpr const char *kTrozoJuegoEnRed = "juego-en-red";
    constexpr const char *kPaginaJuegoEnRed = R"lua(
return function(fila, T, onoff, lista, G, rehacer)
  local fijas = G("ps2x:red_anfitrion") == 1
  local aviso = ""
  if fijas then aviso = T(" Fija mientras el anfitri\195\179n est\195\161 encendido.", " Fixed while hosting is on.") end
  local function red(clave, nombre, valores, ayuda)
    fila(clave, nombre, valores, ayuda)
    local item = lista[table.getn(lista)][1]
    local poner = item.setGetFunction
    item.setGetFunction = function(v)
      local r = poner(v)
      if v ~= nil and (clave == "red_anfitrion" or G("ps2x:" .. clave) ~= v) then rehacer() end
      return r
    end
  end
  local function solo(nombre, texto, ayuda)
    local item = ClassConfigMenuToggleItem()
    item.mTitle = nombre
    item.mValueMappings = {{texto, 1}, {texto, 0}, {texto, 2}}
    item.setGetFunction = function(v)
      if v == nil then return 0 end
      if v ~= 0 then rehacer() end
    end
    item.hasChanged = function() return false end
    table.insert(lista, {item, ayuda})
  end
  red("red_anfitrion", T("ANFITRI\195\147N", "HOST"), {{T("APAGADO", "OFF"), 0}, {T("ENCENDIDO", "ON"), 1}},
    T("Otro PC se une como jugador 2 a pantalla partida (SPLIT SCREEN). Puerto, calidad y direcciones se fijan al encenderlo.",
      "Another PC joins as player 2 in split screen (SPLIT SCREEN). Port, quality and addresses are fixed when it starts."))
  local puertos = {}
  local actual = G("ps2x:red_puerto") or 50650
  for p = 50650, 50655 do
    if actual > 0 and actual < p then
      table.insert(puertos, {"" .. actual, actual})
      actual = 0
    elseif actual == p then
      actual = 0
    end
    table.insert(puertos, {"" .. p, p})
  end
  if actual > 0 then table.insert(puertos, {"" .. actual, actual}) end
  red("red_puerto", T("PUERTO", "PORT"), puertos,
    T("El puerto TCP en el que este PC espera al invitado; el invitado escribe el mismo.",
      "The TCP port this PC waits on for the guest; the guest types the same one.") .. aviso)
  red("red_calidad", T("CALIDAD DE LA IMAGEN", "PICTURE QUALITY"),
    {{T("BAJA", "LOW"), 0}, {T("MEDIA", "MEDIUM"), 1}, {T("ALTA", "HIGH"), 2}},
    T("La imagen que recibe el invitado: baja 4 Mbit/s, media 8, alta 15. Si le va a saltos, b\195\161jala.",
      "The picture the guest gets: low 4 Mbit/s, medium 8, high 15. If it stutters, lower it.") .. aviso)
  red("red_publicas", T("DIRECCIONES P\195\154BLICAS", "PUBLIC ADDRESSES"), {{"NO", 0}, {T("S\195\141", "YES"), 1}},
    T("No: solo invitados de la red local o de una VPN privada (Tailscale, ZeroTier). S\195\173: tambi\195\169n Hamachi, Radmin VPN o Internet.",
      "No: only guests on the local network or a private VPN (Tailscale, ZeroTier). Yes: also Hamachi, Radmin VPN or the Internet.") .. aviso)
  local estado = G("ps2x:red_estado") or 0
  local puerto = G("ps2x:red_puerto_activo") or 0
  local direccion, todas = "-", ""
  for i = 0, (G("ps2x:red_direcciones") or 0) - 1 do
    local d = G("ps2x:red_octeto", 4 * i) .. "." .. G("ps2x:red_octeto", 4 * i + 1) .. "." ..
      G("ps2x:red_octeto", 4 * i + 2) .. "." .. G("ps2x:red_octeto", 4 * i + 3)
    if i == 0 then direccion = d .. ":" .. puerto else todas = todas .. T(" o ", " or ") end
    todas = todas .. d
  end
  local actualizar = T(" Izquierda o derecha: actualizar.", " Left or right: refresh.")
  local estados = {
    {T("APAGADO", "OFF"), T("Enciende ANFITRI\195\147N para que otro PC se una.", "Turn HOST on so another PC can join.")},
    {T("EMPEZANDO", "STARTING"), T("El anfitri\195\179n est\195\161 empezando.", "The host is starting.")},
    {T("ESPERANDO AL INVITADO", "WAITING FOR THE GUEST"),
      T("El invitado abre Jugar en red - Invitado y escribe la direcci\195\179n, el puerto y la contrase\195\177a.",
        "The guest opens Netplay - Guest and types the address, the port and the password.")},
    {T("INVITADO CONECTADO", "GUEST CONNECTED"), T("El invitado es el jugador 2.", "The guest is player 2.")},
    {T("NO SE PUDO EMPEZAR", "COULD NOT START"), T("El motivo sale arriba del juego.", "The reason is shown at the top of the game.")},
  }
  -- En un bloque: la funcion principal del trozo no admite mas de 24 locales a la vez.
  do
    local function texto(clave)
      local s = ""
      for i = 0, (G("ps2x:" .. clave) or 0) - 1 do s = s .. string.char(G("ps2x:" .. clave, i) or 63) end
      return s
    end
    local s = texto("red_invitado")
    if s ~= "" then
      estados[4][2] = T("El invitado, desde ", "The guest, from ") .. s .. T(", es el jugador 2.", ", is player 2.")
    end
    s = texto("red_motivo")
    if s ~= "" then
      if string.sub(s, -1) ~= "." then s = s .. "." end
      estados[5][2] = T("Motivo: ", "Reason: ") .. s
    end
  end
  local e = estados[estado + 1] or estados[1]
  solo(T("ESTADO", "STATE"), e[1], e[2] .. actualizar)
  local donde = T("Con el anfitri\195\179n encendido, d\195\179nde se conecta el invitado.", "With the host on, where the guest connects.")
  if todas ~= "" then donde = T("Este PC: ", "This PC: ") .. todas .. T(", puerto ", ", port ") .. puerto .. "." end
  solo(T("DIRECCI\195\147N", "ADDRESS"), direccion, donde .. actualizar)
  -- En un bloque y con funciones: la funcion principal del trozo no admite mas de 24 locales a la vez.
  do
    local item = ClassConfigMenuToggleItem()
    item.mTitle = T("CONTRASE\195\145A", "PASSWORD")
    item.mValueMappings = (function()
      local codigos = {}
      for i = 0, (G("ps2x:red_clave_ver") or 0) - 1 do codigos[i + 1] = G("ps2x:red_clave_ver", i) or 63 end
      local v = T("AL AZAR", "RANDOM")
      if table.getn(codigos) > 0 then v = string.char(unpack(codigos)) end
      return {{v, 1}, {v, 0}, {v, 2}}
    end)()
    item.setGetFunction = function(v)
      if v == nil then return 0 end
      if v ~= 0 then
        if not fijas then G("ps2x:red_clave_azar") end
        rehacer()
      end
    end
    item.hasChanged = function() return false end
    local ayuda = T("X: escribirla con el teclado (de 8 a 32 caracteres). Izquierda o derecha: una al azar al encender. Nunca se guarda.",
      "X: type it on the keyboard (8 to 32 characters). Left or right: a random one when it starts. It is never saved.")
    if fijas then
      ayuda = T("La que escribe el invitado. Fija mientras el anfitri\195\179n est\195\161 encendido. Nunca se guarda.",
        "The one the guest types. Fixed while hosting is on. It is never saved.")
    end
    table.insert(lista, {item, ayuda, function(volver)
      if G("ps2x:red_anfitrion") == 1 then
        sfx.play(sfx.cancel)
        return
      end
      sfx.play(sfx.accept)
      -- El titulo pasa por localised.string, que devuelve tal cual lo que no conoce (no tiene uno de contrasena).
      gPopupKeyboard.titleString = item.mTitle
      gPopupKeyboard.charMin = 8
      gPopupKeyboard.charLimit = 32
      gPopupKeyboard.passwordStars = 1
      gPopupKeyboard.capsOnly = nil
      gPopupKeyboard.NameSelected = nil
      gPopupKeyboard.KeepContent = nil
      gPopupKeyboard.callback = nil
      -- El grafo de DisplaySettings no tiene las salidas del teclado: se toman aqui mientras esta abierto.
      local salida = graph.outcome
      graph.outcome = function(...)
        local o = arg[1]
        if o ~= "ConfirmKeyboardChange" and o ~= "CancelKeyboardChange" then return salida(unpack(arg)) end
        graph.outcome = salida
        if o == "ConfirmKeyboardChange" then
          G("ps2x:red_clave_empezar")
          local s = vKeyboardMgr.getFinalStringASCII() or ""
          for i = 1, string.len(s) do G("ps2x:red_clave_letra", string.byte(s, i)) end
          s = nil
          G("ps2x:red_clave_poner")
        end
        -- Sin NameSelected el juego no copia el texto en NameString/NameStringASCII (tablas de los perfiles): no se tocan.
        vKeyboardMgr.setString("")
        graph.pop()
        -- onDeactivate deja passwordStars = 0; se quita para que el siguiente uso (nombre del perfil) no herede nada.
        gPopupKeyboard.passwordStars = nil
        gPopupKeyboard.capsOnly = nil
        volver()
      end
      graph.push("FrontEnd/Graph/Keyboard.fxg")
    end})
  end
  local aplicar = T("APLICAR", "APPLY")
  local defecto = ClassConfigMenuToggleItem()
  defecto.mTitle = T("VALORES POR DEFECTO", "DEFAULT VALUES")
  defecto.mValueMappings = {{aplicar, 1}, {aplicar, 0}, {aplicar, 2}}
  defecto.setGetFunction = function(v)
    if v == nil then return 0 end
    if v ~= 0 then
      G("ps2x:por_defecto")
      rehacer()
    end
  end
  defecto.hasChanged = function() return false end
  table.insert(lista, {defecto, T("Izquierda o derecha: puerto, calidad y direcciones p\195\186blicas vuelven a sus valores por defecto. CONFIRM los guarda.",
    "Left or right: port, quality and public addresses go back to their default values. CONFIRM saves them.") .. aviso})
end
)lua";

    // RACE OPTIONS (fase 5, T10): la fila RACE DISTANCE (ClassSlider; el juego ofrece 3 vueltas, 10, 20, 30, 50, 75 %
    // y la carrera entera) pasa a elegir vueltas, de 1 a las del Gran Premio del circuito, con el texto del juego
    // (MSC_3LAPS, "3 LAPS", con otro numero) y una ayuda con las vueltas del Gran Premio. Se guarda lo mismo que guarda el juego: la distancia (game.racedistance
    // del modo, en el perfil) que da esas vueltas (ps2_race_laps.h, por el puente: "ps2x:vueltas_gp", circuito -> sus
    // vueltas; "ps2x:vueltas_de", distancia -> vueltas; "ps2x:vueltas_distancia", vueltas -> distancia). Combustible y
    // desgaste como el juego (entera: realista; si no, a escala), y sin efecto por debajo del 10 % (lo que el juego hace
    // con 3 vueltas). Si no se conocen las vueltas del circuito, la fila del juego tal cual.
    constexpr const char *kExtensionCarrera = R"lua(
if Menu and Menu.mRaceDistance and Menu.mRaceDistance.addValue then
  local original = Menu.mRaceDistance.addValue
  Menu.mRaceDistance.addValue = function(self, paso)
    local G = os.getenv
    local modo = game.optionsgamemode()
    local total = G("ps2x:vueltas_gp", game.track())
    if total == nil then return original(self, paso) end
    local n = G("ps2x:vueltas_de", game.racedistance(modo)) + paso
    local minimo = 1
    if game.downloadweather() == true and game.is_update_active() == true and game.climate(modo) == game.fromfile then
      minimo = G("ps2x:vueltas_de", 0.2)
    end
    if n < minimo then n = minimo elseif n > total then n = total elseif paso ~= 0 then sfx.play(sfx.move) end
    local d = G("ps2x:vueltas_distancia", n)
    game.racedistance(modo, d)
    if d < 0.1 then
      game.fuelandwear(modo, game.noeffect)
    elseif modo ~= game.optionsmode_splitscreen and modo ~= game.optionsmode_tvmode then
      if d == 1 then game.fuelandwear(modo, game.alwaysrealistic) else game.fuelandwear(modo, game.scaledwithdistance) end
    end
    local texto = localised.string("MSC_3LAPS")
    if n == 1 and string.sub(texto, -1) == "S" then texto = string.sub(texto, 1, -2) end
    self.mValueStr = (string.gsub(texto, "3", n))
    self:setSliderPercentage(d)
  end
  local insertar = Menu.insertItem
  Menu.insertItem = function(self, i, item, ayuda, submenu)
    local total = item == self.mRaceDistance and os.getenv("ps2x:vueltas_gp", game.track())
    if total then
      ayuda = "From 1 lap to the whole Grand Prix: " .. total .. " laps."
      if game.language() == game.spanish then ayuda = "De 1 vuelta a todo el Gran Premio: " .. total .. " vueltas." end
    end
    return insertar(self, i, item, ayuda, submenu)
  end
end
)lua";

    // Carrera directa (pruebas, F1_CARRERA_CIRCUITO, F1_CARRERA_PILOTO, F1_CARRERA_LISTA; carreraDirecta): tras la
    // activacion de PreRace (Menu.customActivate, que devuelve a 1 un circuito > 18 y a 'falonso' un piloto clasico),
    // en carrera rapida o Time Trial, game.track(n) y el piloto del jugador 1 con su equipo, antes de START. El puente:
    // "ps2x:carrera_circuito" (numero o nil), "ps2x:carrera_piloto:<id>" (1 si es el pedido), "ps2x:carrera_lista"
    // y "ps2x:carrera_nota:<texto>" (lo anota).
    constexpr const char *kExtensionCarreraDirecta = R"lua(
if Menu and Menu.customActivate then
  local original = Menu.customActivate
  Menu.customActivate = function(self)
    original(self)
    local G = os.getenv
    if G("ps2x:carrera_lista") == 1 then
      for n = 1, 18 do G("ps2x:carrera_nota:circuito " .. n .. " " .. tostring(game.trackinfo(n).shortname)) end
      for t = 1, game.num_teams() do
        local e = game.teaminfo(t)
        G("ps2x:carrera_nota:equipo " .. t .. " " .. tostring(e.shortname) .. " " .. e.driver1 .. " " .. e.driver2)
      end
    end
    local modo = game.gamemode()
    if modo ~= game.mode_custom_race_1p and modo ~= game.mode_testdrive then return end
    local n = G("ps2x:carrera_circuito")
    if n then game.track(n) end
    local actual = game.playercurrent()
    for t = 1, game.num_teams() do
      local e = game.teaminfo(t)
      for _, id in ipairs({e.driver1, e.driver2}) do
        if G("ps2x:carrera_piloto:" .. id) == 1 then
          game.playercurrent(1)
          game.playerdriver(id)
          game.playerteam(game.driverinfo(id).team)
          game.playercurrent(actual)
        end
      end
    end
    game.playercurrent(1)
    local piloto, equipo = game.playerdriver(), game.playerteam()
    game.playercurrent(actual)
    G("ps2x:carrera_nota:modo " .. tostring(modo) .. " circuito " .. tostring(game.track()) .. " " ..
      tostring(piloto) .. " " .. tostring(equipo))
  end
end
)lua";

    bool puenteCarrera(const std::string &clave, bool &hay, float &resultado)
    {
        const CarreraDirecta &c = carreraDirecta();
        static uint32_t s_notas = 0u;
        if (clave == "carrera_circuito")
        {
            hay = c.circuito > 0;
            resultado = static_cast<float>(c.circuito);
        }
        else if (clave == "carrera_lista")
        {
            hay = true;
            resultado = c.lista ? 1.0f : 0.0f;
        }
        else if (clave.rfind("carrera_piloto:", 0) == 0)
        {
            // El id del juego (p. ej. 'falonso') o su final ('alonso'), sin mayusculas.
            const std::string id = minusculas(clave.substr(15));
            hay = !c.piloto.empty() && (id == c.piloto || (c.piloto.size() >= 3u && id.size() > c.piloto.size() &&
                                                           id.compare(id.size() - c.piloto.size(), c.piloto.size(), c.piloto) == 0));
            resultado = hay ? 1.0f : 0.0f;
            if (hay && s_notas < 40u)
            {
                ++s_notas;
                std::cout << "[f1-06:carrera] t=" << segundosDesdeArranque() << " piloto " << id << std::endl;
            }
        }
        else if (clave.rfind("carrera_nota:", 0) == 0)
        {
            hay = false;
            if (s_notas < 40u)
            {
                ++s_notas;
                std::cout << "[f1-06:carrera] t=" << segundosDesdeArranque() << " " << clave.substr(13) << std::endl;
            }
        }
        else
        {
            return false;
        }
        return true;
    }

    // Vueltas del puente de RACE OPTIONS (kExtensionCarrera); clave sin "ps2x:". false si no es una de ellas.
    int g_vueltasGranPremio = 0; // las del circuito de la ultima "vueltas_gp"
    bool puenteVueltas(const uint8_t *rdram, const std::string &clave, bool conValor, float valor, bool &hay,
                       float &resultado)
    {
        if (clave == "vueltas_gp")
        {
            g_vueltasGranPremio = conValor ? ps2x::trackFullLaps(rdram, PS2_RAM_SIZE, static_cast<int>(std::lround(valor))) : 0;
            resultado = static_cast<float>(g_vueltasGranPremio);
            std::cout << "[opciones-juego] RACE OPTIONS: circuito " << valor << ", " << g_vueltasGranPremio
                      << " vueltas el Gran Premio" << std::endl;
        }
        else if (clave == "vueltas_de")
        {
            resultado = static_cast<float>(ps2x::raceLapsForDistance(g_vueltasGranPremio, valor));
        }
        else if (clave == "vueltas_distancia")
        {
            resultado = ps2x::raceDistanceForLaps(g_vueltasGranPremio, static_cast<int>(std::lround(valor)));
            std::cout << "[opciones-juego] RACE OPTIONS: " << valor << " vueltas = distancia " << resultado << std::endl;
        }
        else
        {
            return false;
        }
        hay = conValor && g_vueltasGranPremio > 0;
        return true;
    }

    // ---- Fase 5 (T16): dificultad de los rivales mas fina (ps2_ai_difficulty.h; opciones.ini dificultad_ia) ----
    // El juego tiene tres niveles (DIFFICULTY > OPPONENT AI, MEN_DIF_OA: game.aidifficulty(modo[, nivel]) = 0x15CD18,
    // tabla game.* 0x3FC820, que guarda el numero tal cual en el byte *0x49ED94 + 0x7C + modo * 8 + 6 del perfil;
    // game.rookie, game.semipro, game.professional = 0, 1, 2). func_1E6AC8 lo copia a la preparacion de la carrera
    // (*0x3FE588 + 0x11C) y la carrera lo lee en: func_1823C0(piloto) (dentro de func_182700, que rehace las
    // estadisticas de cada piloto: a los de la IA les suma +0x24..+0x38 segun el nivel, nada en HARD), func_372E10(coche)
    // (a cada coche de la IA, + 0xD0 bit 0, le pone + 0x208, + 0x20C, + 0x21C y + 0x220 segun el nivel; el del jugador
    // lleva otros fijos), func_1E52D8 (FakeTimes/<circuito>_easy|_medm|_hard.binfkt, los tiempos de las sesiones que no
    // se corren) y func_1B7510 (lo recibe pero no lo usa). Con un valor fino que cuente (el nivel del juego es el mas
    // cercano a el), las dos primeras se llaman tal cual y despues lo que escribieron se mueve a la mezcla de los dos
    // niveles de alrededor; los tiempos simulados siguen los del nivel del juego. Las dos son hojas (func_1823C0 solo
    // llama a func_18F760, que solo lee): terminan al volver de la llamada. [f1-06:ia] en las primeras aplicaciones,
    // con un AVISO si lo que escribio el juego no es lo de su tabla (entonces no se toca).
    constexpr uint32_t kIaPiloto = 0x001823C0u;
    constexpr uint32_t kIaCoche = 0x00372E10u;
    constexpr uint32_t kPreparacionCarrera = 0x003FE588u;
    constexpr uint32_t kPilotosCarrera = 0x00422E64u; // *: los pilotos (0x1DC bytes); el de un piloto, su + 0x82
    constexpr uint32_t kSesionCarrera = 0x0049F1E8u;  // *: + 0x590 bit 35 = todos de la IA (func_18F760)
    PS2Runtime::RecompiledFunction g_iaPilotoOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_iaCocheOriginal = nullptr;

    int dificultadIaFina()
    {
#if defined(PS2X_ENABLE_DEBUG_UI)
        float valor = 0.0f;
        if (PS2OptionsMenu::gameOptionNumber("dificultad_ia", valor))
            return static_cast<int>(std::lround(valor));
#endif
        const char *variable = std::getenv("PS2X_DIFICULTAD_IA");
        return variable != nullptr ? std::atoi(variable) : 0;
    }

    // El nivel del juego en la carrera y el punto entre niveles (-1: sin preparacion o con un nivel desconocido).
    float posicionIa(const uint8_t *rdram, int &nivel)
    {
        nivel = -1;
        const uint32_t preparacion = leer32(rdram, kPreparacionCarrera);
        if (!punteroEe(preparacion))
            return -1.0f;
        nivel = static_cast<int>(leer32(rdram, preparacion + 0x11Cu));
        return ps2x::aiDifficultyPosition(dificultadIaFina(), nivel);
    }

    // func_1823C0 no toca al piloto si func_18F760(su ficha) es 1: el del jugador.
    bool pilotoDeLaIa(const uint8_t *rdram, uint32_t piloto)
    {
        const uint32_t sesion = leer32(rdram, kSesionCarrera);
        uint64_t banderas = 0u;
        std::memcpy(&banderas, rdram + ((sesion + 0x590u) & PS2_RAM_MASK), sizeof(banderas));
        if (((banderas >> 35) & 1u) != 0u)
            return true;
        int16_t indice = 0;
        std::memcpy(&indice, rdram + ((piloto + 0x82u) & PS2_RAM_MASK), sizeof(indice));
        const uint32_t ficha = leer32(rdram, kPilotosCarrera) + static_cast<uint32_t>(indice * 0x1DC);
        return ((leer32(rdram, ficha + 0x64u) >> 30) & 1u) == 0u;
    }

    void escribirFloatIa(uint8_t *rdram, uint32_t direccion, float valor)
    {
        std::memcpy(rdram + (direccion & PS2_RAM_MASK), &valor, sizeof(valor));
    }

    bool informeIa(int tipo) // 0 pilotos, 1 coches: las primeras 24 de cada uno
    {
        static uint32_t informes[2] = {};
        return informes[tipo]++ < 24u;
    }

    void iaPiloto(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kIaPiloto)
        {
            g_iaPilotoOriginal(rdram, ctx, runtime);
            return;
        }
        const uint32_t piloto = GPR_U32(ctx, 4);
        constexpr size_t kCampos = std::size(ps2x::kAiDriverOffsets);
        float antes[kCampos];
        for (size_t i = 0; i < kCampos; ++i)
            antes[i] = leerFloat(rdram, piloto + ps2x::kAiDriverOffsets[i].offset);
        g_iaPilotoOriginal(rdram, ctx, runtime);
        int nivel = -1;
        const float posicion = posicionIa(rdram, nivel);
        const bool deLaIa = pilotoDeLaIa(rdram, piloto);
        if (posicion < 0.0f || posicion == static_cast<float>(nivel) || !deLaIa)
        {
            static const bool s_medirIa = std::getenv("F1_IA_MEDIR") != nullptr;
            if (s_medirIa && informeIa(0))
                std::cout << "[f1-06:ia] t=" << segundosDesdeArranque() << " piloto 0x" << std::hex << piloto << std::dec
                          << " nivel " << nivel << " -> " << posicion << (deLaIa ? "" : " (jugador)") << ": sin cambios"
                          << std::endl;
            return;
        }
        for (size_t i = 0; i < kCampos; ++i)
        {
            const ps2x::AiDifficultyField &campo = ps2x::kAiDriverOffsets[i];
            const float ahora = leerFloat(rdram, piloto + campo.offset);
            if (std::fabs(ahora - antes[i] - campo.perLevel[nivel]) > 1e-4f)
            {
                std::cout << "[f1-06:ia] AVISO: el piloto 0x" << std::hex << piloto << " + 0x" << campo.offset << std::dec
                          << " paso de " << antes[i] << " a " << ahora << " (nivel " << nivel << "); no se toca" << std::endl;
                return;
            }
        }
        std::ostringstream detalle;
        for (size_t i = 0; i < kCampos; ++i)
        {
            const ps2x::AiDifficultyField &campo = ps2x::kAiDriverOffsets[i];
            const float valor = antes[i] + ps2x::aiDifficultyBlend(campo, posicion);
            escribirFloatIa(rdram, piloto + campo.offset, valor);
            detalle << ' ' << valor;
        }
        if (informeIa(0))
            std::cout << "[f1-06:ia] t=" << segundosDesdeArranque() << " piloto 0x" << std::hex << piloto << std::dec
                      << " nivel " << nivel << " -> " << posicion << ":" << detalle.str() << std::endl;
    }

    void iaCoche(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kIaCoche)
        {
            g_iaCocheOriginal(rdram, ctx, runtime);
            return;
        }
        const uint32_t coche = GPR_U32(ctx, 4);
        g_iaCocheOriginal(rdram, ctx, runtime);
        if ((leer32(rdram, coche + 0xD0u) & 1u) == 0u) // no es de la IA: valores fijos
            return;
        int nivel = -1;
        const float posicion = posicionIa(rdram, nivel);
        if (posicion < 0.0f || posicion == static_cast<float>(nivel))
            return;
        for (const ps2x::AiDifficultyField &campo : ps2x::kAiCarValues)
            if (leerFloat(rdram, coche + campo.offset) != campo.perLevel[nivel])
            {
                std::cout << "[f1-06:ia] AVISO: el coche 0x" << std::hex << coche << " + 0x" << campo.offset << std::dec
                          << " = " << leerFloat(rdram, coche + campo.offset) << " (nivel " << nivel << "); no se toca"
                          << std::endl;
                return;
            }
        std::ostringstream detalle;
        for (const ps2x::AiDifficultyField &campo : ps2x::kAiCarValues)
        {
            const float valor = ps2x::aiDifficultyBlend(campo, posicion);
            escribirFloatIa(rdram, coche + campo.offset, valor);
            detalle << ' ' << valor;
        }
        if (informeIa(1))
            std::cout << "[f1-06:ia] t=" << segundosDesdeArranque() << " coche 0x" << std::hex << coche << std::dec
                      << " nivel " << nivel << " -> " << posicion << ":" << detalle.str() << std::endl;
    }

    // CRASH DAMAGE en carrera (danos-siempre T4): func_1E6AC8(prep, perfil) prepara la carrera; con el bit 4 de la
    // palabra S + 4 (S = perfil + 0x7C + func_1E8DB0(*perfil) * 8) pone prep + 0x120 |= 0x4000 | 0x8000 (0x1e6b84) y
    // despues, si la distancia *(S + 0) < 0,06, borra los dos y deja el desgaste en 0 (0x1e6d8c-0x1e6dd8). Tras la
    // original se vuelven a poner los dos bits si el perfil tiene los danos; el desgaste y lo demas, como lo dejo el
    // juego. Sin el bit 4 no se toca nada. [f1-06:danos] cuando se devuelven. F1_DANOS_CORTE=1 deja el corte original.
    constexpr uint32_t kPrepararCarrera = 0x001E6AC8u;
    constexpr uint32_t kTablaModoPerfil = 0x00449660u; // saltos de func_1E8DB0 para los modos 6..28
    constexpr uint32_t kBitsDanos = 0x4000u | 0x8000u;
    PS2Runtime::RecompiledFunction g_prepararCarreraOriginal = nullptr;

    // func_1E8DB0(modo de juego): la ranura del perfil (0..6) por su tabla de saltos; -1 si el destino no se conoce.
    int ranuraPerfil(const uint8_t *rdram, uint32_t modo)
    {
        if (modo - 6u >= 0x17u)
            return 1;
        switch (leer32(rdram, kTablaModoPerfil + (modo - 6u) * 4u))
        {
        case 0x001E8DD8u: return 1;
        case 0x001E8DE0u: return 5;
        case 0x001E8DE8u: return 2;
        case 0x001E8DF0u: return 3;
        case 0x001E8DF8u: return 4;
        case 0x001E8E00u: return 0;
        case 0x001E8E08u: return 6;
        case 0x001E8E10u: return 1;
        default: return -1;
        }
    }

    void prepararCarrera(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kPrepararCarrera)
        {
            g_prepararCarreraOriginal(rdram, ctx, runtime);
            return;
        }
        // Como el despachador, hasta que vuelve: el codigo traducido puede volver a medias en un punto de control.
        const uint32_t prep = GPR_U32(ctx, 4), perfil = GPR_U32(ctx, 5), vuelta = GPR_U32(ctx, 31);
        g_prepararCarreraOriginal(rdram, ctx, runtime);
        for (int paso = 0; paso < 1000000 && ctx->pc != vuelta && ctx->pc != 0u && !runtime->isStopRequested(); ++paso)
            runtime->lookupFunction(ctx->pc)(rdram, ctx, runtime);
        if (ctx->pc != vuelta)
            return;
        static const bool s_corte = [] { const char *v = std::getenv("F1_DANOS_CORTE"); return v && std::atoi(v) != 0; }();
        if (s_corte || !punteroEe(prep) || !punteroEe(perfil))
            return;
        const uint32_t modo = leer32(rdram, perfil);
        const int ranura = ranuraPerfil(rdram, modo);
        if (ranura < 0)
        {
            std::cout << "[f1-06:danos] AVISO: modo " << modo << " sin ranura conocida; no se toca" << std::endl;
            return;
        }
        const uint32_t s = perfil + 0x7Cu + static_cast<uint32_t>(ranura) * 8u;
        if (leer32(rdram, s) != leer32(rdram, prep + 0xF8u)) // la original copia *(S + 0) a prep + 0xF8
        {
            std::cout << "[f1-06:danos] AVISO: la ranura " << ranura << " no es la de la carrera; no se toca" << std::endl;
            return;
        }
        const uint32_t antes = leer32(rdram, prep + 0x120u);
        if (((leer32(rdram, s + 4u) >> 4) & 1u) == 0u || (antes & kBitsDanos) == kBitsDanos)
            return;
        const uint32_t despues = antes | kBitsDanos;
        std::memcpy(rdram + ((prep + 0x120u) & PS2_RAM_MASK), &despues, sizeof(despues));
        std::cout << "[f1-06:danos] t=" << segundosDesdeArranque() << " modo " << modo << " ranura " << ranura
                  << " distancia " << leerFloat(rdram, s) << " prep 0x" << std::hex << prep << " + 0x120: 0x" << antes
                  << " -> 0x" << despues << std::dec << std::endl;
    }

    // DIFFICULTY (fase 5, T16): la fila OPPONENT AI del juego (ClassConfigMenuToggleItem, EASY / MEDIUM / HARD) va de
    // EASY a HARD en pasos de 10 (EASY, 10, 20, 30, 40, MEDIUM, 60... HARD). Guarda lo mismo que el juego, el nivel mas
    // cercano (game.aidifficulty), y el valor fino en opciones.ini (por el puente: "ps2x:ia_elegir", valor de la fila ->
    // nivel del juego; "ps2x:ia_fila", nivel del juego -> valor de la fila).
    // CRASH DAMAGE (danos-siempre T1): con distancia 0,05 ("3 LAPS") el juego deja la fila solo en OFF, en gris y sin
    // escribir; tras su customActivate vuelve a OFF / ON con game.crashdamage del modo, como con cualquier otra
    // distancia. Combustible y desgaste siguen como en el juego; Time Trial no tiene esa fila.
    constexpr const char *kExtensionDificultad = R"lua(
if Menu and Menu.mOpponentAI and os.getenv("ps2x:ia_fila", 0) then
  local G = os.getenv
  local niveles = {game.rookie, game.semipro, game.professional}
  local textos = {[0] = "VAR_EAS", [50] = "VAR_MEDIUM", [100] = "VAR_HAR"}
  local filas = {}
  for v = 0, 100, 10 do filas[v / 10 + 1] = {textos[v] or tostring(v), v} end
  Menu.mOpponentAI.mValueMappings = filas
  Menu.mOpponentAI.setGetFunction = function(v)
    local modo = game.optionsgamemode()
    if v ~= nil then
      game.aidifficulty(modo, niveles[G("ps2x:ia_elegir", v) + 1])
      return
    end
    local actual, n = game.aidifficulty(modo), -1
    for i = 1, 3 do if niveles[i] == actual then n = i - 1 end end
    return G("ps2x:ia_fila", n)
  end
end
if Menu and Menu.mCrashDamage and Menu.customActivate then
  local activar = Menu.customActivate
  Menu.customActivate = function(self)
    local fila = self.mCrashDamage
    local activarMenu = self.onActivate
    local abrir = false
    -- Antes del onActivate del menu (que lee todas las filas): la fila como con cualquier otra distancia.
    self.onActivate = function(menu)
      menu.onActivate = activarMenu
      abrir = game.gamemode() ~= game.mode_testdrive and game.racedistance(game.optionsgamemode()) == 0.05
      if abrir then
        fila.mValueMappings = {{"VAR_OFF", false}, {"VAR_ON", true}}
        fila.setGetFunction = function(v)
          if v ~= nil then
            game.crashdamage(game.optionsgamemode(), v)
            return
          end
          return game.crashdamage(game.optionsgamemode())
        end
      end
      return activarMenu(menu)
    end
    activar(self)
    self.onActivate = activarMenu
    if not abrir then return end
    for i = 1, table.getn(self.mMenuObjects) do
      if self.mMenuObjects[i] == fila then
        if i == self.mCurrentMenuItem then fila:setState(1) else fila:setState(0) end
      end
    end
    self:updateItemHighlight()
  end
end
)lua";

    // Puente de la fila OPPONENT AI (kExtensionDificultad); clave sin "ps2x:". false si no es una de ellas.
    bool puenteDificultadIa(const std::string &clave, bool conValor, float valor, bool &hay, float &resultado)
    {
        const int numero = conValor ? static_cast<int>(std::lround(valor)) : -1;
        if (clave == "ia_fila")
        {
            resultado = static_cast<float>(ps2x::aiDifficultyRowValue(dificultadIaFina(), numero));
        }
        else if (clave == "ia_elegir")
        {
#if defined(PS2X_ENABLE_DEBUG_UI)
            const ps2x::AiDifficultyChoice eleccion = ps2x::aiDifficultyChoice(numero);
            const bool cambiada = dificultadIaFina() != eleccion.fine &&
                                  PS2OptionsMenu::setGameOptionNumber("dificultad_ia", static_cast<float>(eleccion.fine));
            const bool guardada = cambiada && PS2OptionsMenu::saveGameOptions();
            resultado = static_cast<float>(eleccion.gameLevel);
            std::cout << "[opciones-juego] OPPONENT AI " << numero << ": nivel del juego " << eleccion.gameLevel
                      << ", dificultad_ia = " << dificultadIaFina() << (guardada ? " (guardada)" : "") << std::endl;
#endif
        }
        else
        {
            return false;
        }
        hay = conValor;
        return true;
    }

#if defined(PS2X_ENABLE_DEBUG_UI)
    // Puente de PANTALLA PC (kPaginaPantallaPc); clave sin "ps2x:". false si no es una de ellas. Los tamanos de la
    // ventana se toman al crear la fila ("ventanas") y no cambian mientras la pagina sigue abierta; la foto de las tres
    // opciones, al abrirla ("recordar"). "recordar", "descartar" y "por_defecto" valen tambien para JUEGO EN RED PC
    // (pagina 4) y, en el menu principal, GRAFICOS PC y AJUSTES PC (paginas 1 y 2, Nativizacion T7), cada una con sus
    // opciones (ps2x::gameOptionsPageKeys); con el anfitrion encendido o pedido, el runtime no cambia las de red.
    std::vector<std::string> g_tamanosVentana;
    std::map<std::string, std::string> g_fotoPantallaPc;

    const std::vector<std::string> &opcionesDeLaPagina()
    {
        return ps2x::gameOptionsPageKeys(g_paginaOpciones);
    }

    const char *nombrePagina(int pagina)
    {
        return pagina == 4 ? "JUEGO EN RED PC" : pagina == 3 ? "PANTALLA PC" : pagina == 2 ? "AJUSTES PC" : "GRAFICOS PC";
    }

    bool puentePantallaPc(const std::string &clave, bool conValor, float valor, bool &hay, float &resultado)
    {
        const long numero = conValor ? std::lround(valor) : -1;
        const bool enLista = numero >= 0 && static_cast<size_t>(numero) < g_tamanosVentana.size();
        if (clave == "ventanas")
        {
            g_tamanosVentana = PS2OptionsMenu::gameWindowSizes();
            resultado = static_cast<float>(g_tamanosVentana.size());
        }
        else if (clave == "ventana_ancho" || clave == "ventana_alto")
        {
            int ancho = 0, alto = 0;
            hay = enLista && ps2x::parseWindowSize(g_tamanosVentana[static_cast<size_t>(numero)], ancho, alto);
            resultado = static_cast<float>(clave == "ventana_ancho" ? ancho : alto);
            return true;
        }
        else if (clave == "tamano_ventana")
        {
            if (conValor)
            {
                const bool cambiada =
                    enLista && PS2OptionsMenu::setGameOptionText(clave, g_tamanosVentana[static_cast<size_t>(numero)]);
                std::cout << "[opciones-juego] tamano_ventana = "
                          << (enLista ? g_tamanosVentana[static_cast<size_t>(numero)] : std::to_string(numero))
                          << (cambiada ? "" : " (no se puede)") << std::endl;
            }
            std::string actual;
            PS2OptionsMenu::gameOptionText(clave, actual);
            const auto encontrado = std::find(g_tamanosVentana.begin(), g_tamanosVentana.end(), actual);
            hay = encontrado != g_tamanosVentana.end();
            resultado = static_cast<float>(encontrado - g_tamanosVentana.begin());
            return true;
        }
        else if (clave == "gs_en_uso")
        {
            resultado = PS2OptionsMenu::gameGsInUse() == "gpu" ? 1.0f : 0.0f;
        }
        else if (clave == "recordar")
        {
            g_fotoPantallaPc.clear();
            for (const std::string &opcion : opcionesDeLaPagina())
                if (std::string texto; PS2OptionsMenu::gameOptionText(opcion, texto))
                    g_fotoPantallaPc[opcion] = texto;
            resultado = 1.0f;
        }
        else if (clave == "descartar")
        {
            std::string fijas;
            for (const auto &[opcion, texto] : g_fotoPantallaPc)
                if (std::string ahora; PS2OptionsMenu::gameOptionText(opcion, ahora) && ahora != texto &&
                                       !PS2OptionsMenu::setGameOptionText(opcion, texto))
                    fijas += (fijas.empty() ? "" : ", ") + opcion;
            std::cout << "[opciones-juego] " << nombrePagina(g_paginaOpciones) << ": vuelve a lo de antes de abrirla"
                      << (fijas.empty() ? "" : " (no se puede: " + fijas + ")") << std::endl;
            resultado = 1.0f;
        }
        else if (clave == "por_defecto")
        {
            PS2OptionsMenu::resetGameOptions(opcionesDeLaPagina());
            std::string valores;
            for (const std::string &opcion : opcionesDeLaPagina())
            {
                std::string texto;
                PS2OptionsMenu::gameOptionText(opcion, texto);
                valores += (valores.empty() ? "" : ", ") + opcion + " = " + texto;
            }
            std::cout << "[opciones-juego] " << nombrePagina(g_paginaOpciones) << ": valores por defecto (" << valores
                      << ")" << std::endl;
            resultado = 1.0f;
        }
        else
        {
            return false;
        }
        hay = true;
        return true;
    }

    // Puente de JUEGO EN RED PC (kPaginaJuegoEnRed): sus filas de solo lectura; clave sin "ps2x:". false si no es una
    // de ellas. "red_estado" toma el estado (esperando hasta 0,5 s a que el hilo principal encienda o apague el
    // anfitrion pedido) y las demas lo leen. "red_motivo" (por que no se pudo empezar) y "red_invitado" (la direccion
    // del invitado conectado) son textos (Nativizacion T14): sin valor, su largo; con valor i, el codigo del caracter i
    // (ASCII imprimible, como mucho ps2x::kNetplayPageTextSize).
    // CONTRASENA (Nativizacion T16): "red_clave_empezar", "red_clave_letra" (codigo) y "red_clave_poner" traen la
    // escrita en el teclado del juego (ps2x::GamePasswordEntry: entera o nada) al anfitrion
    // (PS2OptionsMenu::setGameNetplayPassword, el bufer de la contrasena del menu); "red_clave_azar" la deja vacia (una al azar al
    // encender); "red_clave_ver" la que hay que decir al invitado (sin valor el largo, con valor i el codigo del
    // caracter i). El registro solo dice si se puso, nunca la contrasena; las copias de aqui se borran al usarlas.
    PS2OptionsMenu::GameNetplayState g_estadoRed;
    ps2x::GamePasswordEntry g_claveRed;

    void borrarClave(std::string &clave)
    {
        volatile char *letras = clave.data();
        for (size_t i = 0; i < clave.size(); ++i)
            letras[i] = '\0';
        clave.clear();
    }

    bool puenteJuegoEnRed(const std::string &clave, bool conValor, float valor, bool &hay, float &resultado)
    {
        if (clave == "red_estado")
        {
            g_estadoRed = PS2OptionsMenu::gameNetplayState(500);
            resultado = static_cast<float>(g_estadoRed.state);
            std::cout << "[opciones-juego] JUEGO EN RED PC: estado " << g_estadoRed.state << ", puerto " << g_estadoRed.port
                      << ", " << g_estadoRed.addresses.size() << " direcciones" << std::endl;
        }
        else if (clave == "red_direcciones")
        {
            resultado = static_cast<float>(g_estadoRed.addresses.size());
        }
        else if (clave == "red_puerto_activo")
        {
            resultado = static_cast<float>(g_estadoRed.port);
        }
        else if (clave == "red_motivo" || clave == "red_invitado")
        {
            const std::string &texto = clave == "red_motivo" ? g_estadoRed.reason : g_estadoRed.guest;
            if (!conValor)
            {
                resultado = static_cast<float>(texto.size());
            }
            else
            {
                const long numero = std::lround(valor);
                hay = numero >= 0 && static_cast<size_t>(numero) < texto.size();
                resultado = hay ? static_cast<float>(static_cast<unsigned char>(texto[static_cast<size_t>(numero)])) : 0.0f;
                return true;
            }
        }
        else if (clave == "red_clave_empezar")
        {
            g_claveRed.begin();
            resultado = 1.0f;
        }
        else if (clave == "red_clave_letra")
        {
            hay = conValor && g_claveRed.add(valor);
            resultado = hay ? 1.0f : 0.0f;
            return true;
        }
        else if (clave == "red_clave_poner" || clave == "red_clave_azar")
        {
            std::string texto;
            const size_t recibidos = g_claveRed.size();
            const bool tomada = clave == "red_clave_azar" || g_claveRed.finish(texto);
            const bool puesta = tomada && PS2OptionsMenu::setGameNetplayPassword(texto);
            borrarClave(texto);
            resultado = puesta ? 1.0f : 0.0f;
            std::cout << "[opciones-juego] JUEGO EN RED PC: contrasena "
                      << (clave == "red_clave_azar" ? "al azar" : "del teclado") << (puesta ? " puesta" : " rechazada")
                      << (clave == "red_clave_azar" || puesta ? "" : " (" + std::to_string(recibidos) + " caracteres)")
                      << std::endl;
        }
        else if (clave == "red_clave_ver")
        {
            std::string texto = PS2OptionsMenu::gameNetplayPassword();
            const long numero = conValor && std::isfinite(valor) ? std::lround(valor) : -1;
            hay = !conValor || (numero >= 0 && static_cast<size_t>(numero) < texto.size());
            resultado = !conValor ? static_cast<float>(texto.size())
                        : hay     ? static_cast<float>(static_cast<unsigned char>(texto[static_cast<size_t>(numero)]))
                                  : 0.0f;
            borrarClave(texto);
            return true;
        }
        else if (clave == "red_octeto")
        {
            const long numero = conValor ? std::lround(valor) : -1;
            hay = numero >= 0 && static_cast<size_t>(numero) < 4u * g_estadoRed.addresses.size();
            resultado = hay ? static_cast<float>(g_estadoRed.addresses[static_cast<size_t>(numero) / 4u]
                                                                       [static_cast<size_t>(numero) % 4u])
                            : 0.0f;
            return true;
        }
        else
        {
            return false;
        }
        hay = true;
        return true;
    }
#endif

    struct ExtensionLua
    {
        const char *trozo; // clave (claveTrozoLua) del trozo del juego
        const char *texto;
    };
    const ExtensionLua kExtensionesLua[] = {
        {"frontend/scripts/options/optionsmenu.lua", kExtensionOpciones},
        {"frontend/scripts/pausemenu/profiles2006_pause/editprofile.lua", kExtensionOpcionesPausa},
        {"frontend/scripts/options/displaysettings.lua", kExtensionPantalla},
        {"frontend/scripts/options/raceoptions.lua", kExtensionCarrera},
        {"frontend/scripts/options/difficultymenu.lua", kExtensionDificultad},
        {"frontend/scripts/pausemenu/common/replaymenu.lua", kExtensionRepeticion},
    };

    const char *extensionLua(const std::string &clave)
    {
        for (const ExtensionLua &extension : kExtensionesLua)
            if (clave == extension.trozo)
                return extension.texto;
        if (clave == "frontend/scripts/offline/prerace.lua" && carreraDirecta().lua())
            return kExtensionCarreraDirecta;
        // El menu de pausa de cada modo (FrontEnd/Scripts/PauseMenu/<modo>/PauseMenu.lua; kExtensionPausa mira su forma).
        constexpr std::string_view kPausas = "frontend/scripts/pausemenu/", kPausa = "/pausemenu.lua";
        if (clave.size() > kPausas.size() + kPausa.size() && clave.compare(0, kPausas.size(), kPausas) == 0 &&
            clave.compare(clave.size() - kPausa.size(), kPausa.size(), kPausa) == 0)
            return kExtensionPausa;
        return nullptr;
    }

    // El texto que sustituye al trozo: el original (por "=ps2x:<nombre>") y despues la extension.
    std::string textoConExtension(const std::string &nombre, const char *extension)
    {
        std::string literal;
        for (char c : nombre)
        {
            if (c == '\\' || c == '"')
                literal.push_back('\\');
            literal.push_back(c);
        }
        return std::string("-- F1 06 (PC): ") + nombre + " del juego y las opciones del PC\n"
               "local ps2x_original = loadstring(\"\", \"" + kPrefijoOriginalLua + literal + "\")\n"
               "if ps2x_original then setfenv(ps2x_original, getfenv(1)) ps2x_original() end\n"
               "ps2x_original = nil\n" +
               extension;
    }

    // Gancho de lua_load: true si el trozo es uno de los nuestros (lo sirve getS desde g_trozoModLua).
    bool servirOpcionesJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const std::string &nombre)
    {
        const uint32_t lector = GPR_U32(ctx, 5);
        const uint32_t datos = GPR_U32(ctx, 6);
        std::vector<uint8_t> bytes;
        std::string detalle;
        if (nombre == std::string(kPrefijoOriginalLua) + kTrozoAjustesPc)
        {
            const std::string texto = kPaginaAjustesPc;
            bytes.assign(texto.begin(), texto.end());
            detalle = "pagina AJUSTES PC";
        }
        else if (nombre == std::string(kPrefijoOriginalLua) + kTrozoGraficosPc)
        {
            const std::string texto = kPaginaGraficosPc;
            bytes.assign(texto.begin(), texto.end());
            detalle = "pagina GRAFICOS PC";
        }
        else if (nombre == std::string(kPrefijoOriginalLua) + kTrozoPantallaPc)
        {
            const std::string texto = kPaginaPantallaPc;
            bytes.assign(texto.begin(), texto.end());
            detalle = "pagina PANTALLA PC";
        }
        else if (nombre == std::string(kPrefijoOriginalLua) + kTrozoJuegoEnRed)
        {
            const std::string texto = kPaginaJuegoEnRed;
            bytes.assign(texto.begin(), texto.end());
            detalle = "pagina JUEGO EN RED PC";
        }
        else if (nombre == std::string(kPrefijoOriginalLua) + kTrozoOpcionesPc)
        {
            const std::string texto = kPaginaOpcionesPc;
            bytes.assign(texto.begin(), texto.end());
            detalle = "pagina OPCIONES PC";
        }
        else if (nombre.rfind(kPrefijoOriginalLua, 0) == 0)
        {
            const auto original = g_trozosOriginales.find(claveTrozoLua(nombre.substr(std::strlen(kPrefijoOriginalLua))));
            if (original == g_trozosOriginales.end())
            {
                std::cout << "[opciones-juego] " << nombre << ": no se guardo el original" << std::endl;
                return false;
            }
            bytes = original->second;
            detalle = "bytecode original";
        }
        else
        {
            const std::string clave = claveTrozoLua(nombre);
            const char *extension = extensionLua(clave);
            if (extension == nullptr)
                return false;
            if (lector != kLuaLectorGetS && lector != kLuaLectorFrontEnd)
            {
                std::cout << "[opciones-juego] " << nombre << ": lector 0x" << std::hex << lector << std::dec
                          << " desconocido; se carga sin las opciones del PC" << std::endl;
                return false;
            }
            const uint32_t puntero = leer32(rdram, datos);
            const uint32_t tamano = leer32(rdram, datos + 4u);
            if (tamano == 0u || tamano > 0x00400000u || (puntero & PS2_RAM_MASK) + tamano > PS2_RAM_SIZE)
                return false;
            const uint8_t *original = rdram + (puntero & PS2_RAM_MASK);
            g_trozosOriginales[clave].assign(original, original + tamano);
            const std::string texto = textoConExtension(nombre, extension);
            bytes.assign(texto.begin(), texto.end());
            detalle = "original de " + std::to_string(tamano) + " bytes y extension";
        }
        if (g_buferModLua == 0u)
            g_buferModLua = runtime->guestMalloc(kBuferModLua, 64u);
        if (g_buferModLua == 0u)
            return false;
        std::cout << "[opciones-juego] " << nombre << ": " << detalle << " (" << bytes.size() << ")" << std::endl;
        g_trozoModLua.datos = datos;
        g_trozoModLua.bytes = std::move(bytes);
        SET_GPR_U32(ctx, 5, kLuaLectorGetS);
        return true;
    }

    // os.getenv(clave[, valor]) de Lua 5.0 (funcion C: a0 = lua_State, argumentos de L->base a L->top). Las claves
    // "ps2x:..." devuelven un numero o nil como lua_pushnumber (0x29D5D8: TObject de 8 bytes en L->top, tipo 3 y el
    // float; tipo 0 = nil) y L->top avanza; v0 = 1 resultado. Sin llamadas al juego.
    void trazarOsGetenv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t L = GPR_U32(ctx, 4);
        const uint32_t base = leer32(rdram, L + 12u);
        const uint32_t top = leer32(rdram, L + 8u);
        std::string clave;
        if (top >= base + 8u && leer32(rdram, base) == 4u)
            clave = leerCadena(rdram, leer32(rdram, leer32(rdram, base + 4u) + 16u), 64); // TString: el texto en +16
        if (clave.rfind("ps2x:", 0) != 0)
        {
            g_osGetenvOriginal(rdram, ctx, runtime);
            return;
        }
        clave.erase(0, 5);
        const bool conValor = top >= base + 16u && leer32(rdram, base + 8u) == 3u;
        float valor = 0.0f;
        if (conValor)
            std::memcpy(&valor, rdram + ((base + 12u) & PS2_RAM_MASK), sizeof(valor));
        bool hay = true;
        float resultado = 0.0f;
#if defined(PS2X_ENABLE_DEBUG_UI)
        if (clave == "modo_foto") // Nativizacion T10: MODO FOTO de la pausa (kExtensionPausa, kExtensionRepeticion)
        {
            ps2x::requestPhotoModeFromMenu();
            resultado = 1.0f;
        }
        else if (clave == "pagina")
        {
            if (conValor)
                g_paginaOpciones = static_cast<int>(std::lround(valor));
            resultado = static_cast<float>(g_paginaOpciones);
        }
        else if (clave == "abrir")
        {
            PS2OptionsMenu::syncGameOptions(*runtime);
            resultado = 1.0f;
            std::cout << "[opciones-juego] pagina " << nombrePagina(g_paginaOpciones) << std::endl;
        }
        else if (clave == "guardar")
        {
            resultado = PS2OptionsMenu::saveGameOptions() ? 1.0f : 0.0f;
        }
        else if (puenteVueltas(rdram, clave, conValor, valor, hay, resultado))
        {
        }
        else if (puenteCarrera(clave, hay, resultado))
        {
        }
        else if (puenteDificultadIa(clave, conValor, valor, hay, resultado))
        {
        }
        else if (puentePantallaPc(clave, conValor, valor, hay, resultado))
        {
        }
        else if (puenteJuegoEnRed(clave, conValor, valor, hay, resultado))
        {
        }
        else
        {
            if (conValor)
            {
                const bool cambiada = PS2OptionsMenu::setGameOptionNumber(clave, valor);
                std::cout << "[opciones-juego] " << clave << " = " << valor << (cambiada ? "" : " (no se puede)")
                          << std::endl;
            }
            hay = PS2OptionsMenu::gameOptionNumber(clave, resultado);
        }
#else
        hay = false;
#endif
        const uint32_t objeto = top & PS2_RAM_MASK;
        const uint32_t tipo = hay ? 3u : 0u;
        std::memcpy(rdram + objeto, &tipo, sizeof(tipo));
        std::memcpy(rdram + objeto + 4u, &resultado, sizeof(resultado));
        const uint32_t nuevoTop = top + 8u;
        std::memcpy(rdram + ((L + 8u) & PS2_RAM_MASK), &nuevoTop, sizeof(nuevoTop));
        SET_GPR_U32(ctx, 2, 1u);
        ctx->pc = GPR_U32(ctx, 31);
    }

    void trazarLuaLoad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t L = GPR_U32(ctx, 4);
        const uint32_t G = leer32(rdram, L + 0x10u);
        const uint32_t antes = leer32(rdram, G + 0x4Cu);
        const std::string nombre = leerCadena(rdram, GPR_U32(ctx, 7), 256);
        if (!g_luaVolcado.empty())
            volcarTrozoLua(rdram, GPR_U32(ctx, 5), GPR_U32(ctx, 6), nombre);
        g_trozoModLua = {};
        // Un trozo de los mods gana a nuestras opciones (que entonces no salen en esa pantalla).
        const bool hayMod = !g_modsLua.empty() && g_modsLua.count(claveTrozoLua(nombre)) != 0u;
        const bool opciones = g_opcionesJuego && !hayMod && servirOpcionesJuego(rdram, ctx, runtime, nombre);
        if (!opciones && hayMod)
        {
            if (const auto mod = g_modsLua.find(claveTrozoLua(nombre)); mod != g_modsLua.end())
            {
                std::ifstream archivo(mod->second, std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(archivo)), std::istreambuf_iterator<char>());
                if (g_buferModLua == 0u)
                    g_buferModLua = runtime->guestMalloc(kBuferModLua, 64u);
                if (archivo && !bytes.empty() && g_buferModLua != 0u)
                {
                    std::cout << "[mods-lua] " << nombre << " -> " << mod->second.string() << " (" << bytes.size() << ")"
                              << std::endl;
                    g_trozoModLua.datos = GPR_U32(ctx, 6);
                    g_trozoModLua.bytes = std::move(bytes);
                    SET_GPR_U32(ctx, 5, kLuaLectorGetS);
                }
                else
                {
                    std::cout << "[mods-lua] " << nombre << ": no se puede cargar " << mod->second.string() << std::endl;
                }
            }
        }
        g_luaLoadOriginal(rdram, ctx, runtime);
        if (g_luaMemoria)
            std::cout << "[f1-06:lua-carga] t=" << segundosDesdeArranque() << " " << nombre << " bytes " << antes << " -> "
                      << leer32(rdram, G + 0x4Cu) << std::endl;
    }

    void instalarCargaLua(PS2Runtime &runtime)
    {
        g_luaMemoria = std::getenv("F1_LUA_MEMORIA") != nullptr;
        if (const char *carpeta = std::getenv("F1_LUA_VOLCAR"); carpeta != nullptr && *carpeta != '\0')
        {
            g_luaVolcado = std::filesystem::path(carpeta);
            std::error_code error;
            std::filesystem::create_directories(g_luaVolcado, error);
            std::ofstream(g_luaVolcado / "indice.txt", std::ios::trunc)
                << "# nombre\ttamano\tformato\tarchivo\tprimeros bytes\n";
            std::cout << "[f1-06:lua-volcado] trozos de Lua en " << g_luaVolcado.string() << std::endl;
        }
        buscarModsLua();
#if defined(PS2X_ENABLE_DEBUG_UI)
        {
            const char *valor = std::getenv("F1_OPCIONES_JUEGO");
            g_opcionesJuego = valor == nullptr || std::atoi(valor) != 0;
        }
#endif
        std::cout << "[opciones-juego] "
                  << (g_opcionesJuego ? "OPTIONS > OPCIONES PC (GRAFICOS PC, PANTALLA PC, AJUSTES PC y JUEGO EN RED PC)" : "desactivadas")
                  << std::endl;
        if (g_opcionesJuego)
            instalarChivato(runtime, kOsGetenv, trazarOsGetenv, g_osGetenvOriginal);
        if (!g_modsLua.empty() || g_opcionesJuego)
            instalarChivato(runtime, kLuaLectorGetS, trazarLuaGetS, g_luaGetSOriginal);
        if (g_luaMemoria || !g_luaVolcado.empty() || !g_modsLua.empty() || g_opcionesJuego)
            instalarChivato(runtime, kLuaLoad, trazarLuaLoad, g_luaLoadOriginal);
    }

    // Asignador de Lua del juego (0x29B5B0(ud, bloque, tamanoViejo, tamanoNuevo) -> func_29C858 con el monton de
    // 0x40495C): con F1_LUA_MEMORIA, cada peticion que devuelve NULL.
    constexpr uint32_t kLuaFrealloc = 0x0029B5B0u;
    PS2Runtime::RecompiledFunction g_luaFreallocOriginal = nullptr;

    void trazarLuaFrealloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t bloque = GPR_U32(ctx, 5), viejo = GPR_U32(ctx, 6), nuevo = GPR_U32(ctx, 7);
        g_luaFreallocOriginal(rdram, ctx, runtime);
        if (nuevo != 0u && GPR_U32(ctx, 2) == 0u)
            std::cout << "[f1-06:lua-frealloc] t=" << segundosDesdeArranque() << " NULL bloque=0x" << std::hex << bloque
                      << std::dec << " viejo=" << viejo << " nuevo=" << nuevo << " monton=0x" << std::hex
                      << leer32(rdram, 0x0040495Cu) << std::dec << std::endl;
    }

#define F1_CHIVATO_LUA(nombre, texto, maximo)                                                                     \
    PS2Runtime::RecompiledFunction g_lua_##nombre##Original = nullptr;                                               \
    void trazarLua_##nombre(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)                                  \
    {                                                                                                                \
        static uint32_t veces = 0;                                                                                   \
        if (veces++ < (maximo))                                                                                      \
        {                                                                                                            \
            const uint32_t L = GPR_U32(ctx, 4);                                                                      \
            std::string args;                                                                                        \
            for (uint32_t i = 0; i < 3u; ++i)                                                                        \
            {                                                                                                        \
                const std::string arg = argumentoLua(rdram, L, i);                                                   \
                if (arg.empty())                                                                                     \
                    break;                                                                                           \
                args += (i ? ", " : "") + arg;                                                                       \
            }                                                                                                        \
            std::cout << "[f1-06:lua] t=" << segundosDesdeArranque() << " " texto "(" << args << ")" << std::endl;   \
        }                                                                                                            \
        g_lua_##nombre##Original(rdram, ctx, runtime);                                                               \
    }
    F1_CHIVATO_LUA(graphPush, "graph.push", 200u)
    F1_CHIVATO_LUA(graphPop, "graph.pop", 200u)
    F1_CHIVATO_LUA(graphResult, "graph.result", 200u)
    F1_CHIVATO_LUA(graphFinish, "graph.finish", 200u)
    F1_CHIVATO_LUA(graphInit, "graph.init", 20u)
    F1_CHIVATO_LUA(graphTransition, "graph.transition", 5u)
#undef F1_CHIVATO_LUA

    // graph.outcome tambien dispara el guion de pulsaciones (F1_GUION).
    PS2Runtime::RecompiledFunction g_lua_graphOutcomeOriginal = nullptr;
    void trazarLua_graphOutcome(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Sin tope: los sucesos son pocos por minuto y scripts/explorar.ps1 los necesita todos (un campeonato pasa de 200).
        const std::string arg = argumentoLua(rdram, GPR_U32(ctx, 4), 0);
        std::cout << "[f1-06:lua] t=" << segundosDesdeArranque() << " graph.outcome(" << arg << ")" << std::endl;
        if (!g_guion.empty() && arg.size() >= 2u && arg.front() == '"' && arg.back() == '"')
            dispararGuion(arg.substr(1u, arg.size() - 2u));
        aplicarAyudas(rdram);
        g_lua_graphOutcomeOriginal(rdram, ctx, runtime);
    }

    // Perfil de Lua (F1_LUA_PERFIL=1): luaV_execute (0x2A9DD0) se entra desde C (lua_call de los nodos del grafo,
    // por ejemplo) con la funcion en L->base - 1 (TObject: tipo 6, LClosure*; el Proto esta en closure + 12).
    // Cada 5 s imprime los Proto mas ejecutados con su linea y sus primeras constantes de texto, para saber que
    // pantalla del front-end esta activa. Se desensamblan con scripts/lua50_ram.py --proto y un volcado de RAM.
    constexpr uint32_t kLuaVExecute = 0x002A9DD0u;
    PS2Runtime::RecompiledFunction g_luaVExecuteOriginal = nullptr;
    std::map<uint32_t, uint32_t> g_perfilLua;

    std::string resumenProto(const uint8_t *rdram, uint32_t proto)
    {
        const uint32_t k = leer32(rdram, proto + 8u);
        const uint32_t sizek = leer32(rdram, proto + 40u);
        std::string texto = "linea=" + std::to_string(leer32(rdram, proto + 60u)) + " k:";
        uint32_t mostradas = 0;
        for (uint32_t i = 0; i < sizek && i < 64u && mostradas < 6u; ++i)
        {
            if (leer32(rdram, k + 8u * i) != 4u)
                continue;
            const uint32_t ts = leer32(rdram, k + 8u * i + 4u);
            texto += " " + leerCadena(rdram, leer32(rdram, ts + 16u), 40);
            ++mostradas;
        }
        return texto;
    }

    void trazarLuaVExecute(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t L = GPR_U32(ctx, 4);
        const uint32_t base = leer32(rdram, L + 12u);
        if (leer32(rdram, base - 8u) == 6u)
        {
            const uint32_t cierre = leer32(rdram, base - 4u);
            const uint32_t proto = leer32(rdram, cierre + 12u);
            ++g_perfilLua[proto];
        }
        static double ultimo = 0.0;
        const double ahora = segundosDesdeArranque();
        if (ahora - ultimo >= 5.0)
        {
            ultimo = ahora;
            std::vector<std::pair<uint32_t, uint32_t>> lista(g_perfilLua.begin(), g_perfilLua.end());
            std::sort(lista.begin(), lista.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
            std::cout << "[f1-06:luaperfil] t=" << ahora << " protos=" << lista.size() << std::endl;
            for (size_t i = 0; i < lista.size() && i < 8u; ++i)
                std::cout << "[f1-06:luaperfil]   0x" << std::hex << lista[i].first << std::dec << " x" << lista[i].second
                          << " " << resumenProto(rdram, lista[i].first) << std::endl;
            g_perfilLua.clear();
        }
        g_luaVExecuteOriginal(rdram, ctx, runtime);
    }

    void instalarChivatosLua(PS2Runtime &runtime)
    {
        if (const char *perfil = std::getenv("F1_LUA_PERFIL"); perfil && std::atoi(perfil) != 0)
            instalarChivato(runtime, kLuaVExecute, trazarLuaVExecute, g_luaVExecuteOriginal);
        instalarChivato(runtime, 0x0028C9E8u, trazarLua_graphPush, g_lua_graphPushOriginal);
        instalarChivato(runtime, 0x0028CAE8u, trazarLua_graphPop, g_lua_graphPopOriginal);
        instalarChivato(runtime, 0x0028CB98u, trazarLua_graphResult, g_lua_graphResultOriginal);
        instalarChivato(runtime, 0x0028C9A8u, trazarLua_graphFinish, g_lua_graphFinishOriginal);
        instalarChivato(runtime, 0x0028CDA8u, trazarLua_graphOutcome, g_lua_graphOutcomeOriginal);
        instalarChivato(runtime, 0x0028CC90u, trazarLua_graphInit, g_lua_graphInitOriginal);
        instalarChivato(runtime, 0x0028CCB8u, trazarLua_graphTransition, g_lua_graphTransitionOriginal);
    }

    // Render de cada fotograma (func_14AF40, la llama el front-end Lua). Cada 50 fotogramas: tiempo y fotogramas/s.
    constexpr uint32_t kRenderFotograma = 0x0014AF40u;
    PS2Runtime::RecompiledFunction g_renderFotogramaOriginal = nullptr;

    // Opcion panoramica del juego: el objeto de configuracion (0x6238 bytes, func_1598C0; su puntero en 0x49ED94, lo
    // devuelve func_164830) tiene el booleano "widescreen" en +0x2C (accesor Lua func_15B0B8 de la tabla 0x3FC6B8, que lo
    // lee y escribe con func_16E048). Con ella el juego dibuja anamorfico: la ventana pasa a 16:9 (parche 0103). Su
    // puntero, kPunteroConfiguracion, esta con los efectos de velocidad del modo foto (aspecto T1).
    constexpr uint32_t kConfigPanoramica = 0x2Cu;
    constexpr uint32_t kConfigIdioma = 0x74u;

    void trazarRenderFotograma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t fotogramas = 0;
        static double ultimo = 0.0;
        if ((fotogramas % 10u) == 0u)
        {
            const uint32_t configuracion = leer32(rdram, kPunteroConfiguracion);
            if (configuracion != 0u)
            {
                const int panoramica = leer32(rdram, configuracion + kConfigPanoramica) != 0u ? 1 : 0;
                static int anterior = -1;
                if (panoramica != anterior)
                {
                    anterior = panoramica;
                    std::cout << "[f1-06] opcion panoramica = " << panoramica << " t=" << segundosDesdeArranque()
                              << std::endl;
                }
                ps2SetGameWidescreen(panoramica);
                // Aspecto T4: el idioma del juego para los textos del anfitrion (game.language(), func_15B610, lee
                // +0x74 de este objeto; game.spanish = 4, visto con el juego en espanol).
                const uint32_t idioma = leer32(rdram, configuracion + kConfigIdioma);
                static uint32_t idiomaAnterior = 0xFFFFFFFFu;
                if (idioma != idiomaAnterior)
                {
                    idiomaAnterior = idioma;
                    std::cout << "[f1-06:idioma] idioma del juego = " << idioma
                              << (idioma == ps2x::kGameLanguageSpanish ? " (espanol)" : "") << " t="
                              << segundosDesdeArranque() << std::endl;
                }
                ps2x::noteGameLanguage(idioma);
            }
        }
        if ((fotogramas++ % 50u) == 0u)
        {
            const double ahora = segundosDesdeArranque();
            std::cout << "[f1-06:fotograma] n=" << fotogramas - 1u << " t=" << ahora
                      << " fps=" << (fotogramas > 1u ? 50.0 / (ahora - ultimo) : 0.0) << std::endl;
            ultimo = ahora;
        }
        g_renderFotogramaOriginal(rdram, ctx, runtime);
    }

    // Videos .ipf de fondo de los menus (clase Lua movie, p. ej. Movies/PAL/SINGLMOV.ipf) y de los circuitos: el juego
    // los decodifica con su propio codigo de IPU (0x2D3A28-0x2D3E04: FDEC, IDEC, DMA 0x1000B400 en cadena y 0x1000B000)
    // y dibuja el fotograma como teselas con textura (metodo 0x2CA8C0). Desde el parche 0102 el runtime emula el IPU
    // (ps2_ipu.cpp); antes ese dibujo se anulaba porque llenaba la pantalla de basura.

    // Epilogo de func_1C04C0 (ordenacion por insercion de la deteccion de colisiones; la usa la carga de la carrera).
    // El recompilador tomo unos bytes de una fuente (0x46B204: 0x001C0808) por un puntero a funcion y abrio una
    // funcion falsa en 0x1C0808, el hueco de retardo del salto que cierra el bucle. El recompilador ya traduce ese
    // hueco (ps2xRecomp, decodeFunction), pero al terminar el bucle la ejecucion sigue en 0x1C080C, que no tiene
    // entrada: ld $s0, 0($sp); ld $ra, 8($sp); jr $ra; addiu $sp, $sp, 0x10.
    void epilogo_1C080C(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t sp = GPR_U32(ctx, 29);
        uint64_t s0 = 0;
        uint64_t ra = 0;
        std::memcpy(&s0, rdram + (sp & PS2_RAM_MASK), sizeof(s0));
        std::memcpy(&ra, rdram + ((sp + 8u) & PS2_RAM_MASK), sizeof(ra));
        SET_GPR_U64(ctx, 16, s0);
        SET_GPR_U64(ctx, 31, ra);
        SET_GPR_S32(ctx, 29, static_cast<int32_t>(sp + 0x10u));
        ctx->pc = static_cast<uint32_t>(ra);
    }

    // exit() de la biblioteca C (0x102038: ejecuta los atexit y llama a _exit, que acaba el hilo principal). Si el
    // juego sale por su cuenta (p. ej. al entrar en el campeonato, fase 5) los demas hilos se quedan esperando y la
    // pantalla en blanco: se registra quien llama, con que codigo y la pila del EE.
    constexpr uint32_t kExit = 0x00102038u;
    PS2Runtime::RecompiledFunction g_exitOriginal = nullptr;

    void trazarExit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cout << "[f1-06:exit] t=" << segundosDesdeArranque() << " codigo=" << static_cast<int32_t>(GPR_U32(ctx, 4))
                  << " ra=0x" << std::hex << GPR_U32(ctx, 31) << std::dec << " pila:" << std::endl;
        imprimirPila(rdram, GPR_U32(ctx, 31), GPR_U32(ctx, 29), 16);
        std::cout.flush();
        g_exitOriginal(rdram, ctx, runtime);
    }

    // Lectura de archivo (func_261620(archivo, a1, a2, a3)): si archivo[8] == 1 hay una operacion pendiente y espera
    // activamente (vaciando la cola de comandos SIF con func_257D70) a que la respuesta del IOP (CORE.IRX) la borre.
    // Registra las entradas con operacion pendiente para saber que archivo se queda esperando.
    constexpr uint32_t kLeerArchivo = 0x00261620u;
    PS2Runtime::RecompiledFunction g_leerArchivoOriginal = nullptr;

    void trazarLeerArchivo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t veces = 0;
        const uint32_t archivo = GPR_U32(ctx, 4);
        if (leer32(rdram, archivo + 8u) == 1u && veces++ < 200u)
        {
            std::cout << "[f1-06:leer] t=" << segundosDesdeArranque() << " archivo=0x" << std::hex << archivo
                      << " a1=0x" << GPR_U32(ctx, 5) << " a2=0x" << GPR_U32(ctx, 6) << " a3=0x" << GPR_U32(ctx, 7)
                      << " campos=";
            for (uint32_t off = 0; off < 0x40u; off += 4u)
                std::cout << leer32(rdram, archivo + off) << (off + 4u < 0x40u ? "," : "");
            std::cout << std::dec << std::endl;
        }
        g_leerArchivoOriginal(rdram, ctx, runtime);
    }

    // ---- Cola de comandos SIF del juego con CORE.IRX ----
    // Cada cola Q (la crea func_257EF0; tabla de colas en 0x4A4080 + 0x10, el archivo guarda su indice en [0x1C]) junta
    // hasta 6 comandos de 16 bytes en un paquete SIF de 0x70 bytes (Q[0x140]: cabecera de 16 bytes con el numero de
    // comandos en +0xC). func_2581C0(Q, cmd, retrollamada, arg) encola; retrollamada y argumento van en
    // Q + 0x148 + n*8 + Q[0x144]*0x30. func_258100(Q, dentro) espera en el semaforo Q[0x24C] a que se haya procesado la
    // respuesta del lote anterior y envia el lote con sceSifSendCmd(Q[0x240], Q[0x140], 0x70). La respuesta de CORE.IRX
    // llega al manejador 0x257FC0 (copia el paquete en Q + 0x1C0 y despierta al hilo de la cola con Q[0x244]); ese hilo
    // (func_257FF8) llama a las retrollamadas (func_258050) y suelta Q[0x24C]. Las lecturas de archivo (func_261620)
    // esperan a que su retrollamada 0x261B18 borre archivo[8].
    // Se anotan los ultimos sucesos en un registro circular y un vigilante lo vuelca si un lote enviado lleva 5 s sin
    // respuesta (o en el segundo F1_COLA_VOLCAR). Los chivatos anotan antes de llamar a la original: si el hilo se
    // bloquea, el planificador deshace la pila del host y luego lo reanuda a mitad de funcion.
    constexpr uint32_t kEncolarComando = 0x002581C0u;
    constexpr uint32_t kEnviarLote = 0x00258100u;
    constexpr uint32_t kRespuestaCola = 0x00257FC0u;
    constexpr uint32_t kProcesarRespuesta = 0x00258050u;
    constexpr uint32_t kRetrollamadaLectura = 0x00261B18u;
    constexpr uint32_t kSceSifSendCmd = 0x002E3F28u;

    struct SucesoCola
    {
        double t = 0.0;
        char tipo = 0;
        uint32_t q = 0;
        uint32_t datos[8] = {};
    };

    struct EstadoCid
    {
        double ultimoEnvio = 0.0;
        double ultimaRespuesta = 0.0;
        uint64_t envios = 0;
        uint64_t respuestas = 0;
        uint32_t q = 0;
        bool volcado = false;
    };

    std::mutex g_colaMutex;
    std::vector<SucesoCola> g_colaSucesos(4096); // fase 3 T7: 512 eran ~2 s con el sonido (cid 1, 50 lotes/s)
    uint64_t g_colaPosicion = 0;
    std::map<uint32_t, EstadoCid> g_colaCids;
    std::map<uint32_t, uint32_t> g_colaPorQ; // Q -> cid
    PS2Runtime::RecompiledFunction g_encolarComandoOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_enviarLoteOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_respuestaColaOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_procesarRespuestaOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_retrollamadaLecturaOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_sceSifSendCmdOriginal = nullptr;

    void anotarCola(char tipo, uint32_t q, std::initializer_list<uint32_t> datos)
    {
        std::lock_guard<std::mutex> lock(g_colaMutex);
        SucesoCola &s = g_colaSucesos[g_colaPosicion++ % g_colaSucesos.size()];
        s.t = segundosDesdeArranque();
        s.tipo = tipo;
        s.q = q;
        size_t i = 0;
        for (const uint32_t d : datos)
            if (i < 8u)
                s.datos[i++] = d;
        for (; i < 8u; ++i)
            s.datos[i] = 0;
    }

    void trazarEncolarComando(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t q = GPR_U32(ctx, 4);
        const uint32_t cmd = GPR_U32(ctx, 5);
        anotarCola('E', q, {leer32(rdram, cmd), leer32(rdram, cmd + 4u), leer32(rdram, cmd + 8u), leer32(rdram, cmd + 12u),
                            GPR_U32(ctx, 6), GPR_U32(ctx, 7), leer32(rdram, q + 0x250u), leer32(rdram, q + 0x144u)});
        g_encolarComandoOriginal(rdram, ctx, runtime);
    }

    void trazarEnviarLote(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t q = GPR_U32(ctx, 4);
        const uint32_t pendientes = leer32(rdram, q + 0x250u);
        if (pendientes != 0u)
        {
            anotarCola('S', q, {pendientes, leer32(rdram, q + 0x144u), GPR_U32(ctx, 5), leer32(rdram, q + 0x240u)});
            std::lock_guard<std::mutex> lock(g_colaMutex);
            g_colaPorQ[q] = leer32(rdram, q + 0x240u);
        }
        g_enviarLoteOriginal(rdram, ctx, runtime);
    }

    void trazarSceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t cid = GPR_U32(ctx, 4);
        const uint32_t paquete = GPR_U32(ctx, 5);
        anotarCola('X', 0u, {cid, paquete, GPR_U32(ctx, 6), leer32(rdram, paquete + 12u), leer32(rdram, paquete + 16u),
                             leer32(rdram, paquete + 20u), leer32(rdram, paquete + 24u), leer32(rdram, paquete + 28u)});
        {
            std::lock_guard<std::mutex> lock(g_colaMutex);
            EstadoCid &estado = g_colaCids[cid];
            ++estado.envios;
            estado.ultimoEnvio = segundosDesdeArranque();
        }
        g_sceSifSendCmdOriginal(rdram, ctx, runtime);
    }

    void trazarRespuestaCola(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t paquete = GPR_U32(ctx, 4);
        const uint32_t q = GPR_U32(ctx, 5);
        const uint32_t cid = leer32(rdram, paquete + 8u);
        anotarCola('R', q, {cid, leer32(rdram, paquete + 12u), leer32(rdram, paquete + 16u), leer32(rdram, paquete + 20u),
                            leer32(rdram, paquete + 24u), leer32(rdram, paquete + 28u), leer32(rdram, q + 0x144u)});
        {
            std::lock_guard<std::mutex> lock(g_colaMutex);
            EstadoCid &estado = g_colaCids[cid];
            ++estado.respuestas;
            estado.ultimaRespuesta = segundosDesdeArranque();
            estado.q = q;
            estado.volcado = false;
        }
        g_respuestaColaOriginal(rdram, ctx, runtime);
    }

    void trazarProcesarRespuesta(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t q = GPR_U32(ctx, 4);
        anotarCola('P', q, {leer32(rdram, q + 0x1CCu), (leer32(rdram, q + 0x144u) + 1u) & 1u});
        g_procesarRespuestaOriginal(rdram, ctx, runtime);
    }

    void trazarRetrollamadaLectura(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t resultado = GPR_U32(ctx, 4);
        const uint32_t archivo = GPR_U32(ctx, 5);
        anotarCola('C', archivo, {leer32(rdram, resultado), leer32(rdram, resultado + 4u), leer32(rdram, resultado + 8u),
                                  leer32(rdram, resultado + 12u), leer32(rdram, archivo + 8u)});
        g_retrollamadaLecturaOriginal(rdram, ctx, runtime);
    }

    void volcarCola(PS2Runtime &runtime, const char *motivo)
    {
        const uint8_t *rdram = runtime.memory().getRDRAM();
        std::vector<SucesoCola> sucesos;
        std::map<uint32_t, EstadoCid> cids;
        std::map<uint32_t, uint32_t> colas;
        {
            std::lock_guard<std::mutex> lock(g_colaMutex);
            const uint64_t total = std::min<uint64_t>(g_colaPosicion, g_colaSucesos.size());
            for (uint64_t i = g_colaPosicion - total; i < g_colaPosicion; ++i)
                sucesos.push_back(g_colaSucesos[i % g_colaSucesos.size()]);
            cids = g_colaCids;
            colas = g_colaPorQ;
        }
        std::ostringstream out;
        out << "[f1-06:cola] VOLCADO (" << motivo << ") t=" << segundosDesdeArranque() << "\n";
        for (const auto &[cid, e] : cids)
            out << "[f1-06:cola]   cid=0x" << std::hex << cid << std::dec << " envios=" << e.envios << " respuestas="
                << e.respuestas << " ultimoEnvio=" << e.ultimoEnvio << " ultimaRespuesta=" << e.ultimaRespuesta << "\n";
        out << std::hex;
        for (const SucesoCola &s : sucesos)
        {
            out << "[f1-06:cola]   t=" << std::dec << s.t << std::hex << " " << s.tipo << " q=0x" << s.q << ":";
            for (const uint32_t d : s.datos)
                out << " " << d;
            out << "\n";
        }
        for (const auto &[q, cid] : colas)
        {
            out << "[f1-06:cola]   Q=0x" << q << " cid=0x" << cid << " [140]=" << leer32(rdram, q + 0x140u)
                << " [144]=" << leer32(rdram, q + 0x144u) << " [1CC]=" << leer32(rdram, q + 0x1CCu) << " [244]="
                << leer32(rdram, q + 0x244u) << " [248]=" << leer32(rdram, q + 0x248u) << " [24C]="
                << leer32(rdram, q + 0x24Cu) << " [250]=" << leer32(rdram, q + 0x250u) << "\n";
            const uint32_t lote = leer32(rdram, q + 0x140u);
            out << "[f1-06:cola]     lote:";
            for (uint32_t off = 0; off < 0x70u; off += 4u)
                out << " " << leer32(rdram, lote + off);
            out << "\n[f1-06:cola]     respuesta:";
            for (uint32_t off = 0; off < 0x70u; off += 4u)
                out << " " << leer32(rdram, q + 0x1C0u + off);
            out << "\n[f1-06:cola]     retrollamadas:";
            for (uint32_t off = 0; off < 0x60u; off += 4u)
                out << " " << leer32(rdram, q + 0x148u + off);
            out << "\n";
        }
        // Montón del runtime (paquetes SIF del IOP al EE, etc.): si guestMalloc falla, las respuestas se pierden.
        const uint32_t prueba = runtime.guestMalloc(112u, 16u);
        out << "[f1-06:cola]   monton runtime: base=0x" << runtime.guestHeapBase() << " fin=0x" << runtime.guestHeapEnd()
            << " limite=0x" << runtime.guestHeapLimit() << " guestMalloc(112)=0x" << prueba << std::dec << "\n";
        if (prueba != 0u)
            runtime.guestFree(prueba);
        std::cerr << out.str() << std::flush;
    }

    void arrancarVigilanteCola(PS2Runtime &runtime)
    {
        // F1_COLA_VOLCAR=<segundo>[,<segundo>...]: un volcado en cada uno (fase 3 T7: antes y despues de un suceso).
        std::vector<double> segundosVolcado;
        if (const char *valor = std::getenv("F1_COLA_VOLCAR"))
        {
            std::stringstream lista(valor);
            std::string elemento;
            while (std::getline(lista, elemento, ','))
                if (const double s = std::atof(elemento.c_str()); s > 0.0)
                    segundosVolcado.push_back(s);
            std::sort(segundosVolcado.begin(), segundosVolcado.end());
        }
        std::thread([&runtime, segundosVolcado]()
                    {
            size_t siguienteVolcado = 0;
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                const double ahora = segundosDesdeArranque();
                if (siguienteVolcado < segundosVolcado.size() && ahora >= segundosVolcado[siguienteVolcado])
                {
                    ++siguienteVolcado;
                    volcarCola(runtime, "F1_COLA_VOLCAR");
                }
                std::string atasco;
                {
                    std::lock_guard<std::mutex> lock(g_colaMutex);
                    for (auto &[cid, e] : g_colaCids)
                        if (!e.volcado && e.ultimoEnvio > e.ultimaRespuesta && ahora - e.ultimoEnvio > 5.0)
                        {
                            e.volcado = true;
                            atasco = "cid " + std::to_string(cid) + " sin respuesta";
                        }
                }
                if (!atasco.empty())
                    volcarCola(runtime, atasco.c_str());
            } })
            .detach();
    }

    void applyFormulaOne06(PS2Runtime &runtime)
    {
        // F1 06 siempre presenta 0/128; con ese plan B, si la instantanea pillaba el bufer 0 recien borrado, la ventana
        // ensenaba el del retrovisor o el del mapa de entorno (FBP 384/439, sin HUD) (parche 0104).
        ps2GsSetBlackDisplayFallback(false);
        // Mapa del circuito del HUD de carrera (parche 0148): el juego dibuja el plano entero (textura PSMT4 de 512x512,
        // girada) y lo recorta con SCISSOR_1 = (20,299)-(99,423), asi que la linea del circuito acaba en un corte
        // recto (tambien en la consola). Esa ventana se dibuja con un fundido de F1_MAPA_FUNDIDO pixeles (por defecto
        // 12; 0 = como el original) hacia sus bordes; los marcadores de los coches, dentro de la misma ventana, tambien.
        {
            const char *valor = std::getenv("F1_MAPA_FUNDIDO");
            const long ancho = valor != nullptr ? std::strtol(valor, nullptr, 10) : 12;
            ps2GsSetScissorFeather(0x01A7012B00630014ull, static_cast<uint32_t>(std::clamp(ancho, 0L, 40L)));
        }
        // Lineas desplazadas junto al texto y los sprites de menus y HUD (parche 0148): el juego muestra cada imagen
        // mezclando el circuito 1 con el otro bufer una linea mas abajo (PMODE 0x8003, MMOD 0: por el alfa de cada
        // pixel; DISPFB2 con DBY 1), y la fuente y otros sprites escribian alfa 0 en sus texeles transparentes, asi
        // que en sus cajas se veia el otro bufer desplazado: el rojo de la barra una linea mas arriba detras de
        // "100%" y la linea oscura bajo la franja a cuadros. En los buferes de pantalla (FBP 0 y 128) esos texeles
        // ya no tocan el alfa (el color no cambia). F1_ALFA_FONDO=0: como el original.
        if (const char *valor = std::getenv("F1_ALFA_FONDO"); valor == nullptr || std::strcmp(valor, "0") != 0)
        {
            const uint32_t buferes[] = {0u, 128u};
            ps2GsSetKeepFrameAlpha(buferes, 2u);
        }
        if (const char *carpeta = std::getenv("F1_PCT_VOLCAR"); carpeta != nullptr && *carpeta != '\0')
            g_volcarPct = carpeta;
        arrancarVolcadoHilos(runtime);
        cargarPulsaciones();
        // Memoria del runtime (fase 5): en la consola el juego tiene toda la RAM hasta su pila (SetupThread con pila -1 y
        // 0x20000 bytes: el monton llega a 0x1FE0000); el runtime le quitaba 1,375 MB (monton hasta 0x1F00000 menos una
        // reserva de 512 KB, y sus pilas de invocacion al final de la RAM). El campeonato se quedaba sin memoria (los
        // bloques de Lua de mas de 1 KB salen del malloc del juego): luaD_throw sin pcall, exit(1) y pantalla en blanco.
        // Ahora el monton del runtime (usa ~6 KB) y las pilas de invocacion (16 KB cada una, ~20 en carrera) van a la
        // RAM del nucleo, que el juego no usa: 0x80000-0x100000 y, si hace falta, 0x20000-0x70000 (0x74000-0x75330 son
        // los parches de nucleo que copia la libkernel). F1_MEMORIA_ANTIGUA=1: como antes (reserva arriba).
        if (const char *antigua = std::getenv("F1_MEMORIA_ANTIGUA"); antigua != nullptr && std::atoi(antigua) != 0)
        {
            ps2_syscalls::setGuestHeapTopReserve(kReservaRuntime);
        }
        else
        {
            ps2_syscalls::setRuntimeLowHeap(0x00080000u, 0x00088000u);
            ps2_syscalls::addRuntimeLowStackArea(0x00088000u, 0x00100000u);
            ps2_syscalls::addRuntimeLowStackArea(0x00020000u, 0x00070000u);
        }
        g_mallocLibre = std::getenv("F1_MALLOC_LIBRE") != nullptr;
        instalarChivato(runtime, kMalloc, trazarMalloc, g_mallocOriginal);
        instalarChivato(runtime, kAyudaPerfil, trazarAyudaPerfil, g_ayudaPerfilOriginal);
        instalarChivato(runtime, kAbrirArchivo, trazarAbrirArchivo, g_abrirArchivoOriginal);
        instalarChivato(runtime, kLeerFuente, leerFuente, g_leerFuenteOriginal);
        instalarChivato(runtime, kLeerFlujoWad, leerFlujoWad, g_leerFlujoWadOriginal);
        instalarChivato(runtime, kCerrarFlujoWad, cerrarFlujoWad, g_cerrarFlujoWadOriginal);
        instalarChivato(runtime, kLanzarDmaVif1, trazarLanzarDmaVif1, g_lanzarDmaVif1Original);
        instalarChivato(runtime, kIrqVif1, trazarIrqVif1, g_irqVif1Original);
        instalarChivato(runtime, kEsperaVif1, trazarEsperaVif1, g_esperaVif1Original);
        instalarChivato(runtime, kPlazoVif1Agotado, trazarPlazoVif1Agotado, g_plazoVif1AgotadoOriginal);
        instalarChivato(runtime, kFinDmaGif, trazarFinDmaGif, g_finDmaGifOriginal);
        instalarChivato(runtime, kHabilitarDmacI, trazarHabilitarDmacI, g_habilitarDmacIOriginal);
        instalarChivato(runtime, kDeshabilitarDmacI, trazarDeshabilitarDmacI, g_deshabilitarDmacIOriginal);
        instalarChivato(runtime, kProgresoCarga, trazarProgresoCarga, g_progresoCargaOriginal);
        instalarChivato(runtime, kMaquinaCarga, maquinaCarga, g_maquinaCargaOriginal);
        instalarChivato(runtime, kQuitarPantallaCarga, quitarPantallaCarga, g_quitarPantallaCargaOriginal);
        std::cout << "[f1-06:carga] cargas_rapidas = " << (cargasRapidas() ? 1 : 0) << std::endl;
        instalarChivato(runtime, kIniciarReloj, trazarIniciarReloj, g_iniciarRelojOriginal);
        instalarChivato(runtime, kIrqReloj, trazarIrqReloj, g_irqRelojOriginal);
        instalarChivato(runtime, 0x002E8DE8u, trazar_scePadPortOpen, g_scePadPortOpenOriginal);
        instalarChivato(runtime, 0x002E91E0u, trazar_scePadRead, g_scePadReadOriginal);
        instalarChivato(runtime, 0x002E9258u, trazar_scePadGetState, g_scePadGetStateOriginal);
        instalarChivato(runtime, 0x002E9510u, trazar_scePadInfoMode, g_scePadInfoModeOriginal);
        instalarChivato(runtime, 0x002E9648u, trazar_scePadSetMainMode, g_scePadSetMainModeOriginal);
        instalarChivato(runtime, 0x002E93F0u, trazar_scePadInfoAct, g_scePadInfoActOriginal);
        instalarChivato(runtime, 0x002E9390u, trazar_scePadGetReqState, g_scePadGetReqStateOriginal);
        instalarChivato(runtime, 0x002E8990u, trazar_scePadInit, g_scePadInitOriginal);
        instalarChivato(runtime, 0x002E9A40u, trazar_scePadGetModVersion, g_scePadGetModVersionOriginal);
        instalarChivato(runtime, kRenderFotograma, trazarRenderFotograma, g_renderFotogramaOriginal);
        instalarChivato(runtime, kFinFotograma, trazarFinFotograma, g_finFotogramaOriginal);
        if (const char *valor = std::getenv("F1_TRAZADA_DIAG"); valor != nullptr && std::atoi(valor) != 0)
        {
            g_trazadaDiag = std::atoi(valor);
            instalarChivato(runtime, kTrazada, trazarTrazada, g_trazadaOriginal);
        }
        if (const char *valor = std::getenv("F1_VIGIA_MATRIZ"); valor != nullptr && std::atoi(valor) != 0)
        {
            g_vigiaRuntime = &runtime;
            g_vigiaModo = std::atoi(valor);
            arrancarVigiaMatriz();
        }
        instalarChivato(runtime, kEsperaVblank, trazarEsperaVblank, g_esperaVblankOriginal);
        if (const char *valor = std::getenv("F1_CALIDAD_V1");
            valor != nullptr && (std::strcmp(valor, "0") == 0 || std::strcmp(valor, "juego") == 0))
            g_calidadV1ComoV0 = false;
        std::cout << "[f1-06] calidad v1 = v0: "
                  << (!g_calidadV1ComoV0 ? "no (F1_CALIDAD_V1)" : detalleCochesPs2() ? "si" : "no")
                  << " (detalle_coches = " << nivelDetalleCoches() << ", distancia_circuito = "
                  << std::lround(factorCircuito() * 100.0f) << ")" << std::endl;
        if (std::getenv("F1_CALIDAD_DIAG") != nullptr)
        {
            instalarChivato(runtime, kMedirFotograma, trazarMedirFotograma, g_medirFotogramaOriginal);
            instalarChivato(runtime, kEfectoAlfa, trazarEfectoAlfa, g_efectoAlfaOriginal);
        }
        else if (g_calidadV1ComoV0) // detalle_coches can change in a race: the hook stays
            instalarChivato(runtime, kMedirFotograma, medirFotograma, g_medirFotogramaOriginal);
        instalarRitmo(runtime);
        instalarLectorBits(runtime);
        {
            const char *valor = std::getenv("F1_VBLANK_ELASTICO");
            const long us = valor != nullptr ? std::atol(valor) : 1L;
            if (us > 0)
                instalarChivato(runtime, kWaitSema, trazarWaitSema, g_waitSemaOriginal);
            std::cout << "[f1-06] VBLANK elastico: " << (us > 0 ? "si" : "no") << std::endl;
        }
        instalarChivato(runtime, kResultadoVisibilidad, trazarResultadoVisibilidad, g_resultadoVisibilidadOriginal);
        if (const char *detalle = std::getenv("F1_VISIBILIDAD_DETALLE"))
        {
            g_visibilidadDetalleDesde = std::atof(detalle);
            instalarChivato(runtime, kLanzarVisibilidad, trazarLanzarVisibilidad, g_lanzarVisibilidadOriginal);
        }
        instalarChivatosLua(runtime);
        runtime.replaceFunction(0x001C080Cu, epilogo_1C080C);
        instalarChivato(runtime, kLeerArchivo, trazarLeerArchivo, g_leerArchivoOriginal);
        instalarChivato(runtime, kExit, trazarExit, g_exitOriginal);
        instalarChivato(runtime, kLuaThrow, trazarLuaThrow, g_luaThrowOriginal);
        if (const char *valor = std::getenv("F1_LUA_MENU_MB"); valor != nullptr && std::atoi(valor) >= 1 && std::atoi(valor) <= 8)
            g_montonLuaMenus = static_cast<uint32_t>(std::atoi(valor)) << 20;
        std::cout << "[f1-06] monton Lua de los menus: " << (g_montonLuaMenus >> 10) << " KB" << std::endl;
        if (const char *valor = std::getenv("F1_LUA_RAM_ALTA"); valor != nullptr)
        {
            if (std::strcmp(valor, "0") == 0)
                ram_alta::g_activa = false;
            else if (std::strcmp(valor, "1") != 0)
                std::cout << "[f1-06] F1_LUA_RAM_ALTA=" << valor << " no vale (solo 0 o 1): queda la RAM alta activa"
                          << std::endl;
        }
        const char *carreraMb = std::getenv("F1_LUA_CARRERA_MB");
        if (carreraMb != nullptr && std::atoi(carreraMb) >= 1 && std::atoi(carreraMb) <= 16)
            g_montonLuaCarrera = static_cast<uint32_t>(std::atoi(carreraMb)) << 20;
        std::cout << "[f1-06] RAM alta (0x2000000-0x3FFFFFF) para los montones de Lua: "
                  << (ram_alta::g_activa ? "si" : "no") << "; monton Lua de la carrera: "
                  << (ram_alta::g_activa ? g_montonLuaCarrera >> 10 : 1536u) << " KB"
                  << (!ram_alta::g_activa && carreraMb != nullptr ? " (F1_LUA_CARRERA_MB no cuenta sin la RAM alta)" : "")
                  << std::endl;
        if (ram_alta::g_activa)
            instalarChivato(runtime, kFree, trazarFree, g_freeOriginal);
        instalarChivato(runtime, kCrearMontonLua, trazarCrearMontonLua, g_crearMontonLuaOriginal);
        if (std::getenv("F1_LUA_MEMORIA") != nullptr)
        {
            instalarChivato(runtime, kLuaRealloc, trazarLuaRealloc, g_luaReallocOriginal);
            instalarChivato(runtime, kLuaFrealloc, trazarLuaFrealloc, g_luaFreallocOriginal);
        }
        instalarCargaLua(runtime);
        instalarSaltoIntro(runtime);
        instalarHud(runtime);
        instalarChivato(runtime, kCamaraCarrera, camaraCarrera, g_camaraCarreraOriginal);
        // detalle_coches and distancia_circuito can change in a race: both hooks stay.
        instalarChivato(runtime, kLodCoches, lodCoches, g_lodCochesOriginal);
        if (g_lodDiag)
            instalarChivato(runtime, kLodPiezas, lodPiezas, g_lodPiezasOriginal);
        instalarChivato(runtime, kFundidoCircuito, fundidoCircuito, g_fundidoCircuitoOriginal);
        instalarChivato(runtime, kPasosCarrera, pasosCarrera, g_pasosCarreraOriginal);
        instalarChivato(runtime, kEfectosVelocidad, efectosVelocidad, g_efectosVelocidadOriginal);
        instalarChivato(runtime, kAnimacionPiloto, animacionPiloto, g_animacionPilotoOriginal);
        if (const char *valor = std::getenv("F1_AZAR_ANFITRION"); valor == nullptr || std::atoi(valor) == 0)
            instalarChivato(runtime, kRandJuego, randJuego, g_randJuegoOriginal);
        std::cout << "[f1-06:azar] rand() " << (g_randJuegoOriginal ? "del juego" : "de la biblioteca del runtime")
                  << std::endl;
        volante::instalar(runtime);
        if (const char *valor = std::getenv("F1_MARCHAS_DIAG"); valor != nullptr && std::atoi(valor) != 0)
        {
            g_marchasDiag = std::atoi(valor);
            instalarChivato(runtime, kPasoCarreraMetodo, pasoCarrera, g_pasoCarreraOriginal);
            instalarChivato(runtime, kRomperMarcha, romperMarcha, g_romperMarchaOriginal);
            instalarChivato(runtime, kAplicarAverias, aplicarAverias, g_aplicarAveriasOriginal);
            instalarChivato(runtime, kPrepararAverias, prepararAverias, g_prepararAveriasOriginal);
            instalarChivato(runtime, kUmbralesAverias, umbralesAverias, g_umbralesAveriasOriginal);
            instalarChivato(runtime, kRepararAverias, repararAverias, g_repararAveriasOriginal);
            instalarChivato(runtime, kRepararTodo, repararTodo, g_repararTodoOriginal);
            if (g_marchasDiag >= 2)
            {
                g_vigiaRuntime = &runtime;
                arrancarVigiaMarchas();
            }
        }
        instalarChivato(runtime, kIaPiloto, iaPiloto, g_iaPilotoOriginal);
        instalarChivato(runtime, kIaCoche, iaCoche, g_iaCocheOriginal);
        instalarChivato(runtime, kPrepararCarrera, prepararCarrera, g_prepararCarreraOriginal);
        std::cout << "[f1-06:ia] dificultad_ia = " << dificultadIaFina() << std::endl;
        std::cout << "[f1-06:fov] fov = " << porcentajeFov() << std::endl;
        instalarChivato(runtime, kVistasCarrera, vistasCarrera, g_vistasCarreraOriginal);
        instalarChivato(runtime, kControlesJugador, controlesJugador, g_controlesJugadorOriginal);
        instalarChivato(runtime, kCamaraOtraVez, camaraOtraVez, g_camaraOtraVezOriginal);
        instalarChivato(runtime, kApagarDirector, apagarDirector, g_apagarDirectorOriginal);
        instalarChivato(runtime, kHudRepeticionDibujar, hudRepeticionDibujar, g_hudRepeticionDibujarOriginal);
        if (std::getenv("F1_CAMARA_DIAG") != nullptr)
        {
            instalarChivato(runtime, kSiguientesCamara[0], siguienteCamaraDiag<0>, g_siguientesCamaraOriginal[0]);
            instalarChivato(runtime, kSiguientesCamara[1], siguienteCamaraDiag<1>, g_siguientesCamaraOriginal[1]);
            instalarChivato(runtime, kSiguientesCamara[2], siguienteCamaraDiag<2>, g_siguientesCamaraOriginal[2]);
            instalarChivato(runtime, kCochesCamaraDiag[0], cocheCamaraDiag<0>, g_cochesCamaraDiagOriginal[0]);
            instalarChivato(runtime, kCochesCamaraDiag[1], cocheCamaraDiag<1>, g_cochesCamaraDiagOriginal[1]);
            instalarChivato(runtime, kCochesCamaraDiag[2], cocheCamaraDiag<2>, g_cochesCamaraDiagOriginal[2]);
            instalarChivato(runtime, kCochesCamaraDiag[3], cocheCamaraDiag<3>, g_cochesCamaraDiagOriginal[3]);
        }
        if (std::getenv("F1_FOV_DIAG") != nullptr)
        {
            instalarChivato(runtime, kCamaraVistaFov, camaraVistaFov, g_camaraVistaFovOriginal);
            instalarChivato(runtime, kCamaraFov, camaraFov, g_camaraFovOriginal);
        }
        instalarChivato(runtime, kEncolarComando, trazarEncolarComando, g_encolarComandoOriginal);
        instalarChivato(runtime, kEnviarLote, trazarEnviarLote, g_enviarLoteOriginal);
        instalarChivato(runtime, kRespuestaCola, trazarRespuestaCola, g_respuestaColaOriginal);
        instalarChivato(runtime, kProcesarRespuesta, trazarProcesarRespuesta, g_procesarRespuestaOriginal);
        instalarChivato(runtime, kRetrollamadaLectura, trazarRetrollamadaLectura, g_retrollamadaLecturaOriginal);
        instalarChivato(runtime, kSceSifSendCmd, trazarSceSifSendCmd, g_sceSifSendCmdOriginal);
        arrancarVigilanteCola(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Formula One 06 (SCES-53950)", "SCES_539.50", 0x00101BE8u, 0u, applyFormulaOne06)
