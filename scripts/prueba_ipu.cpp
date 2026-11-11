// Banco de pruebas del IPU (ps2xRuntime/src/lib/ps2_ipu.cpp): decodifica un fotograma de un video .ipf de F1 06 como
// lo hace el juego (FDEC 0 para leer el byte de cabecera, FDEC 8, IPU_CTRL con sus banderas, SETTH 0 e IDEC) y guarda
// los macrobloques en un BMP, en orden de filas con el ancho que se pida.
// Formato .ipf: "IPUF", version, numero de fotogramas (+8), tamano de cada registro (+0xC); los registros empiezan en
// 0x14 y cada uno lleva el tamano de los datos (+0) y los datos alineados en +0x40.
// Compilar (con entorno.cmd): cl /O2 /EHsc /std:c++17 scripts\prueba_ipu.cpp
//   third_party\PS2Recomp\ps2xRuntime\src\lib\ps2_ipu.cpp /Fe:build\prueba_ipu.exe
// Uso: build\prueba_ipu.exe archivo.ipf fotograma salida.bmp [ancho_en_macrobloques]
#include "../third_party/PS2Recomp/ps2xRuntime/src/lib/ps2_ipu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "uso: prueba_ipu archivo.ipf fotograma salida.bmp [ancho_mb]\n");
        return 1;
    }
    FILE *f = std::fopen(argv[1], "rb");
    if (!f)
        return 1;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;)
        d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &d[o], 4); return v; };
    const uint32_t fotogramas = u32(8);
    const uint32_t registro = u32(0xC);
    const uint32_t k = static_cast<uint32_t>(std::atoi(argv[2]));
    const size_t base = 0x14u + static_cast<size_t>(k) * registro;
    const uint32_t tam = u32(base);
    std::printf("fotogramas=%u registro=0x%x fotograma %u: %u bytes\n", fotogramas, registro, k, tam);

    std::vector<uint8_t> datos(d.begin() + base + 0x40, d.begin() + base + 0x40 + tam);
    datos.resize((datos.size() + 15u) & ~size_t(15));
    ps2_ipu::reset();
    ps2_ipu::writeCmd(0); // BCLR
    ps2_ipu::pushInput(datos.data(), datos.size());
    ps2_ipu::writeCmd(0x40000000u);
    const uint32_t cab = ps2_ipu::readCmd() >> 24;
    ps2_ipu::writeCmd(0x40000008u);
    ps2_ipu::writeCtrl((cab & ~4u) << 16);
    ps2_ipu::writeCmd(0x90000000u);
    ps2_ipu::writeCmd(0x10010000u | (((cab >> 2) & 1u) << 24));
    const size_t bytes = ps2_ipu::outputAvailable();
    const size_t mbs = bytes / 1024u;
    std::printf("cabecera=0x%02x ctrl=0x%08x salida=%zu bytes = %zu macrobloques\n", cab, ps2_ipu::readCtrl(), bytes, mbs);
    std::vector<uint8_t> rgba(bytes);
    ps2_ipu::popOutput(rgba.data(), bytes);

    const size_t anchoMb = argc > 4 ? static_cast<size_t>(std::atoi(argv[4])) : 40u;
    const size_t altoMb = (mbs + anchoMb - 1u) / anchoMb;
    const int w = static_cast<int>(anchoMb * 16u), h = static_cast<int>(altoMb * 16u);
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 3u, 0);
    for (size_t m = 0; m < mbs; ++m)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const uint8_t *p = &rgba[m * 1024u + (y * 16 + x) * 4u];
                const size_t px = (m % anchoMb) * 16u + x, py = (m / anchoMb) * 16u + y;
                uint8_t *q = &img[((h - 1 - py) * w + px) * 3u]; // BMP de abajo arriba, BGR
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
            }
    FILE *o = std::fopen(argv[3], "wb");
    const uint32_t tamImg = static_cast<uint32_t>(img.size());
    uint8_t cabBmp[54] = {'B', 'M'};
    auto p32 = [&](int off, uint32_t v) { std::memcpy(cabBmp + off, &v, 4); };
    p32(2, 54 + tamImg);
    p32(10, 54);
    p32(14, 40);
    p32(18, static_cast<uint32_t>(w));
    p32(22, static_cast<uint32_t>(h));
    cabBmp[26] = 1;
    cabBmp[28] = 24;
    p32(34, tamImg);
    std::fwrite(cabBmp, 1, 54, o);
    std::fwrite(img.data(), 1, img.size(), o);
    std::fclose(o);
    return 0;
}
