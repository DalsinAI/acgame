/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * acgame.library 1.0: ACGame's game layer (c2p, HAM8, the AGA and RTG
 * screens, keyboard, mouse and pads) as a shared library for AmigaOS 3.x.
 * The calls are acgame.h's and platform.h's, in library/acgame_lib.sfd's
 * order; the backend is src/platform_aros_aga.c, built with ACGAME_LIBRARY.
 *
 * Every OpenLibrary gets a library base of its own, holding that game's
 * state, so two games can run at once, each with its own screen. Each entry
 * point puts the opener's state in a4 for the call (acgame_state.h). The
 * library opened by name (the one in the system's list) is the master: it
 * only hands out the openers' bases, and it goes away once the last of them
 * is closed and memory is wanted.
 *
 * Built bare by library/build-amiga.sh (no startup code, no C library). This
 * file goes first: its start() and RomTag lead the hunk.
 */
#include "acgame_state.h"                   /* a4, before any function */

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <intuition/intuitionbase.h>
#include "../include/oa_protos_begin.h"   /* the protos' unused varargs wrappers dropped */
#include <proto/exec.h>
#include "../include/oa_protos_end.h"

#include <libraries/acgame.h>

#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */

/* The system's libraries, opened once by the library for every game. */
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Device *TimerBase;

int start(void) { return -1; }      /* run as a program: nothing (stays first in the file) */

static const char lib_name[] = ACGAME_LIB_NAME;
static const char lib_id[] = "acgame.library 1.0 (6.11.2026) ACGame, AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) = "$VER: acgame.library 1.0 (6.11.2026)";   /* for Version on the file */

struct ACGBase {
    struct Library lib;
    BPTR seglist;                   /* the master's */
    struct ACGBase *master;         /* an opener's base: the master; the master: NULL */
    struct acgame_state *state;     /* an opener's base: its game's state */
    struct timerequest timer;       /* the master's: TimerBase, for ReadEClock */
};

static struct Library *lib_init(REG(d0, struct ACGBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct ACGBase *base));
static BPTR lib_close(REG(a6, struct ACGBase *base));
static BPTR lib_expunge(REG(a6, struct ACGBase *base));
static ULONG lib_null(void);

static LONG L_C2P8Ref(REG(a0, const UBYTE *chunky), REG(d0, ULONG chunky_pitch), REG(a1, UBYTE **planes),
                      REG(d1, ULONG plane_pitch), REG(d2, ULONG width), REG(d3, ULONG height));
static LONG L_C2P8Fast(REG(a0, const UBYTE *chunky), REG(d0, ULONG chunky_pitch), REG(a1, UBYTE **planes),
                       REG(d1, ULONG plane_pitch), REG(d2, ULONG width), REG(d3, ULONG height));
static LONG L_P2C8Ref(REG(a0, UBYTE *chunky), REG(d0, ULONG chunky_pitch), REG(a1, const UBYTE **planes),
                      REG(d1, ULONG plane_pitch), REG(d2, ULONG width), REG(d3, ULONG height));
static LONG L_HAM8EncodeRow(REG(a0, const acgame_rgb8 *src), REG(d0, ULONG width), REG(a1, const acgame_rgb8 *base_palette),
                            REG(a2, UBYTE *codes), REG(a3, acgame_rgb8 *decoded));
static LONG L_HAM8DecodeRow(REG(a0, const UBYTE *codes), REG(d0, ULONG width), REG(a1, const acgame_rgb8 *base_palette),
                            REG(a2, acgame_rgb8 *decoded));
static LONG L_PlatformInit(REG(a0, const acgame_video_config *config), REG(a6, struct ACGBase *base));
static VOID L_PlatformShutdown(REG(a6, struct ACGBase *base));
static acgame_indexed_surface *L_BeginIndexedFrame(REG(a6, struct ACGBase *base));
static LONG L_PresentIndexed(REG(a0, const acgame_indexed_surface *frame), REG(a6, struct ACGBase *base));
static LONG L_SetPalette(REG(a0, const acgame_rgb8 *palette), REG(a6, struct ACGBase *base));
static LONG L_DisplayIsPlanar(REG(a6, struct ACGBase *base));
static LONG L_GetInfo(REG(a0, acgame_platform_info *info), REG(a6, struct ACGBase *base));
static LONG L_PresentHAM8(REG(a0, const UBYTE *ham_codes), REG(d0, ULONG pitch), REG(a1, const acgame_rgb8 *base_palette),
                          REG(a6, struct ACGBase *base));
static VOID L_Poll(REG(a0, acgame_input_state *state), REG(a6, struct ACGBase *base));
static const UBYTE *L_RawKeys(REG(a6, struct ACGBase *base));
static LONG L_Pointer(REG(a0, LONG *x), REG(a1, LONG *y), REG(a6, struct ACGBase *base));
static LONG L_ShowPointer(REG(d0, LONG show), REG(a6, struct ACGBase *base));
static ULONG L_TicksMS(void);
static LONG L_AudioS16Stereo(REG(a0, const WORD *samples), REG(d0, ULONG frames), REG(d1, ULONG sample_rate),
                             REG(a6, struct ACGBase *base));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)L_C2P8Ref, (APTR)L_C2P8Fast, (APTR)L_P2C8Ref, (APTR)L_HAM8EncodeRow, (APTR)L_HAM8DecodeRow,
    (APTR)L_PlatformInit, (APTR)L_PlatformShutdown, (APTR)L_BeginIndexedFrame, (APTR)L_PresentIndexed,
    (APTR)L_SetPalette, (APTR)L_DisplayIsPlanar, (APTR)L_GetInfo, (APTR)L_PresentHAM8, (APTR)L_Poll,
    (APTR)L_RawKeys, (APTR)L_Pointer, (APTR)L_ShowPointer, (APTR)L_TicksMS, (APTR)L_AudioS16Stereo,
    (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct ACGBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, ACGAME_LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

/* ---- no C library: GCC may call these itself --------------------------- */

void *memcpy(void *to, const void *from, unsigned long n)
{
    UBYTE *d = to; const UBYTE *s = from;
    if (!(((ULONG)d | (ULONG)s) & 3)) {
        ULONG *dl = (ULONG *)d; const ULONG *sl = (const ULONG *)s;
        for (; n >= 4; n -= 4) *dl++ = *sl++;
        d = (UBYTE *)dl; s = (const UBYTE *)sl;
    }
    while (n--) *d++ = *s++;
    return to;
}
void *memset(void *p, int c, unsigned long n)
{
    UBYTE *d = p;
    if (!((ULONG)d & 3)) {
        ULONG v = (UBYTE)c, *dl = (ULONG *)d;
        v |= v << 8; v |= v << 16;
        for (; n >= 4; n -= 4) *dl++ = v;
        d = (UBYTE *)dl;
    }
    while (n--) *d++ = (UBYTE)c;
    return p;
}
int memcmp(const void *a, const void *b, unsigned long n)
{
    const UBYTE *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}

/* ---- the library -------------------------------------------------------- */

static VOID free_base(struct ACGBase *base)
{
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
}

static VOID close_system(struct ACGBase *base)
{
    if (TimerBase) { CloseDevice((struct IORequest *)&base->timer); TimerBase = NULL; }
    if (IntuitionBase) { CloseLibrary((struct Library *)IntuitionBase); IntuitionBase = NULL; }
    if (GfxBase) { CloseLibrary((struct Library *)GfxBase); GfxBase = NULL; }
    if (DOSBase) { CloseLibrary((struct Library *)DOSBase); DOSBase = NULL; }
}

static struct Library *lib_init(REG(d0, struct ACGBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->master = NULL;
    base->state = NULL;
    base->lib.lib_Revision = ACGAME_LIB_REVISION;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&base->timer, 0) == 0)
        TimerBase = base->timer.tr_node.io_Device;
    if (!DOSBase || !GfxBase || !IntuitionBase || !TimerBase) {
        close_system(base);
        free_base(base);
        return NULL;
    }
    return &base->lib;
}

/* The master hands each opener a base of its own (its own jump table and
 * its own state); exec never sees those in its list. */
static struct Library *lib_open(REG(a6, struct ACGBase *base))
{
    struct ACGBase *opener;
    if (base->master) return NULL;          /* only the master is opened by name */
    opener = (struct ACGBase *)MakeLibrary((CONST_APTR)lib_vectors, NULL, NULL, sizeof(struct ACGBase), 0);
    if (!opener) return NULL;
    opener->state = AllocVec(acgame_state_size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!opener->state) { free_base(opener); return NULL; }
    opener->lib.lib_Node.ln_Type = NT_LIBRARY;
    opener->lib.lib_Node.ln_Name = (char *)lib_name;
    opener->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    opener->lib.lib_Version = ACGAME_LIB_VERSION;
    opener->lib.lib_Revision = ACGAME_LIB_REVISION;
    opener->lib.lib_IdString = (APTR)lib_id;
    opener->lib.lib_OpenCnt = 1;
    opener->seglist = 0;
    opener->master = base;
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &opener->lib;
}

static BPTR lib_close(REG(a6, struct ACGBase *base))
{
    struct ACGBase *master = base->master;
    if (!master) return 0;                  /* the master is never handed out */
    {
        struct acgame_state *const caller = acg_state;
        acg_state = base->state;
        acgame_state_cleanup();             /* a game that did not shut down */
        acg_state = caller;
    }
    FreeVec(base->state);
    free_base(base);
    master->lib.lib_OpenCnt--;
    if (master->lib.lib_OpenCnt == 0 && (master->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(master);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct ACGBase *base))
{
    BPTR seglist;
    if (base->master) return 0;
    if (base->lib.lib_OpenCnt) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    close_system(base);
    free_base(base);
    return seglist;
}

static ULONG lib_null(void) { return 0; }

/* ---- the calls ------------------------------------------------------------ */

/* The opener's state in a4 for the call, and the caller's a4 back after. */
#define ENTER(b) struct acgame_state *const acg_caller = acg_state; acg_state = (b)->state
#define LEAVE()  (acg_state = acg_caller)

static LONG L_C2P8Ref(REG(a0, const UBYTE *chunky), REG(d0, ULONG chunky_pitch), REG(a1, UBYTE **planes),
                      REG(d1, ULONG plane_pitch), REG(d2, ULONG width), REG(d3, ULONG height))
{
    return acgame_c2p8_ref(chunky, chunky_pitch, planes, plane_pitch, width, height);
}
static LONG L_C2P8Fast(REG(a0, const UBYTE *chunky), REG(d0, ULONG chunky_pitch), REG(a1, UBYTE **planes),
                       REG(d1, ULONG plane_pitch), REG(d2, ULONG width), REG(d3, ULONG height))
{
    return acgame_c2p8_fast(chunky, chunky_pitch, planes, plane_pitch, width, height);
}
static LONG L_P2C8Ref(REG(a0, UBYTE *chunky), REG(d0, ULONG chunky_pitch), REG(a1, const UBYTE **planes),
                      REG(d1, ULONG plane_pitch), REG(d2, ULONG width), REG(d3, ULONG height))
{
    return acgame_p2c8_ref(chunky, chunky_pitch, planes, plane_pitch, width, height);
}
static LONG L_HAM8EncodeRow(REG(a0, const acgame_rgb8 *src), REG(d0, ULONG width), REG(a1, const acgame_rgb8 *base_palette),
                            REG(a2, UBYTE *codes), REG(a3, acgame_rgb8 *decoded))
{
    return acgame_ham8_encode_row(src, width, base_palette, codes, decoded);
}
static LONG L_HAM8DecodeRow(REG(a0, const UBYTE *codes), REG(d0, ULONG width), REG(a1, const acgame_rgb8 *base_palette),
                            REG(a2, acgame_rgb8 *decoded))
{
    return acgame_ham8_decode_row(codes, width, base_palette, decoded);
}
static LONG L_PlatformInit(REG(a0, const acgame_video_config *config), REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_init(config);
    LEAVE();
    return r;
}
static VOID L_PlatformShutdown(REG(a6, struct ACGBase *base))
{
    ENTER(base);
    acgame_platform_shutdown();
    LEAVE();
}
static acgame_indexed_surface *L_BeginIndexedFrame(REG(a6, struct ACGBase *base))
{
    acgame_indexed_surface *r;
    ENTER(base);
    r = acgame_platform_begin_indexed_frame();
    LEAVE();
    return r;
}
static LONG L_PresentIndexed(REG(a0, const acgame_indexed_surface *frame), REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_present_indexed(frame);
    LEAVE();
    return r;
}
static LONG L_SetPalette(REG(a0, const acgame_rgb8 *palette), REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_set_palette(palette);
    LEAVE();
    return r;
}
static LONG L_DisplayIsPlanar(REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_display_is_planar();
    LEAVE();
    return r;
}
static LONG L_GetInfo(REG(a0, acgame_platform_info *info), REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_get_info(info);
    LEAVE();
    return r;
}
static LONG L_PresentHAM8(REG(a0, const UBYTE *ham_codes), REG(d0, ULONG pitch), REG(a1, const acgame_rgb8 *base_palette),
                          REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_present_ham8(ham_codes, pitch, base_palette);
    LEAVE();
    return r;
}
static VOID L_Poll(REG(a0, acgame_input_state *state), REG(a6, struct ACGBase *base))
{
    ENTER(base);
    acgame_platform_poll(state);
    LEAVE();
}
static const UBYTE *L_RawKeys(REG(a6, struct ACGBase *base))
{
    const UBYTE *r;
    ENTER(base);
    r = acgame_platform_rawkeys();
    LEAVE();
    return r;
}
static LONG L_Pointer(REG(a0, LONG *x), REG(a1, LONG *y), REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_pointer((int *)x, (int *)y);
    LEAVE();
    return r;
}
static LONG L_ShowPointer(REG(d0, LONG show), REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_show_pointer(show);
    LEAVE();
    return r;
}
static ULONG L_TicksMS(void)
{
    return acgame_platform_ticks_ms();
}
static LONG L_AudioS16Stereo(REG(a0, const WORD *samples), REG(d0, ULONG frames), REG(d1, ULONG sample_rate),
                             REG(a6, struct ACGBase *base))
{
    LONG r;
    ENTER(base);
    r = acgame_platform_audio_s16stereo(samples, frames, sample_rate);
    LEAVE();
    return r;
}
