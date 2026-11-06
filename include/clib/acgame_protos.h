/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT */
#ifndef CLIB_ACGAME_PROTOS_H
#define CLIB_ACGAME_PROTOS_H
#include <exec/types.h>
#include <libraries/acgame.h>

LONG ACG_C2P8Ref(const UBYTE * chunky, ULONG chunkyPitch, UBYTE ** planes, ULONG planePitch, ULONG width, ULONG height);
LONG ACG_C2P8Fast(const UBYTE * chunky, ULONG chunkyPitch, UBYTE ** planes, ULONG planePitch, ULONG width, ULONG height);
LONG ACG_P2C8Ref(UBYTE * chunky, ULONG chunkyPitch, const UBYTE ** planes, ULONG planePitch, ULONG width, ULONG height);
LONG ACG_HAM8EncodeRow(const acgame_rgb8 * src, ULONG width, const acgame_rgb8 * basePalette, UBYTE * codes, acgame_rgb8 * decoded);
LONG ACG_HAM8DecodeRow(const UBYTE * codes, ULONG width, const acgame_rgb8 * basePalette, acgame_rgb8 * decoded);
LONG ACG_PlatformInit(const acgame_video_config * config);
VOID ACG_PlatformShutdown(VOID);
acgame_indexed_surface * ACG_BeginIndexedFrame(VOID);
LONG ACG_PresentIndexed(const acgame_indexed_surface * frame);
LONG ACG_SetPalette(const acgame_rgb8 * palette);
LONG ACG_DisplayIsPlanar(VOID);
LONG ACG_GetInfo(acgame_platform_info * info);
LONG ACG_PresentHAM8(const UBYTE * hamCodes, ULONG pitch, const acgame_rgb8 * basePalette);
VOID ACG_Poll(acgame_input_state * state);
const UBYTE * ACG_RawKeys(VOID);
LONG ACG_Pointer(LONG * x, LONG * y);
LONG ACG_ShowPointer(LONG show);
ULONG ACG_TicksMS(VOID);
LONG ACG_AudioS16Stereo(const WORD * samples, ULONG frames, ULONG sampleRate);

#endif
