#include "acgame/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* acgame-joy [AGA|RTG|AUTO] [FRAMES]: shows the first pad as ACGame reads it
 * (GFOpenInput when it is there, lowlevel.library's port 2 otherwise): a lamp
 * for each standard button, the two sticks and the two triggers. The top
 * strip says where the pad came from: green GFOpenInput, yellow lowlevel.library,
 * red no pad. Changes are printed too. Escape quits. */

#define W 320
#define H 200

enum { C_BACK, C_PANEL, C_OFF, C_ON, C_STICK, C_DOT, C_OI, C_LL, C_NONE, C_KEY };

static const acgame_rgb8 colours[] = {
    {16, 16, 32}, {48, 48, 72}, {72, 72, 96}, {255, 220, 64},
    {24, 24, 40}, {255, 255, 255}, {32, 200, 64}, {230, 200, 32}, {200, 40, 40}, {64, 160, 255}
};

static void box(acgame_indexed_surface *f, int x, int y, int w, int h, uint8_t c) {
    int i;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)f->width) w = (int)f->width - x;
    if (y + h > (int)f->height) h = (int)f->height - y;
    if (w <= 0 || h <= 0) return;
    for (i = 0; i < h; ++i) {
        memset(f->pixels + (size_t)(y + i) * f->pitch + (size_t)x, c, (size_t)w);
    }
}

/* A stick: a 64x64 field with a dot at (x, y), -32768..32767 each. */
static void stick(acgame_indexed_surface *f, int left, int top, int16_t x, int16_t y) {
    const int cx = left + 30 + ((int)x * 30) / 32768;
    const int cy = top + 30 + ((int)y * 30) / 32768;
    box(f, left, top, 64, 64, C_STICK);
    box(f, left + 31, top, 2, 64, C_PANEL);
    box(f, left, top + 31, 64, 2, C_PANEL);
    box(f, cx - 2, cy - 2, 8, 8, C_DOT);
}

/* A trigger: a 12x64 bar filled from the bottom, 0..32767. */
static void trigger(acgame_indexed_surface *f, int left, int top, int16_t v) {
    const int fill = v > 0 ? ((int)v * 64) / 32768 : 0;
    box(f, left, top, 12, 64, C_STICK);
    box(f, left, top + 64 - fill, 12, fill, C_ON);
}

static const char *source_name(unsigned s) {
    return s == ACGAME_PAD_OPENINPUT ? "GFOpenInput" : s == ACGAME_PAD_LOWLEVEL ? "lowlevel.library" : "none";
}

int main(int argc, char **argv) {
    acgame_video_config config = {W, H, 50, ACGAME_VIDEO_INDEXED8, ACGAME_DISPLAY_AUTO};
    acgame_input_state in;
    acgame_indexed_surface *f;
    acgame_platform_info info;
    unsigned long frames = 0, limit = 0, presses = 0;
    uint32_t last_buttons = 0, seen = 0;
    unsigned last_source = 99, last_connected = 99;
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
    f = acgame_platform_begin_indexed_frame();
    if (!f || acgame_platform_get_info(&info) != 0) {
        acgame_platform_shutdown();
        return 21;
    }
    printf("ACGame joystick: %s screen; gfopeninput.library %s\n", info.planar ? "AGA planar" : "RTG chunky",
           info.openinput ? "in use" : "not in use (lowlevel.library only)");
    if (info.openinput) {
        printf("ACGame joystick: gfopeninput.library %d.%d\n", info.openinput >> 16, info.openinput & 0xffff);
    }
    memset(f->palette, 0, sizeof(f->palette));
    memcpy(f->palette, colours, sizeof(colours));
    memset(&in, 0, sizeof(in));

    while (!in.quit_requested && (!limit || frames < limit)) {
        const acgame_pad_state *p = &in.pad;
        const uint8_t strip = !p->connected ? C_NONE : p->source == ACGAME_PAD_OPENINPUT ? C_OI : C_LL;
        box(f, 0, 0, W, H, C_BACK);
        box(f, 0, 0, W, 10, strip);
        for (i = 0; i < ACGAME_PAD_BUTTON_COUNT; ++i) {
            const int x = 8 + (i % 11) * 28, y = 20 + (i / 11) * 22;
            box(f, x, y, 24, 18, (p->buttons & ACGAME_PAD_BIT(i)) ? C_ON : C_OFF);
        }
        for (i = 0; i < ACGAME_KEY_COUNT; ++i) {
            box(f, 8 + i * 14, 70, 10, 10, in.key[i] ? C_KEY : C_PANEL);
        }
        stick(f, 40, 100, p->axes[ACGAME_PAD_LEFTX], p->axes[ACGAME_PAD_LEFTY]);
        stick(f, 200, 100, p->axes[ACGAME_PAD_RIGHTX], p->axes[ACGAME_PAD_RIGHTY]);
        trigger(f, 130, 100, p->axes[ACGAME_PAD_TRIGGERLEFT]);
        trigger(f, 170, 100, p->axes[ACGAME_PAD_TRIGGERRIGHT]);
        if (acgame_platform_present_indexed(f) != 0) {
            acgame_platform_shutdown();
            return 22;
        }
        ++frames;
        acgame_platform_poll(&in);

        if (p->connected != last_connected || p->source != last_source) {
            printf("ACGame joystick: frame %lu: %s%s%s%s\n", frames,
                   p->connected ? "pad from " : "no pad", p->connected ? source_name(p->source) : "",
                   p->connected && p->name ? ": " : "", p->connected && p->name ? p->name : "");
            last_connected = p->connected;
            last_source = p->source;
        }
        if (p->buttons != last_buttons) {
            presses += 1;
            seen |= p->buttons;
            printf("ACGame joystick: frame %lu: buttons %08lx  left %d,%d  right %d,%d  triggers %d,%d\n",
                   frames, (unsigned long)p->buttons,
                   p->axes[ACGAME_PAD_LEFTX], p->axes[ACGAME_PAD_LEFTY],
                   p->axes[ACGAME_PAD_RIGHTX], p->axes[ACGAME_PAD_RIGHTY],
                   p->axes[ACGAME_PAD_TRIGGERLEFT], p->axes[ACGAME_PAD_TRIGGERRIGHT]);
            last_buttons = p->buttons;
        }
    }
    acgame_platform_shutdown();
    printf("ACGame joystick: %lu frames, %lu button changes, buttons seen %08lx%s\n",
           frames, presses, (unsigned long)seen, in.quit_requested ? ", stopped by Escape" : "");
    return 0;
}
