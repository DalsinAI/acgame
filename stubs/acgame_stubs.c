/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * libacgame.a for acgame.library: acgame.h's and platform.h's calls, each
 * going to acgame.library. A game links this in place of the static
 * libacgame.a and changes nothing else.
 *
 * The library is opened at the first call: the copy in the game's own
 * Libs/ drawer first (PROGDIR:Libs/acgame.library, the one the game was
 * built against and ships with), then the system's. Either must be at least
 * the version these stubs were built with. It is closed as the game exits,
 * and closing it also shuts down a screen the game left open.
 */
#include <exec/types.h>
#include <proto/exec.h>

#include <libraries/acgame.h>
#include <inline/acgame.h>

struct Library *ACGameBase = NULL;

static int acg_open(void)
{
    if (!ACGameBase) {
        ACGameBase = OpenLibrary((CONST_STRPTR)"PROGDIR:Libs/" ACGAME_LIB_NAME, ACGAME_LIB_VERSION);
        if (!ACGameBase)
            ACGameBase = OpenLibrary((CONST_STRPTR)ACGAME_LIB_NAME, ACGAME_LIB_VERSION);
    }
    return ACGameBase != NULL;
}

__attribute__((destructor)) static void acg_close(void)
{
    if (ACGameBase) {
        CloseLibrary(ACGameBase);
        ACGameBase = NULL;
    }
}

int acgame_c2p8_ref(const uint8_t *chunky, size_t chunky_pitch,
                    uint8_t *planes[8], size_t plane_pitch,
                    unsigned width, unsigned height)
{
    return acg_open() ? ACG_C2P8Ref(chunky, chunky_pitch, planes, plane_pitch, width, height) : -1;
}

int acgame_c2p8_fast(const uint8_t *chunky, size_t chunky_pitch,
                     uint8_t *planes[8], size_t plane_pitch,
                     unsigned width, unsigned height)
{
    return acg_open() ? ACG_C2P8Fast(chunky, chunky_pitch, planes, plane_pitch, width, height) : -1;
}

int acgame_p2c8_ref(uint8_t *chunky, size_t chunky_pitch,
                    const uint8_t *planes[8], size_t plane_pitch,
                    unsigned width, unsigned height)
{
    return acg_open() ? ACG_P2C8Ref(chunky, chunky_pitch, planes, plane_pitch, width, height) : -1;
}

int acgame_ham8_encode_row(const acgame_rgb8 *src, unsigned width,
                           const acgame_rgb8 base_palette[64],
                           uint8_t *codes, acgame_rgb8 *decoded)
{
    return acg_open() ? ACG_HAM8EncodeRow(src, width, base_palette, codes, decoded) : -1;
}

int acgame_ham8_decode_row(const uint8_t *codes, unsigned width,
                           const acgame_rgb8 base_palette[64],
                           acgame_rgb8 *decoded)
{
    return acg_open() ? ACG_HAM8DecodeRow(codes, width, base_palette, decoded) : -1;
}

int acgame_platform_init(const acgame_video_config *config)
{
    return acg_open() ? ACG_PlatformInit(config) : -1;
}

void acgame_platform_shutdown(void)
{
    if (ACGameBase)
        ACG_PlatformShutdown();
}

acgame_indexed_surface *acgame_platform_begin_indexed_frame(void)
{
    return ACGameBase ? ACG_BeginIndexedFrame() : NULL;
}

int acgame_platform_present_indexed(const acgame_indexed_surface *frame)
{
    return ACGameBase ? ACG_PresentIndexed(frame) : -1;
}

int acgame_platform_set_palette(const acgame_rgb8 palette[256])
{
    return ACGameBase ? ACG_SetPalette(palette) : -1;
}

int acgame_platform_display_is_planar(void)
{
    return ACGameBase ? ACG_DisplayIsPlanar() : -1;
}

int acgame_platform_get_info(acgame_platform_info *info)
{
    return ACGameBase ? ACG_GetInfo(info) : -1;
}

int acgame_platform_present_ham8(const uint8_t *ham_codes, size_t pitch,
                                 const acgame_rgb8 base_palette[64])
{
    return ACGameBase ? ACG_PresentHAM8(ham_codes, pitch, base_palette) : -1;
}

void acgame_platform_poll(acgame_input_state *state)
{
    if (acg_open()) {
        ACG_Poll(state);
    } else if (state) {
        unsigned i;
        for (i = 0; i < sizeof(*state); i++)
            ((unsigned char *)state)[i] = 0;
    }
}

const uint8_t *acgame_platform_rawkeys(void)
{
    static const uint8_t none[ACGAME_RAWKEY_COUNT];
    return acg_open() ? ACG_RawKeys() : none;
}

int acgame_platform_pointer(int *x, int *y)
{
    return ACGameBase ? ACG_Pointer((LONG *)x, (LONG *)y) : -1;
}

int acgame_platform_show_pointer(int show)
{
    return ACGameBase ? ACG_ShowPointer(show) : -1;
}

uint32_t acgame_platform_ticks_ms(void)
{
    return acg_open() ? ACG_TicksMS() : 0;
}

int acgame_platform_audio_s16stereo(const int16_t *samples, unsigned frames,
                                    unsigned sample_rate)
{
    return ACGameBase ? ACG_AudioS16Stereo(samples, frames, sample_rate) : -1;
}
