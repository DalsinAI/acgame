#ifndef ACGAME_H
#define ACGAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} acgame_rgb8;

typedef struct {
    uint16_t width;
    uint16_t height;
    size_t pitch;
    uint8_t *pixels;
    acgame_rgb8 palette[256];
} acgame_indexed_surface;

int acgame_c2p8_ref(const uint8_t *chunky, size_t chunky_pitch,
                    uint8_t *planes[8], size_t plane_pitch,
                    unsigned width, unsigned height);

/* The same result as acgame_c2p8_ref, eight pixels at a time through a bit
 * matrix transpose in 32-bit registers: the converter the backend uses on a
 * planar screen. */
int acgame_c2p8_fast(const uint8_t *chunky, size_t chunky_pitch,
                     uint8_t *planes[8], size_t plane_pitch,
                     unsigned width, unsigned height);

int acgame_p2c8_ref(uint8_t *chunky, size_t chunky_pitch,
                    const uint8_t *planes[8], size_t plane_pitch,
                    unsigned width, unsigned height);

int acgame_ham8_encode_row(const acgame_rgb8 *src, unsigned width,
                           const acgame_rgb8 base_palette[64],
                           uint8_t *codes, acgame_rgb8 *decoded);

int acgame_ham8_decode_row(const uint8_t *codes, unsigned width,
                           const acgame_rgb8 base_palette[64],
                           acgame_rgb8 *decoded);

#ifdef __cplusplus
}
#endif

#endif
