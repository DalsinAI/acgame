/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 * Inline calls for bebbo's m68k-amigaos-gcc, from library/acgame_lib.sfd. */
#ifndef _INLINE_ACGAME_H
#define _INLINE_ACGAME_H
#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif
#ifndef ACGAME_BASE_NAME
#define ACGAME_BASE_NAME ACGameBase
#endif

#define ACG_C2P8Ref(chunky, chunkyPitch, planes, planePitch, width, height) \
    LP6(0x1e, LONG, ACG_C2P8Ref, const UBYTE *, chunky, a0, ULONG, chunkyPitch, d0, UBYTE **, planes, a1, ULONG, planePitch, d1, ULONG, width, d2, ULONG, height, d3, , ACGAME_BASE_NAME)
#define ACG_C2P8Fast(chunky, chunkyPitch, planes, planePitch, width, height) \
    LP6(0x24, LONG, ACG_C2P8Fast, const UBYTE *, chunky, a0, ULONG, chunkyPitch, d0, UBYTE **, planes, a1, ULONG, planePitch, d1, ULONG, width, d2, ULONG, height, d3, , ACGAME_BASE_NAME)
#define ACG_P2C8Ref(chunky, chunkyPitch, planes, planePitch, width, height) \
    LP6(0x2a, LONG, ACG_P2C8Ref, UBYTE *, chunky, a0, ULONG, chunkyPitch, d0, const UBYTE **, planes, a1, ULONG, planePitch, d1, ULONG, width, d2, ULONG, height, d3, , ACGAME_BASE_NAME)
#define ACG_HAM8EncodeRow(src, width, basePalette, codes, decoded) \
    LP5(0x30, LONG, ACG_HAM8EncodeRow, const acgame_rgb8 *, src, a0, ULONG, width, d0, const acgame_rgb8 *, basePalette, a1, UBYTE *, codes, a2, acgame_rgb8 *, decoded, a3, , ACGAME_BASE_NAME)
#define ACG_HAM8DecodeRow(codes, width, basePalette, decoded) \
    LP4(0x36, LONG, ACG_HAM8DecodeRow, const UBYTE *, codes, a0, ULONG, width, d0, const acgame_rgb8 *, basePalette, a1, acgame_rgb8 *, decoded, a2, , ACGAME_BASE_NAME)
#define ACG_PlatformInit(config) \
    LP1(0x3c, LONG, ACG_PlatformInit, const acgame_video_config *, config, a0, , ACGAME_BASE_NAME)
#define ACG_PlatformShutdown() \
    LP0NR(0x42, ACG_PlatformShutdown, , ACGAME_BASE_NAME)
#define ACG_BeginIndexedFrame() \
    LP0(0x48, acgame_indexed_surface *, ACG_BeginIndexedFrame, , ACGAME_BASE_NAME)
#define ACG_PresentIndexed(frame) \
    LP1(0x4e, LONG, ACG_PresentIndexed, const acgame_indexed_surface *, frame, a0, , ACGAME_BASE_NAME)
#define ACG_SetPalette(palette) \
    LP1(0x54, LONG, ACG_SetPalette, const acgame_rgb8 *, palette, a0, , ACGAME_BASE_NAME)
#define ACG_DisplayIsPlanar() \
    LP0(0x5a, LONG, ACG_DisplayIsPlanar, , ACGAME_BASE_NAME)
#define ACG_GetInfo(info) \
    LP1(0x60, LONG, ACG_GetInfo, acgame_platform_info *, info, a0, , ACGAME_BASE_NAME)
#define ACG_PresentHAM8(hamCodes, pitch, basePalette) \
    LP3(0x66, LONG, ACG_PresentHAM8, const UBYTE *, hamCodes, a0, ULONG, pitch, d0, const acgame_rgb8 *, basePalette, a1, , ACGAME_BASE_NAME)
#define ACG_Poll(state) \
    LP1NR(0x6c, ACG_Poll, acgame_input_state *, state, a0, , ACGAME_BASE_NAME)
#define ACG_RawKeys() \
    LP0(0x72, const UBYTE *, ACG_RawKeys, , ACGAME_BASE_NAME)
#define ACG_Pointer(x, y) \
    LP2(0x78, LONG, ACG_Pointer, LONG *, x, a0, LONG *, y, a1, , ACGAME_BASE_NAME)
#define ACG_ShowPointer(show) \
    LP1(0x7e, LONG, ACG_ShowPointer, LONG, show, d0, , ACGAME_BASE_NAME)
#define ACG_TicksMS() \
    LP0(0x84, ULONG, ACG_TicksMS, , ACGAME_BASE_NAME)
#define ACG_AudioS16Stereo(samples, frames, sampleRate) \
    LP3(0x8a, LONG, ACG_AudioS16Stereo, const WORD *, samples, a0, ULONG, frames, d0, ULONG, sampleRate, d1, , ACGAME_BASE_NAME)

#endif /* _INLINE_ACGAME_H */
