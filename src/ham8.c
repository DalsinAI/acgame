#include "acgame/acgame.h"

static unsigned err2(acgame_rgb8 a, acgame_rgb8 b) {
    int dr = (int)a.r - (int)b.r;
    int dg = (int)a.g - (int)b.g;
    int db = (int)a.b - (int)b.b;
    return (unsigned)(dr * dr + dg * dg + db * db);
}

static uint8_t q6(uint8_t v) {
    return (uint8_t)(v >> 2);
}

static acgame_rgb8 apply_code(uint8_t code, acgame_rgb8 prev,
                              const acgame_rgb8 pal[64]) {
    const uint8_t ctl = (uint8_t)(code >> 6);
    const uint8_t data = (uint8_t)(code & 0x3f);
    acgame_rgb8 out = prev;
    switch (ctl) {
    case 0:
        out = pal[data];
        break;
    case 1:
        out.b = (uint8_t)((data << 2) | (prev.b & 3u));
        break;
    case 2:
        out.r = (uint8_t)((data << 2) | (prev.r & 3u));
        break;
    default:
        out.g = (uint8_t)((data << 2) | (prev.g & 3u));
        break;
    }
    return out;
}

int acgame_ham8_decode_row(const uint8_t *codes, unsigned width,
                           const acgame_rgb8 base_palette[64],
                           acgame_rgb8 *decoded) {
    unsigned x;
    acgame_rgb8 prev;
    if (!codes || !base_palette || !decoded || width == 0) return -1;
    prev = base_palette[0];
    for (x = 0; x < width; ++x) {
        prev = apply_code(codes[x], prev, base_palette);
        decoded[x] = prev;
    }
    return 0;
}

int acgame_ham8_encode_row(const acgame_rgb8 *src, unsigned width,
                           const acgame_rgb8 base_palette[64],
                           uint8_t *codes, acgame_rgb8 *decoded) {
    unsigned x, i;
    acgame_rgb8 prev;
    if (!src || !base_palette || !codes || width == 0) return -1;
    prev = base_palette[0];

    for (x = 0; x < width; ++x) {
        uint8_t best_code = 0;
        acgame_rgb8 best_rgb = base_palette[0];
        unsigned best_err = ~0u;

        for (i = 0; i < 64; ++i) {
            const unsigned e = err2(src[x], base_palette[i]);
            if (e < best_err) {
                best_err = e;
                best_code = (uint8_t)i;
                best_rgb = base_palette[i];
            }
        }

        if (x != 0) {
            const uint8_t candidates[3] = {
                (uint8_t)(0x40u | q6(src[x].b)),
                (uint8_t)(0x80u | q6(src[x].r)),
                (uint8_t)(0xc0u | q6(src[x].g))
            };
            for (i = 0; i < 3; ++i) {
                const acgame_rgb8 c = apply_code(candidates[i], prev, base_palette);
                const unsigned e = err2(src[x], c);
                if (e < best_err) {
                    best_err = e;
                    best_code = candidates[i];
                    best_rgb = c;
                }
            }
        }
        codes[x] = best_code;
        prev = best_rgb;
        if (decoded) decoded[x] = best_rgb;
    }
    return 0;
}
