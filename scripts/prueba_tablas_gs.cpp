// Compares the GS memory layouts of the runtime (ps2_gs_memory.cpp page tables) with the GS hardware layout as
// PCSX2 describes it (GSTables.cpp: blockTable32/8/4, columnTable32/8/4), byte by byte over a 2x2-page area.
// A texture uploaded as one format and read as another (F1 06 uploads its UI atlas as CT32 and reads it as PSMT8)
// only works if both layouts are the hardware ones; same-format round trips hide a wrong table.
// Compile (with entorno.cmd): cl /O2 /EHsc /std:c++20 /I third_party\PS2Recomp\ps2xRuntime\include
//   scripts\prueba_tablas_gs.cpp third_party\PS2Recomp\ps2xRuntime\src\lib\gs\ps2_gs_memory.cpp /Fe:build\prueba_tablas_gs.exe
// Run: build\prueba_tablas_gs.exe (prints the mismatches per format and exits with 1 if there is any).
#include <cstdint>
#include <cstdio>

#include "runtime/gs/ps2_gs_memory.h"

namespace
{
    // PCSX2 GSTables.cpp
    const uint8_t kBlock32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
                                    {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
    const uint8_t kBlock4[8][4] = {{0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
                                   {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
    const uint8_t kColumn32[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13},         {2, 3, 6, 7, 10, 11, 14, 15},
                                     {16, 17, 20, 21, 24, 25, 28, 29},   {18, 19, 22, 23, 26, 27, 30, 31},
                                     {32, 33, 36, 37, 40, 41, 44, 45},   {34, 35, 38, 39, 42, 43, 46, 47},
                                     {48, 49, 52, 53, 56, 57, 60, 61},   {50, 51, 54, 55, 58, 59, 62, 63}};
    const uint8_t kColumn8[16][16] = {
        {0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
        {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
        {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23},
        {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31},
        {96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86},
        {104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94},
        {65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119},
        {73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127},
        {128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182},
        {136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190},
        {161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151},
        {169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159},
        {224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214},
        {232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222},
        {193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247},
        {201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255}};

    // Byte address in GS memory of pixel (x, y) of a buffer at block `bp` with width `bw` (units of 64 pixels).
    uint32_t hardware32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
    {
        const uint32_t page = bp / 32u + (y / 32u) * bw + x / 64u;
        const uint32_t block = (bp % 32u) + kBlock32[(y / 8u) % 4u][(x / 8u) % 8u];
        return page * 8192u + block * 256u + kColumn32[y % 8u][x % 8u] * 4u;
    }

    uint32_t hardware8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
    {
        const uint32_t page = bp / 32u + (y / 64u) * (bw / 2u) + x / 128u;
        const uint32_t block = (bp % 32u) + kBlock32[(y / 16u) % 4u][(x / 16u) % 8u]; // blockTable8 == blockTable32
        return page * 8192u + block * 256u + kColumn8[y % 16u][x % 16u];
    }

    template <GSMem::PixelStorageMode psm>
    uint32_t runtimeByte(const typename GSMem::PixelStorageTraits<psm>::PageLookupTableT &table, uint32_t bp, uint32_t bw,
                         uint32_t x, uint32_t y)
    {
        const uint32_t pixel = GSMem::PixelStorageTraits<psm>::Address(table, bp, bw, x, y);
        return static_cast<uint32_t>(pixel * GSMem::UnpackedBitWidth(psm) / 8u);
    }
}

int main()
{
    GSMem::InitLookupTables();
    int failures = 0;
    // CT32 128x64 (width 2) and PSMT8 256x128 (width 4) at bases 0, 20 and 14068 (F1 06's UI atlas): a base that does
    // not start a page adds its block offset to every block of the buffer.
    for (const uint32_t bp : {0u, 20u, 14068u})
    {
        unsigned bad32 = 0;
        for (uint32_t y = 0; y < 64u; ++y)
            for (uint32_t x = 0; x < 128u; ++x)
            {
                const uint32_t want = hardware32(bp, 2u, x, y);
                const uint32_t got = runtimeByte<GSMem::C32>(GSMem::PageTableC32, bp, 2u, x, y);
                if (want != got && bad32++ < 4u)
                    std::printf("C32 bp=%u (%u,%u): hardware %u, runtime %u\n", bp, x, y, want, got);
            }
        std::printf("C32 bp=%u: %u of %u pixels differ\n", bp, bad32, 128u * 64u);
        failures += bad32 != 0u;
        unsigned bad8 = 0;
        for (uint32_t y = 0; y < 128u; ++y)
            for (uint32_t x = 0; x < 256u; ++x)
            {
                const uint32_t want = hardware8(bp, 4u, x, y);
                const uint32_t got = runtimeByte<GSMem::P8>(GSMem::PageTableP8, bp, 4u, x, y);
                if (want != got && bad8++ < 4u)
                    std::printf("P8 bp=%u (%u,%u): hardware %u, runtime %u\n", bp, x, y, want, got);
            }
        std::printf("P8 bp=%u: %u of %u texels differ\n", bp, bad8, 256u * 128u);
        failures += bad8 != 0u;
    }
    (void)kBlock4;
    return failures != 0 ? 1 : 0;
}
