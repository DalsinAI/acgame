#include "acgame/acgame.h"

int acgame_c2p8_ref(const uint8_t *chunky, size_t chunky_pitch,
                    uint8_t *planes[8], size_t plane_pitch,
                    unsigned width, unsigned height) {
    unsigned y, x, p, i;
    if (!chunky || !planes || width == 0 || height == 0 || (width & 7u)) {
        return -1;
    }
    for (p = 0; p < 8; ++p) {
        if (!planes[p]) return -1;
    }
    for (y = 0; y < height; ++y) {
        const uint8_t *src = chunky + (size_t)y * chunky_pitch;
        for (x = 0; x < width; x += 8) {
            uint8_t packed[8] = {0,0,0,0,0,0,0,0};
            for (i = 0; i < 8; ++i) {
                const uint8_t px = src[x + i];
                const uint8_t mask = (uint8_t)(0x80u >> i);
                for (p = 0; p < 8; ++p) {
                    if (px & (1u << p)) packed[p] |= mask;
                }
            }
            for (p = 0; p < 8; ++p) {
                planes[p][(size_t)y * plane_pitch + (x >> 3)] = packed[p];
            }
        }
    }
    return 0;
}

int acgame_p2c8_ref(uint8_t *chunky, size_t chunky_pitch,
                    const uint8_t *planes[8], size_t plane_pitch,
                    unsigned width, unsigned height) {
    unsigned y, x, p;
    if (!chunky || !planes || width == 0 || height == 0 || (width & 7u)) {
        return -1;
    }
    for (p = 0; p < 8; ++p) {
        if (!planes[p]) return -1;
    }
    for (y = 0; y < height; ++y) {
        uint8_t *dst = chunky + (size_t)y * chunky_pitch;
        for (x = 0; x < width; ++x) {
            const uint8_t mask = (uint8_t)(0x80u >> (x & 7u));
            uint8_t px = 0;
            for (p = 0; p < 8; ++p) {
                if (planes[p][(size_t)y * plane_pitch + (x >> 3)] & mask) {
                    px |= (uint8_t)(1u << p);
                }
            }
            dst[x] = px;
        }
    }
    return 0;
}

/* Eight pixels make an 8x8 bit matrix (pixel i in row i, bit 7 - p its plane
 * p bit); its transpose has plane p's byte in row 7 - p, pixel 0 in bit 7 as
 * the planes want. The transpose is Hacker's Delight's transpose8 on two
 * 32-bit halves, so a 68k does it in data registers: about 30 instructions
 * for eight pixels where the reference takes 64 tests. */
int acgame_c2p8_fast(const uint8_t *chunky, size_t chunky_pitch,
                     uint8_t *planes[8], size_t plane_pitch,
                     unsigned width, unsigned height) {
    unsigned y, x, p;
    if (!chunky || !planes || width == 0 || height == 0 || (width & 7u)) {
        return -1;
    }
    for (p = 0; p < 8; ++p) {
        if (!planes[p]) return -1;
    }
    for (y = 0; y < height; ++y) {
        const uint8_t *src = chunky + (size_t)y * chunky_pitch;
        const size_t row = (size_t)y * plane_pitch;
        for (x = 0; x < width; x += 8) {
            uint32_t a = ((uint32_t)src[x] << 24) | ((uint32_t)src[x + 1] << 16) |
                         ((uint32_t)src[x + 2] << 8) | (uint32_t)src[x + 3];
            uint32_t b = ((uint32_t)src[x + 4] << 24) | ((uint32_t)src[x + 5] << 16) |
                         ((uint32_t)src[x + 6] << 8) | (uint32_t)src[x + 7];
            uint32_t t;
            const size_t at = row + (x >> 3);

            t = (a ^ (a >> 7)) & 0x00AA00AAu;  a = a ^ t ^ (t << 7);
            t = (b ^ (b >> 7)) & 0x00AA00AAu;  b = b ^ t ^ (t << 7);
            t = (a ^ (a >> 14)) & 0x0000CCCCu; a = a ^ t ^ (t << 14);
            t = (b ^ (b >> 14)) & 0x0000CCCCu; b = b ^ t ^ (t << 14);
            t = (a & 0xF0F0F0F0u) | ((b >> 4) & 0x0F0F0F0Fu);
            b = ((a << 4) & 0xF0F0F0F0u) | (b & 0x0F0F0F0Fu);
            a = t;

            planes[7][at] = (uint8_t)(a >> 24);
            planes[6][at] = (uint8_t)(a >> 16);
            planes[5][at] = (uint8_t)(a >> 8);
            planes[4][at] = (uint8_t)a;
            planes[3][at] = (uint8_t)(b >> 24);
            planes[2][at] = (uint8_t)(b >> 16);
            planes[1][at] = (uint8_t)(b >> 8);
            planes[0][at] = (uint8_t)b;
        }
    }
    return 0;
}
