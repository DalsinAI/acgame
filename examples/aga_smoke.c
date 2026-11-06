#include "acgame/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 200

/* The kind of screen asked for when none is given: AUTO for acgame-smoke,
 * AGA for acgame-aga-smoke, RTG for acgame-rtg-smoke (the Makefile sets it). */
#ifndef ACGAME_SMOKE_DISPLAY
#define ACGAME_SMOKE_DISPLAY ACGAME_DISPLAY_AUTO
#endif

static void make_palette(acgame_indexed_surface *frame) {
    unsigned i;
    for (i = 0; i < 256; ++i) {
        frame->palette[i].r = (uint8_t)i;
        frame->palette[i].g = (uint8_t)((i * 5u) & 255u);
        frame->palette[i].b = (uint8_t)(255u - i);
    }
}

static void make_frame(acgame_indexed_surface *frame, unsigned phase) {
    unsigned x, y;
    for (y = 0; y < frame->height; ++y) {
        uint8_t *row = frame->pixels + (size_t)y * frame->pitch;
        for (x = 0; x < frame->width; ++x) {
            unsigned checker = ((x >> 4) ^ (y >> 4)) & 1u;
            row[x] = (uint8_t)((x + y + phase + checker * 48u) & 255u);
        }
    }
}

/* acgame-smoke [AGA|RTG|AUTO] [FRAMES]: animates a 320x200 checker until
 * Escape, or for FRAMES frames, then says how fast it went. */
int main(int argc, char **argv) {
    acgame_video_config config = {W, H, 50, ACGAME_VIDEO_INDEXED8, ACGAME_SMOKE_DISPLAY};
    acgame_input_state input;
    acgame_indexed_surface *frame;
    acgame_platform_info info;
    unsigned long frames = 0, limit = 0;
    uint32_t start, took;
    unsigned phase = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        const char c = argv[i][0];
        if (c == 'A' || c == 'a') {
            config.display = (argv[i][1] == 'G' || argv[i][1] == 'g') ? ACGAME_DISPLAY_AGA : ACGAME_DISPLAY_AUTO;
        } else if (c == 'R' || c == 'r') {
            config.display = ACGAME_DISPLAY_RTG;
        } else if (c >= '0' && c <= '9') {
            limit = strtoul(argv[i], NULL, 10);
        }
    }
    if (acgame_platform_init(&config) != 0) {
        fputs("ACGame: failed to open an 8-bit screen\n", stderr);
        return 20;
    }
    frame = acgame_platform_begin_indexed_frame();
    if (!frame || acgame_platform_get_info(&info) != 0) {
        fputs("ACGame: no indexed frame available\n", stderr);
        acgame_platform_shutdown();
        return 21;
    }

    printf("ACGame: %s screen %ux%u, frame %ux%u%s\n",
           info.planar ? "AGA planar" : "RTG chunky", info.screen_width, info.screen_height,
           (unsigned)frame->width, (unsigned)frame->height,
           info.planar ? " through ACGame's C2P" :
           info.opengfx ? " through GFOpenGfx (WriteChunkyPixels on the cores)" :
                          " through WriteChunkyPixels");
    make_palette(frame);
    memset(&input, 0, sizeof(input));
    start = acgame_platform_ticks_ms();
    while (!input.quit_requested && (!limit || frames < limit)) {
        make_frame(frame, phase++);
        if (acgame_platform_present_indexed(frame) != 0) {
            fputs("ACGame: present failed\n", stderr);
            acgame_platform_shutdown();
            return 22;
        }
        ++frames;
        acgame_platform_poll(&input);
    }
    took = acgame_platform_ticks_ms() - start;

    acgame_platform_shutdown();
    printf("ACGame: %lu frames in %lu ms (%lu.%lu frames a second)\n", frames, (unsigned long)took,
           took ? frames * 1000UL / took : 0UL, took ? (frames * 10000UL / took) % 10UL : 0UL);
    return 0;
}
