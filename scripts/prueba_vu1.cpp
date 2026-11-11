// Banco de pruebas diferencial del interprete de VU1. Ejecuta volcados reales del juego (logs/vu1/run_*.bin, que
// escribe ps2_vu1_core.cpp con F1_VU1_VOLCADO_N/_CADA/_MAX) con dos versiones del interprete y compara bit a bit el
// estado final (registros, banderas, pc y ciclos), la memoria de datos y los paquetes que manda XGKICK. Tambien mide
// cuanto tarda cada version en la misma carga.
//
// La version de referencia (la anterior) se compila aparte con /DVU1Interpreter=VU1InterpreterViejo
// /DVU1State=VU1StateViejo; scripts/prueba_vu1.cmd lo hace todo:
//   scripts\prueba_vu1.cmd <carpeta con los ps2_vu1_*.cpp de referencia> [repeticiones]
// La carpeta de referencia se saca de la base de PS2Recomp con los parches aplicados (ver ESTADO.md).
#include "runtime/ps2_vu1.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"

#undef PS2_VU1_H
#define VU1Interpreter VU1InterpreterViejo
#define VU1State VU1StateViejo
#include "runtime/ps2_vu1.h"
#undef VU1Interpreter
#undef VU1State

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
    std::vector<std::vector<uint8_t>> *g_paquetes = nullptr;
}

// El conmutador A/B del runtime (runtime/ps2_ab.h), que aqui siempre vale 0.
std::atomic<int> g_ps2AbMode{0};
// Los contadores de diagnostico de cada ejecucion (parche 0069), que aqui nadie lee.
std::atomic<uint64_t> g_ps2Vu1HostNs{0};
std::atomic<uint64_t> g_ps2Vu1HostPairs{0};
std::atomic<uint64_t> g_ps2GsEeWaitNs{0};
std::atomic<uint64_t> g_ps2Vu1GsWaitNs{0};

// Lo minimo del runtime que usa el interprete: XGKICK entrega los paquetes a PS2Memory::submitGifPacket (o, sin
// memoria, a GS::processGIFPacket) y la memoria dice cual es la memoria de microcodigo (para la cache de
// decodificacion). Aqui solo se guardan los paquetes.
GS::GS() {}
void GS::processGIFPacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (g_paquetes)
        g_paquetes->emplace_back(data, data + sizeBytes);
}
PS2Memory::PS2Memory() {}
PS2Memory::~PS2Memory() {}
void PS2Memory::submitGifPacket(GifPathId, const uint8_t *data, uint32_t sizeBytes, bool, bool)
{
    if (g_paquetes)
        g_paquetes->emplace_back(data, data + sizeBytes);
}

namespace
{
    struct Volcado
    {
        std::string nombre;
        uint32_t maxCiclos = 0;
        std::vector<uint8_t> estado;
        std::vector<uint8_t> codigo;
        std::vector<uint8_t> datos;
    };

    bool leerVolcado(const std::filesystem::path &ruta, Volcado &v)
    {
        std::ifstream f(ruta, std::ios::binary);
        uint32_t cabecera[6] = {};
        if (!f.read(reinterpret_cast<char *>(cabecera), sizeof(cabecera)) || cabecera[0] != 0x52315556u || cabecera[1] != 1u)
            return false;
        if (cabecera[5] != sizeof(VU1State) || cabecera[5] != sizeof(VU1StateViejo))
        {
            std::printf("%s: VU1State de %u bytes (aqui %zu)\n", ruta.string().c_str(), cabecera[5], sizeof(VU1State));
            return false;
        }
        v.nombre = ruta.filename().string();
        v.maxCiclos = cabecera[4];
        v.estado.resize(cabecera[5]);
        v.codigo.resize(cabecera[2]);
        v.datos.resize(cabecera[3]);
        f.read(reinterpret_cast<char *>(v.estado.data()), v.estado.size());
        f.read(reinterpret_cast<char *>(v.codigo.data()), v.codigo.size());
        f.read(reinterpret_cast<char *>(v.datos.data()), v.datos.size());
        return static_cast<bool>(f);
    }

    struct Resultado
    {
        std::vector<uint8_t> estado;
        std::vector<uint8_t> datos;
        std::vector<std::vector<uint8_t>> paquetes;
        double segundos = 0.0;
        // Parche 0097: el codigo no tiene lectores de MAC ni de estado y el interprete nuevo los deja fuera
        // (PS2X_VU1_BANDERAS_MUERTAS, activo por defecto): se comparan solo los bits D e I del estado.
        bool banderasMuertas = false;
    };

    bool codigoSinLectoresDeBanderas(const std::vector<uint8_t> &codigo)
    {
        static const bool activo = []()
        {
            const char *valor = std::getenv("PS2X_VU1_BANDERAS_MUERTAS");
            return valor == nullptr || valor[0] != '0';
        }();
        if (!activo)
            return false;
        for (size_t k = 0; k + 8u <= codigo.size(); k += 8u)
        {
            uint32_t palabras[2];
            std::memcpy(palabras, codigo.data() + k, 8u);
            if ((palabras[1] >> 31) != 0u)
                continue;
            const uint32_t op = palabras[0] >> 25;
            if (op == 0x14u || op == 0x16u || op == 0x17u || op == 0x18u || op == 0x1Au || op == 0x1Bu)
                return false;
        }
        return true;
    }

    // Una instancia por volcado: la primera ejecucion (la que se compara) construye la cache de decodificacion, como
    // en el juego tras subir el microcodigo; las repeticiones cronometradas la reutilizan.
    template <typename Interprete, typename Estado>
    Resultado ejecutar(const Volcado &v, int repeticiones)
    {
        Resultado r;
        static GS gs;
        static PS2Memory memoria;
        static std::vector<uint8_t> codigo(0x4000u);
        codigo.assign(v.codigo.begin(), v.codigo.end());
        memoria.m_vu1Code = codigo.data();
        memoria.m_vu1CodeGeneration.fetch_add(1u);
        auto vu = std::make_unique<Interprete>(Interprete::Unit::VU1);
        std::vector<uint8_t> datos;
        std::vector<std::vector<uint8_t>> paquetes;
        auto una = [&]()
        {
            std::memcpy(&vu->state(), v.estado.data(), sizeof(Estado));
            datos = v.datos;
            paquetes.clear();
            g_paquetes = &paquetes;
            const Estado inicial = vu->state();
            const auto inicio = std::chrono::steady_clock::now();
            vu->execute(codigo.data(), static_cast<uint32_t>(codigo.size()), datos.data(),
                        static_cast<uint32_t>(datos.size()), gs, &memoria, inicial.pc, inicial.top, inicial.itop,
                        v.maxCiclos);
            r.segundos += std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count();
            g_paquetes = nullptr;
        };
        una();
        r.banderasMuertas = codigoSinLectoresDeBanderas(v.codigo);
        r.estado.resize(sizeof(Estado));
        std::memcpy(r.estado.data(), &vu->state(), sizeof(Estado));
        r.datos = datos;
        r.paquetes = paquetes;
        // Solo el tiempo de execute (sin copiar el estado y los datos de cada repeticion).
        r.segundos = 0.0;
        for (int i = 0; i < repeticiones; ++i)
            una();
        return r;
    }

    std::string primeraDiferencia(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
    {
        if (a.size() != b.size())
            return "tamano " + std::to_string(a.size()) + " frente a " + std::to_string(b.size());
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
            {
                char texto[96];
                std::snprintf(texto, sizeof(texto), "byte 0x%zx: %02x frente a %02x", i, a[i], b[i]);
                return texto;
            }
        return {};
    }

    // ---------------------------------------------------------------------------------------------------------
    // Programas aleatorios ("aleatorio <semilla> <programas>"): registros y memoria con valores especiales (ceros con
    // signo, desnormales, maximos, patrones inf/NaN, valores cuyo producto desborda o subdesborda) e instrucciones al
    // azar de casi todo el repertorio (sin saltos ni XGKICK, que ya cubren los volcados reales), con muchas
    // dependencias entre registros cercanos para que haya esperas, escrituras pendientes y banderas en vuelo.
    struct Azar
    {
        std::mt19937_64 rng;
        explicit Azar(uint64_t semilla) : rng(semilla) {}
        uint32_t n(uint32_t limite) { return static_cast<uint32_t>(rng() % limite); }
        bool p(uint32_t porciento) { return n(100) < porciento; }

        uint32_t flotante()
        {
            static constexpr uint32_t especiales[] = {
                0x00000000u, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x00000001u, 0x807FFFFFu, 0x00800000u,
                0x80800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFFFFFFFu,
                0x1F800000u, 0x9F800000u, 0x5F800000u, 0xDF800000u, 0x5F7FFFFFu, 0x5F800001u, 0x20000000u,
                0x1F000000u, 0x00FFFFFFu, 0x3F7FFFFFu, 0x4B000000u, 0x00000400u, 0x000003FFu, 0x7F000000u,
                0x01000000u, 0x7E800000u};
            const uint32_t tipo = n(100);
            if (tipo < 35)
                return especiales[n(sizeof(especiales) / sizeof(especiales[0]))];
            const uint32_t signo = p(50) ? 0x80000000u : 0u;
            if (tipo < 85) // normales de magnitud moderada
                return signo | ((100u + n(55)) << 23) | n(1u << 23);
            if (tipo < 95) // cualquier exponente
                return signo | (n(256) << 23) | n(1u << 23);
            return n(0x800); // enteros pequenos (datos para FTOI/ITOF/MTIR)
        }

        uint8_t vf() { return static_cast<uint8_t>(p(80) ? n(8) : n(32)); }
        uint8_t vi() { return static_cast<uint8_t>(n(8)); }
        uint32_t dest() { return p(10) ? 0u : 1u + n(15); }

        uint32_t superior()
        {
            const uint32_t ft = vf(), fs = vf(), fd = p(5) ? 0u : vf(), d = dest();
            uint32_t op;
            const uint32_t tipo = n(100);
            if (tipo < 45)
            {
                static constexpr uint8_t normales[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                                       0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x18, 0x19, 0x1A, 0x1B,
                                                       0x1C, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                                                       0x28, 0x29, 0x2A, 0x2C, 0x2D, 0x2E};
                op = normales[n(sizeof(normales))];
            }
            else if (tipo < 55)
            {
                static constexpr uint8_t otros[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1D, 0x1F,
                                                    0x2B, 0x2F};
                op = otros[n(sizeof(otros))];
            }
            else
            {
                // Grupo especial: ADDA.../ITOF/FTOI/ABS/CLIP/NOP (selector en los bits 10-6 y 1-0).
                static constexpr uint8_t especiales[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                                         0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
                                                         0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
                                                         0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                                                         0x28, 0x29, 0x2A, 0x2C, 0x2D, 0x2E, 0x2F};
                const uint32_t sel = especiales[n(sizeof(especiales))];
                return (d << 21) | (ft << 16) | (fs << 11) | ((sel >> 2) << 6) | 0x3Cu | (sel & 3u);
            }
            return (d << 21) | (ft << 16) | (fs << 11) | (fd << 6) | op;
        }

        uint32_t inferiorEspecial(uint32_t sel, uint32_t d, uint32_t ft, uint32_t fs)
        {
            return 0x80000000u | (d << 21) | (ft << 16) | (fs << 11) | ((sel >> 2) << 6) | 0x3Cu | (sel & 3u);
        }

        uint32_t inferior()
        {
            const uint32_t d = dest();
            const uint32_t imm11 = n(0x800);
            switch (n(40))
            {
            case 0: case 1: case 2: return 0x8000033Cu; // NOP
            case 3: case 4: return (0x00u << 25) | (d << 21) | (vf() << 16) | (vi() << 11) | imm11;  // LQ
            case 5: case 6: return (0x01u << 25) | (d << 21) | (vi() << 16) | (vf() << 11) | imm11;  // SQ
            case 7: return (0x04u << 25) | (d << 21) | (vi() << 16) | (vi() << 11) | imm11;          // ILW
            case 8: return (0x05u << 25) | (d << 21) | (vi() << 16) | (vi() << 11) | imm11;          // ISW
            case 9: return (0x08u << 25) | (n(16) << 21) | (vi() << 16) | (vi() << 11) | imm11;     // IADDIU
            case 10: return (0x09u << 25) | (n(16) << 21) | (vi() << 16) | (vi() << 11) | imm11;    // ISUBIU
            case 11: return (static_cast<uint32_t>(0x10u + n(4)) << 25) | n(1u << 24);              // FCEQ/FCSET/FCAND/FCOR
            case 12: return (static_cast<uint32_t>(0x14u + n(4)) << 25) | (n(2) << 21) | (vi() << 16) | n(0x800); // FS*
            case 13: { static constexpr uint32_t f[] = {0x18, 0x1A, 0x1B}; return (f[n(3)] << 25) | (vi() << 16) | (vi() << 11); } // FM*
            case 14: return (0x1Cu << 25) | (vi() << 16);                                            // FCGET
            case 15: { static constexpr uint32_t f[] = {0x30, 0x31, 0x34, 0x35}; return 0x80000000u | (vi() << 16) | (vi() << 11) | (vi() << 6) | f[n(4)]; } // IADD...
            case 16: return 0x80000000u | (vi() << 16) | (vi() << 11) | (n(32) << 6) | 0x32u;       // IADDI
            case 17: case 18: return inferiorEspecial(0x30u + n(2), d, vf(), vf());                  // MOVE/MR32
            case 19: return inferiorEspecial(0x34u + 2u * n(2), d, vf(), vi());                      // LQI/LQD
            case 20: return inferiorEspecial(0x35u + 2u * n(2), d, vi(), vf());                      // SQI/SQD
            case 21: case 22: return inferiorEspecial(0x38u + n(3), n(16), vf(), vf());             // DIV/SQRT/RSQRT
            case 23: return inferiorEspecial(0x3Bu, 0, 0, 0);                                        // WAITQ
            case 24: return inferiorEspecial(0x3Cu, n(4), vi(), vf());                               // MTIR
            case 25: return inferiorEspecial(0x3Du, d, vf(), vi());                                  // MFIR
            case 26: return inferiorEspecial(0x3Eu + n(2), d, vi(), vi());                           // ILWR/ISWR
            case 27: return inferiorEspecial(0x40u + n(2), d, vf(), 0);                              // RNEXT/RGET
            case 28: return inferiorEspecial(0x42u + n(2), n(4) << 0, 0, vf());                       // RINIT/RXOR
            case 29: return inferiorEspecial(0x64u, d, vf(), 0);                                     // MFP
            case 30: return inferiorEspecial(0x68u + n(2), 0, vi(), 0);                              // XTOP/XITOP
            case 31: case 32: case 33:
            {
                static constexpr uint32_t efu[] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x78, 0x79, 0x7A, 0x7C, 0x7D, 0x7E};
                return inferiorEspecial(efu[n(13)], n(4), 0, vf());                                  // EFU
            }
            case 34: return inferiorEspecial(0x7Bu, 0, 0, 0);                                        // WAITP
            default: return 0x8000033Cu;
            }
        }
    };

    Volcado programaAleatorio(Azar &azar, uint32_t numero)
    {
        Volcado v;
        v.nombre = "aleatorio_" + std::to_string(numero);
        v.maxCiclos = 200000u;
        VU1State estado{};
        for (uint32_t r = 1; r < 32u; ++r)
            for (uint32_t c = 0; c < 4u; ++c)
            {
                const uint32_t bits = azar.flotante();
                std::memcpy(&estado.vf[r][c], &bits, 4);
            }
        estado.vf[0][3] = 1.0f;
        for (uint32_t r = 1; r < 16u; ++r)
            estado.vi[r] = static_cast<int32_t>(azar.n(1024));
        for (uint32_t c = 0; c < 4u; ++c)
        {
            const uint32_t bits = azar.flotante();
            std::memcpy(&estado.acc[c], &bits, 4);
        }
        uint32_t bits = azar.flotante();
        std::memcpy(&estado.q, &bits, 4);
        bits = azar.flotante();
        std::memcpy(&estado.p, &bits, 4);
        bits = azar.flotante();
        std::memcpy(&estado.i, &bits, 4);
        estado.r = 0x3F800000u | azar.n(1u << 23);
        estado.mac = azar.n(0x10000);
        estado.status = azar.n(0x1000);
        estado.clip = azar.n(1u << 24);
        estado.top = azar.n(1024);
        estado.itop = azar.n(1024);
        v.estado.resize(sizeof(VU1State));
        std::memcpy(v.estado.data(), &estado, sizeof(VU1State));

        v.datos.resize(0x4000u);
        for (size_t i = 0; i < v.datos.size(); i += 4)
        {
            const uint32_t palabra = azar.flotante();
            std::memcpy(v.datos.data() + i, &palabra, 4);
        }

        v.codigo.assign(0x4000u, 0u);
        const uint32_t pares = 16u + azar.n(240);
        for (uint32_t k = 0; k < pares + 2u; ++k)
        {
            uint32_t inferior = 0x8000033Cu;
            uint32_t superior = 0x000002FFu; // NOP
            if (k < pares)
            {
                superior = azar.superior();
                if (azar.p(8))
                {
                    superior |= 0x80000000u; // bit I: el inferior es el valor de I
                    inferior = azar.flotante();
                }
                else
                {
                    inferior = azar.inferior();
                }
            }
            else if (k == pares)
            {
                superior |= 0x40000000u; // bit E
            }
            std::memcpy(v.codigo.data() + k * 8u, &inferior, 4);
            std::memcpy(v.codigo.data() + k * 8u + 4u, &superior, 4);
        }
        return v;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Programas para los atajos del FMAC ("fmac <semilla> <programas>"): grupos de ADDAx (ACC = c), MADD/MSUB/OPMSUB
    // (vt, bc, Q o I) y, cuatro pares despues, MAC, estado y resultado guardados en la memoria de datos (FMAND, FSAND,
    // ISW, SQ), de modo que se comparan los de cada operacion. c se elige junto a -+a*b (cancelacion de 0 a 23
    // binades, +-k ulps, exacta) y los productos caen a menudo cerca de los exponentes 2 y 253, que es donde el atajo
    // de MADD/MSUB deja de valer.
    float desdeBits(uint32_t bits)
    {
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    uint32_t aBits(float f)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return bits;
    }

    struct AzarFmac : Azar
    {
        using Azar::Azar;

        float normal(int exponente)
        {
            exponente = std::clamp(exponente, 1, 254);
            return desdeBits((p(50) ? 0x80000000u : 0u) | (static_cast<uint32_t>(exponente) << 23) | n(1u << 23));
        }
        // Un par (a, b) cuyo producto tiene a menudo el exponente cerca de los bordes.
        void operandos(float &a, float &b)
        {
            if (p(15))
            {
                // Un operando cero exacto (el producto es cero: parche 0062).
                a = p(50) ? -0.0f : 0.0f;
                b = p(20) ? (p(50) ? -0.0f : 0.0f) : normal(1 + static_cast<int>(n(254)));
                if (p(50))
                    std::swap(a, b);
                return;
            }
            int objetivo;
            const uint32_t tipo = n(100);
            if (tipo < 25)
                objetivo = static_cast<int>(n(8)); // 0..7: subdesborde y borde de abajo
            else if (tipo < 50)
                objetivo = 249 + static_cast<int>(n(8)); // 249..256: borde de arriba y desborde
            else if (tipo < 90)
                objetivo = 100 + static_cast<int>(n(55));
            else
                objetivo = static_cast<int>(n(257));
            const int bajo = std::max(1, objetivo - 127), alto = std::min(254, objetivo + 126);
            const int ea = bajo + static_cast<int>(n(static_cast<uint32_t>(std::max(1, alto - bajo + 1))));
            a = normal(ea);
            b = normal(objetivo - ea + 127);
        }
        // a*b redondeado hacia cero en float (el producto pf del FMAC).
        static float productoHaciaCero(float a, float b)
        {
            const double exacto = static_cast<double>(a) * static_cast<double>(b);
            float pf = static_cast<float>(exacto);
            if (std::fabs(static_cast<double>(pf)) > std::fabs(exacto))
                pf = std::nextafter(pf, 0.0f);
            return pf;
        }
        // c junto a +-pf (signo = +1 para MADD, que hace c + pf; -1 para MSUB/OPMSUB, c - pf).
        float acumulador(float pf, int signo)
        {
            if (p(12))
                return p(50) ? -0.0f : 0.0f; // c cero (con un producto cero, el resultado es un cero exacto)
            if (!std::isfinite(pf) || pf == 0.0f || p(10))
                return normal(static_cast<int>(n(254)) + 1);
            const double base = -signo * static_cast<double>(pf);
            const double u = std::fabs(static_cast<double>(std::nextafter(std::fabs(pf), INFINITY)) - std::fabs(pf));
            switch (n(5))
            {
            case 0:
                return static_cast<float>(base); // cancelacion exacta: v = 0
            case 1: // +-k ulps: |c| mayor o menor que |pf|
                return static_cast<float>(base + (static_cast<int>(n(7)) - 3) * u);
            case 2: // c - (-+pf) = 2^r ulps: v entre 23 y 0 binades por debajo de pf
                return static_cast<float>(base + (p(50) ? 1.0 : -1.0) * std::ldexp(u, static_cast<int>(n(24))));
            case 3: // c pequeno frente a pf, de cualquier signo
                return static_cast<float>(base * std::ldexp(1.0, -static_cast<int>(n(40))) * (p(50) ? 1.0 : -1.0));
            default: // c grande frente a pf
                return static_cast<float>(base * std::ldexp(1.0, static_cast<int>(n(40))));
            }
        }
    };

    Volcado programaFmac(AzarFmac &azar, uint32_t numero)
    {
        Volcado v;
        v.nombre = "fmac_" + std::to_string(numero);
        v.maxCiclos = 200000u;
        VU1State estado{};
        estado.vf[0][3] = 1.0f;
        float q = 0.0f, i = 0.0f, nada = 0.0f;
        azar.operandos(q, nada);
        azar.operandos(i, nada);
        estado.q = q;
        estado.i = i;
        estado.vi[2] = 0xFFFF;
        v.datos.assign(0x4000u, 0u);
        v.codigo.assign(0x4000u, 0u);
        uint32_t k = 0;
        const auto par = [&](uint32_t superior, uint32_t inferior)
        {
            std::memcpy(v.codigo.data() + k * 8u, &inferior, 4);
            std::memcpy(v.codigo.data() + k * 8u + 4u, &superior, 4);
            ++k;
        };
        constexpr uint32_t kNop = 0x000002FFu;
        constexpr uint32_t kNopInf = 0x8000033Cu;
        const uint32_t grupos = 8u;
        for (uint32_t g = 0; g < grupos; ++g)
        {
            const uint32_t ra = 1u + g, rb = 9u + g, rc = 17u + g, rd = 25u + (g % 7u);
            // Operacion: 0 MADD, 1 MSUB, 2 MADDbc, 3 MSUBbc, 4 MADDq/i, 5 MSUBq/i, 6 OPMSUB.
            const uint32_t op = azar.n(7);
            const uint32_t bc = azar.n(4);
            const bool usaI = azar.p(50);
            uint32_t dest = 1u + azar.n(15);
            if (op == 6 && azar.p(60))
                dest &= ~1u; // sin w: el atajo de OPMSUB
            if (dest == 0u)
                dest = 0x8u;
            const int signo = (op == 0 || op == 2 || op == 4) ? 1 : -1;
            float a[4], b[4], c[4];
            for (uint32_t l = 0; l < 4u; ++l)
                azar.operandos(a[l], b[l]);
            for (uint32_t l = 0; l < 4u; ++l)
            {
                // El producto que usa cada carril de la operacion.
                float x = a[l], y = b[l];
                if (op == 2 || op == 3)
                    y = b[bc];
                else if (op == 4 || op == 5)
                    y = usaI ? i : q;
                else if (op == 6)
                {
                    static constexpr uint32_t ca[4] = {1, 2, 0, 3}, cb[4] = {2, 0, 1, 3};
                    x = a[ca[l]];
                    y = b[cb[l]];
                }
                c[l] = azar.acumulador(AzarFmac::productoHaciaCero(x, y), signo);
            }
            std::memcpy(estado.vf[ra], a, 16);
            std::memcpy(estado.vf[rb], b, 16);
            std::memcpy(estado.vf[rc], c, 16);
            // ADDAx.xyzw ACC, vfC, vf0x: ACC = c.
            par((0xFu << 21) | (0u << 16) | (rc << 11) | (0u << 6) | 0x3Cu | 0u, kNopInf);
            uint32_t sel;
            switch (op)
            {
            case 0: sel = 0x29u; break;                  // MADD
            case 1: sel = 0x2Du; break;                  // MSUB
            case 2: sel = 0x08u + bc; break;             // MADDbc
            case 3: sel = 0x0Cu + bc; break;             // MSUBbc
            case 4: sel = usaI ? 0x23u : 0x21u; break;   // MADDi / MADDq
            case 5: sel = usaI ? 0x27u : 0x25u; break;   // MSUBi / MSUBq
            default: sel = 0x2Eu; break;                 // OPMSUB
            }
            par((dest << 21) | (rb << 16) | (ra << 11) | (rd << 6) | sel, kNopInf);
            for (uint32_t espera = 0; espera < 4u; ++espera)
                par(kNop, kNopInf);
            par(kNop, (0x1Au << 25) | (1u << 16) | (2u << 11));                         // FMAND vi1, vi2 (MAC)
            par(kNop, (0x05u << 25) | (0x8u << 21) | (1u << 16) | (0u << 11) | (3u * g)); // ISW.x vi1, 3g(vi0)
            par(kNop, (0x16u << 25) | (1u << 21) | (1u << 16) | 0x7FFu);                // FSAND vi1, 0xFFF (estado)
            par(kNop, (0x05u << 25) | (0x4u << 21) | (1u << 16) | (0u << 11) | (3u * g)); // ISW.y vi1, 3g(vi0)
            par(kNop, (0x01u << 25) | (0xFu << 21) | (0u << 16) | (rd << 11) | (3u * g + 1u)); // SQ vfD, 3g+1(vi0)
            par(kNop, (0x01u << 25) | (0xFu << 21) | (0u << 16) | (rc << 11) | (3u * g + 2u)); // SQ vfC (ACC de entrada)
        }
        par(kNop | 0x40000000u, kNopInf); // bit E
        par(kNop, kNopInf);
        v.estado.resize(sizeof(VU1State));
        std::memcpy(v.estado.data(), &estado, sizeof(VU1State));
        return v;
    }
}

namespace
{
    std::string diferencia(const Resultado &viejo, const Resultado &nuevo)
    {
        std::string fallo;
        std::vector<uint8_t> estadoNuevo = nuevo.estado;
        if (nuevo.banderasMuertas && estadoNuevo.size() == sizeof(VU1State) && viejo.estado.size() == sizeof(VU1State))
        {
            VU1State a{}, b{};
            std::memcpy(&a, viejo.estado.data(), sizeof(VU1State));
            std::memcpy(&b, estadoNuevo.data(), sizeof(VU1State));
            b.mac = a.mac;
            b.status = (b.status & 0xC30u) | (a.status & ~0xC30u);
            std::memcpy(estadoNuevo.data(), &b, sizeof(VU1State));
        }
        if (std::string d = primeraDiferencia(viejo.estado, estadoNuevo); !d.empty())
            fallo = "estado (" + d + ")";
        else if (std::string d = primeraDiferencia(viejo.datos, nuevo.datos); !d.empty())
            fallo = "memoria de datos (" + d + ")";
        else if (viejo.paquetes.size() != nuevo.paquetes.size())
            fallo = "paquetes XGKICK: " + std::to_string(viejo.paquetes.size()) + " frente a " +
                    std::to_string(nuevo.paquetes.size());
        else
            for (size_t i = 0; i < viejo.paquetes.size() && fallo.empty(); ++i)
                if (std::string d = primeraDiferencia(viejo.paquetes[i], nuevo.paquetes[i]); !d.empty())
                    fallo = "paquete XGKICK " + std::to_string(i) + " (" + d + ")";
        return fallo;
    }

    int aleatorio(uint64_t semilla, uint32_t programas, bool fmac = false)
    {
        Azar azar(semilla);
        AzarFmac azarFmac(semilla);
        size_t iguales = 0, distintos = 0;
        uint64_t ciclos = 0;
        for (uint32_t k = 0; k < programas; ++k)
        {
            const Volcado v = fmac ? programaFmac(azarFmac, k) : programaAleatorio(azar, k);
            const Resultado viejo = ejecutar<VU1InterpreterViejo, VU1StateViejo>(v, 0);
            const Resultado nuevo = ejecutar<VU1Interpreter, VU1State>(v, 0);
            VU1StateViejo final{};
            std::memcpy(&final, viejo.estado.data(), sizeof(final));
            ciclos += final.cycles;
            const std::string fallo = diferencia(viejo, nuevo);
            if (fallo.empty())
            {
                ++iguales;
                continue;
            }
            if (++distintos <= 20u)
                std::printf("semilla %llu, programa %u: DISTINTO %s\n", static_cast<unsigned long long>(semilla), k,
                            fallo.c_str());
            if (distintos <= 3u)
            {
                // Guarda el programa para repetirlo con los volcados (logs/vu1-fallos).
                std::filesystem::create_directories("logs/vu1-fallos");
                const std::string ruta = "logs/vu1-fallos/" + v.nombre + "_" + std::to_string(semilla) + ".bin";
                if (FILE *f = std::fopen(ruta.c_str(), "wb"))
                {
                    const uint32_t cabecera[6] = {0x52315556u, 1u, static_cast<uint32_t>(v.codigo.size()),
                                                  static_cast<uint32_t>(v.datos.size()), v.maxCiclos,
                                                  static_cast<uint32_t>(sizeof(VU1State))};
                    std::fwrite(cabecera, sizeof(cabecera), 1, f);
                    std::fwrite(v.estado.data(), 1, v.estado.size(), f);
                    std::fwrite(v.codigo.data(), 1, v.codigo.size(), f);
                    std::fwrite(v.datos.data(), 1, v.datos.size(), f);
                    std::fclose(f);
                }
            }
        }
        std::printf("\n%s (semilla %llu): %zu iguales, %zu distintos, %llu ciclos en total\n", fmac ? "fmac" : "aleatorio",
                    static_cast<unsigned long long>(semilla), iguales, distintos,
                    static_cast<unsigned long long>(ciclos));
        return distintos == 0u ? 0 : 1;
    }
}

namespace
{
    // Diferencias campo a campo entre dos estados finales.
    void mostrarDiferencias(const Resultado &viejo, const Resultado &nuevo)
    {
        VU1StateViejo a{};
        VU1State b{};
        std::memcpy(&a, viejo.estado.data(), sizeof(a));
        std::memcpy(&b, nuevo.estado.data(), sizeof(b));
        for (uint32_t r = 0; r < 32u; ++r)
            for (uint32_t c = 0; c < 4u; ++c)
            {
                uint32_t x = 0, y = 0;
                std::memcpy(&x, &a.vf[r][c], 4);
                std::memcpy(&y, &b.vf[r][c], 4);
                if (x != y)
                    std::printf("  vf%u.%c: %08x frente a %08x\n", r, "xyzw"[c], x, y);
            }
        for (uint32_t r = 0; r < 16u; ++r)
            if (a.vi[r] != b.vi[r])
                std::printf("  vi%u: %08x frente a %08x\n", r, static_cast<uint32_t>(a.vi[r]), static_cast<uint32_t>(b.vi[r]));
        for (uint32_t c = 0; c < 4u; ++c)
        {
            uint32_t x = 0, y = 0;
            std::memcpy(&x, &a.acc[c], 4);
            std::memcpy(&y, &b.acc[c], 4);
            if (x != y)
                std::printf("  acc.%c: %08x frente a %08x\n", "xyzw"[c], x, y);
        }
        const auto f32 = [](float v) { uint32_t x = 0; std::memcpy(&x, &v, 4); return x; };
        if (f32(a.q) != f32(b.q)) std::printf("  q: %08x frente a %08x\n", f32(a.q), f32(b.q));
        if (f32(a.p) != f32(b.p)) std::printf("  p: %08x frente a %08x\n", f32(a.p), f32(b.p));
        if (f32(a.i) != f32(b.i)) std::printf("  i: %08x frente a %08x\n", f32(a.i), f32(b.i));
        if (a.r != b.r) std::printf("  r: %08x frente a %08x\n", a.r, b.r);
        if (a.mac != b.mac) std::printf("  mac: %04x frente a %04x\n", a.mac, b.mac);
        if (a.status != b.status) std::printf("  status: %03x frente a %03x\n", a.status, b.status);
        if (a.clip != b.clip) std::printf("  clip: %06x frente a %06x\n", a.clip, b.clip);
        if (a.pc != b.pc) std::printf("  pc: %x frente a %x\n", a.pc, b.pc);
        if (a.cycles != b.cycles)
            std::printf("  ciclos: %llu frente a %llu\n", static_cast<unsigned long long>(a.cycles),
                        static_cast<unsigned long long>(b.cycles));
        for (size_t i = 0; i < viejo.datos.size() && i < nuevo.datos.size(); i += 4)
            if (std::memcmp(viejo.datos.data() + i, nuevo.datos.data() + i, 4) != 0)
            {
                uint32_t x = 0, y = 0;
                std::memcpy(&x, viejo.datos.data() + i, 4);
                std::memcpy(&y, nuevo.datos.data() + i, 4);
                std::printf("  datos[0x%zx]: %08x frente a %08x\n", i, x, y);
            }
    }

    // "reducir <volcado>": para un programa sin saltos (los aleatorios), el prefijo mas corto que ya da distinto:
    // corta el programa con el bit E en el par n y compara los dos interpretes.
    int reducir(const char *ruta)
    {
        Volcado original;
        if (!leerVolcado(ruta, original))
        {
            std::printf("No se puede leer %s\n", ruta);
            return 1;
        }
        VU1State inicial{};
        std::memcpy(&inicial, original.estado.data(), sizeof(inicial));
        const uint32_t inicio = inicial.pc / 8u;
        for (uint32_t n = inicio; n < original.codigo.size() / 8u - 2u; ++n)
        {
            Volcado v = original;
            const uint32_t nop = 0x8000033Cu, nopSuperior = 0x000002FFu | 0x40000000u, nopFinal = 0x000002FFu;
            std::memcpy(v.codigo.data() + n * 8u, &nop, 4);
            std::memcpy(v.codigo.data() + n * 8u + 4u, &nopSuperior, 4);
            std::memcpy(v.codigo.data() + n * 8u + 8u, &nop, 4);
            std::memcpy(v.codigo.data() + n * 8u + 12u, &nopFinal, 4);
            const Resultado viejo = ejecutar<VU1InterpreterViejo, VU1StateViejo>(v, 0);
            const Resultado nuevo = ejecutar<VU1Interpreter, VU1State>(v, 0);
            if (!diferencia(viejo, nuevo).empty())
            {
                std::printf("El prefijo mas corto que da distinto acaba en el par %u (pc 0x%x). Pares:\n", n, n * 8u);
                for (uint32_t k = (n > inicio + 12u ? n - 12u : inicio); k < n; ++k)
                {
                    uint32_t inferior = 0, superior = 0;
                    std::memcpy(&inferior, v.codigo.data() + k * 8u, 4);
                    std::memcpy(&superior, v.codigo.data() + k * 8u + 4u, 4);
                    std::printf("  0x%04x: superior %08x inferior %08x\n", k * 8u, superior, inferior);
                }
                std::printf("Diferencias (viejo frente a nuevo):\n");
                mostrarDiferencias(viejo, nuevo);
                // El programa reducido, para repetirlo.
                if (FILE *f = std::fopen("logs/vu1-fallos/reducido.bin", "wb"))
                {
                    const uint32_t cabecera[6] = {0x52315556u, 1u, static_cast<uint32_t>(v.codigo.size()),
                                                  static_cast<uint32_t>(v.datos.size()), v.maxCiclos,
                                                  static_cast<uint32_t>(sizeof(VU1State))};
                    std::fwrite(cabecera, sizeof(cabecera), 1, f);
                    std::fwrite(v.estado.data(), 1, v.estado.size(), f);
                    std::fwrite(v.codigo.data(), 1, v.codigo.size(), f);
                    std::fwrite(v.datos.data(), 1, v.datos.size(), f);
                    std::fclose(f);
                }
                return 0;
            }
        }
        std::printf("Ningun prefijo da distinto\n");
        return 0;
    }
}

namespace
{
    // "seguidos <carpeta> [vueltas]": todos los volcados, uno tras otro, sobre el mismo interprete de cada version
    // (como en el juego, que va cargando microprogramas distintos en las mismas direcciones): ejercita las cachés que
    // se validan contra el codigo actual.
    int seguidos(const char *carpeta, int vueltas)
    {
        std::vector<Volcado> volcados;
        std::vector<std::filesystem::path> rutas;
        for (const auto &e : std::filesystem::directory_iterator(carpeta))
            if (e.path().extension() == ".bin")
                rutas.push_back(e.path());
        std::sort(rutas.begin(), rutas.end());
        for (const auto &ruta : rutas)
        {
            Volcado v;
            if (leerVolcado(ruta, v))
                volcados.push_back(std::move(v));
        }
        static GS gs;
        static PS2Memory memoria;
        static std::vector<uint8_t> codigo(0x4000u);
        auto viejo = std::make_unique<VU1InterpreterViejo>(VU1InterpreterViejo::Unit::VU1);
        auto nuevo = std::make_unique<VU1Interpreter>(VU1Interpreter::Unit::VU1);
        size_t iguales = 0, distintos = 0;
        double tiempoViejo = 0.0, tiempoNuevo = 0.0;
        for (int vuelta = 0; vuelta < vueltas; ++vuelta)
            for (const Volcado &v : volcados)
            {
                // Solo cambia la generacion si el codigo cambia, como con MPG.
                if (std::memcmp(codigo.data(), v.codigo.data(), v.codigo.size()) != 0)
                {
                    codigo.assign(v.codigo.begin(), v.codigo.end());
                    memoria.m_vu1CodeGeneration.fetch_add(1u);
                }
                memoria.m_vu1Code = codigo.data();
                Resultado r[2];
                for (int cual = 0; cual < 2; ++cual)
                {
                    std::vector<uint8_t> datos = v.datos;
                    g_paquetes = &r[cual].paquetes;
                    const auto inicio = std::chrono::steady_clock::now();
                    if (cual == 0)
                    {
                        std::memcpy(&viejo->state(), v.estado.data(), sizeof(VU1StateViejo));
                        const VU1StateViejo inicial = viejo->state();
                        viejo->execute(codigo.data(), static_cast<uint32_t>(codigo.size()), datos.data(),
                                       static_cast<uint32_t>(datos.size()), gs, &memoria, inicial.pc, inicial.top,
                                       inicial.itop, v.maxCiclos);
                        r[cual].estado.resize(sizeof(VU1StateViejo));
                        std::memcpy(r[cual].estado.data(), &viejo->state(), sizeof(VU1StateViejo));
                    }
                    else
                    {
                        std::memcpy(&nuevo->state(), v.estado.data(), sizeof(VU1State));
                        const VU1State inicial = nuevo->state();
                        nuevo->execute(codigo.data(), static_cast<uint32_t>(codigo.size()), datos.data(),
                                       static_cast<uint32_t>(datos.size()), gs, &memoria, inicial.pc, inicial.top,
                                       inicial.itop, v.maxCiclos);
                        r[cual].estado.resize(sizeof(VU1State));
                        std::memcpy(r[cual].estado.data(), &nuevo->state(), sizeof(VU1State));
                        r[cual].banderasMuertas = codigoSinLectoresDeBanderas(v.codigo);
                    }
                    (cual == 0 ? tiempoViejo : tiempoNuevo) +=
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count();
                    g_paquetes = nullptr;
                    r[cual].datos = std::move(datos);
                }
                // El reloj de cada interprete sigue de una ejecucion a otra: si todo es igual, avanzan igual.
                const std::string fallo = diferencia(r[0], r[1]);
                if (fallo.empty())
                    ++iguales;
                else if (++distintos <= 10u)
                    std::printf("vuelta %d, %s: DISTINTO %s\n", vuelta, v.nombre.c_str(), fallo.c_str());
            }
        std::printf("seguidos: %zu iguales, %zu distintos. Tiempo: viejo %.3f s, nuevo %.3f s (x%.2f)\n", iguales,
                    distintos, tiempoViejo, tiempoNuevo, tiempoNuevo > 0.0 ? tiempoViejo / tiempoNuevo : 0.0);
        return distintos == 0u ? 0 : 1;
    }
}

int main(int argc, char **argv)
{
    if (argc > 2 && std::string(argv[1]) == "seguidos")
        return seguidos(argv[2], argc > 3 ? std::max(1, std::atoi(argv[3])) : 10);
    if (argc > 1 && std::string(argv[1]) == "fmac")
        return aleatorio(argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 1u,
                         argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)) : 20000u, true);
    if (argc > 1 && std::string(argv[1]) == "aleatorio")
        return aleatorio(argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 1u,
                         argc > 3 ? static_cast<uint32_t>(std::max(1, std::atoi(argv[3]))) : 1000u);
    if (argc > 2 && std::string(argv[1]) == "reducir")
        return reducir(argv[2]);

    const std::filesystem::path carpeta = argc > 1 ? argv[1] : "logs/vu1";
    const int repeticiones = argc > 2 ? std::max(1, std::atoi(argv[2])) : 10;
    // "solo-nuevo": solo ejecuta la version actual (para perfilarla con build\perfil.exe sin la de referencia).
    const bool soloNuevo = argc > 3 && std::string(argv[3]) == "solo-nuevo";
    std::vector<std::filesystem::path> rutas;
    for (const auto &e : std::filesystem::directory_iterator(carpeta))
        if (e.path().extension() == ".bin")
            rutas.push_back(e.path());
    std::sort(rutas.begin(), rutas.end());
    if (soloNuevo)
    {
        double total = 0.0;
        for (const auto &ruta : rutas)
        {
            Volcado v;
            if (leerVolcado(ruta, v))
                total += ejecutar<VU1Interpreter, VU1State>(v, repeticiones).segundos;
        }
        std::printf("solo-nuevo: %.3f s\n", total);
        return 0;
    }

    size_t iguales = 0, distintos = 0;
    double tiempoViejo = 0.0, tiempoNuevo = 0.0;
    for (const auto &ruta : rutas)
    {
        Volcado v;
        if (!leerVolcado(ruta, v))
            continue;
        const Resultado viejo = ejecutar<VU1InterpreterViejo, VU1StateViejo>(v, repeticiones);
        const Resultado nuevo = ejecutar<VU1Interpreter, VU1State>(v, repeticiones);
        tiempoViejo += viejo.segundos;
        tiempoNuevo += nuevo.segundos;

        const std::string fallo = diferencia(viejo, nuevo);
        VU1StateViejo final{};
        std::memcpy(&final, viejo.estado.data(), sizeof(final));
        if (fallo.empty())
        {
            ++iguales;
            std::printf("%-22s igual    ciclos=%-7llu paquetes=%-4zu viejo=%8.1f us nuevo=%8.1f us\n", v.nombre.c_str(),
                        static_cast<unsigned long long>(final.cycles), viejo.paquetes.size(),
                        1e6 * viejo.segundos / repeticiones, 1e6 * nuevo.segundos / repeticiones);
        }
        else
        {
            ++distintos;
            std::printf("%-22s DISTINTO %s\n", v.nombre.c_str(), fallo.c_str());
        }
    }
    std::printf("\n%zu iguales, %zu distintos. Tiempo total: viejo %.3f s, nuevo %.3f s (x%.2f)\n", iguales, distintos,
                tiempoViejo, tiempoNuevo, tiempoNuevo > 0.0 ? tiempoViejo / tiempoNuevo : 0.0);
    return distintos == 0u ? 0 : 1;
}
