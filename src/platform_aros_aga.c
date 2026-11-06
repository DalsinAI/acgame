#ifdef ACGAME_LIBRARY
/* before any header with a function in it: GCC wants a global register
 * variable declared before the first function definition */
#include "../library/acgame_state.h"
#endif
#include "acgame/platform.h"

#include <string.h>

/* One backend for AmigaOS 3.x (GCC 16.2, libnix) and AROS m68k. The screen
 * is opened through Intuition; a planar 8-plane screen (native AGA) takes the
 * frame through ACGame's C2P, any other 8-bit screen (GFOpenRTG, or another RTG
 * system) through graphics.library's WriteChunkyPixels, which GFOpenGfx
 * (gfopengpu.library 0.8 and later) takes to the cores on AmiCradle.
 *
 * Pads: gfopeninput.library 1.2 or later when it is there (hot-plug, analogue
 * sticks and triggers, the pad opened exclusively while the game runs), and
 * lowlevel.library's ReadJoyPort on port 2 when it is missing or is the 1.1
 * skeleton. No library is needed: without both, keyboard and mouse remain. */
#if defined(__AROS__) || defined(__amigaos__) || defined(AMIGA)

/* The library bases have ACGame's own names, so a game that links ACGame and
 * SDL 2 (which has a LowLevelBase of its own) or opens a library itself has
 * no clash over them. */
#define LowLevelBase acgame_LowLevelBase
#define OpenInputBase acgame_OpenInputBase
#define OpenGPUBase acgame_OpenGPUBase
#define OpenRTGBase acgame_OpenRTGBase

#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <libraries/lowlevel.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/lowlevel.h>

#ifdef ACGAME_LIBRARY
#include <devices/timer.h>
#include <proto/timer.h>
#else
#include <stdlib.h>
#include <sys/time.h>
#endif
#include <string.h>

/* GFOpenInput's frozen header (gfopeninput include/, copied unchanged into
 * include/acgame/gfopeninput so ACGame builds with nothing else installed). */
#ifndef ACGAME_OPENINPUT
#define ACGAME_OPENINPUT 1
#endif
#if ACGAME_OPENINPUT
#include "acgame/gfopeninput/libraries/gfopeninput.h"
#ifdef __AROS__
/* AROS m68k calls the same library with its own macros (LVO / 6). */
#include <aros/libcall.h>
#define OIN_ListControllers(buffer, max, infoSize) \
    AROS_LC3(ULONG, OIN_ListControllers, AROS_LCA(struct OIControllerInfo *, (buffer), A0), \
             AROS_LCA(ULONG, (max), D0), AROS_LCA(ULONG, (infoSize), D1), \
             struct Library *, OpenInputBase, 5, OpenInput)
#define OIN_OpenControllerA(id, tags) \
    AROS_LC2(APTR, OIN_OpenControllerA, AROS_LCA(ULONG, (id), D0), \
             AROS_LCA(struct TagItem *, (tags), A0), struct Library *, OpenInputBase, 7, OpenInput)
#define OIN_CloseController(controller) \
    AROS_LC1NR(void, OIN_CloseController, AROS_LCA(APTR, (controller), A0), \
               struct Library *, OpenInputBase, 8, OpenInput)
#define OIN_ReadState(controller, state, stateSize) \
    AROS_LC3(LONG, OIN_ReadState, AROS_LCA(APTR, (controller), A0), \
             AROS_LCA(struct OIState *, (state), A1), AROS_LCA(ULONG, (stateSize), D0), \
             struct Library *, OpenInputBase, 9, OpenInput)
#define OIN_AddNotifyA(port, tags) \
    AROS_LC2(APTR, OIN_AddNotifyA, AROS_LCA(struct MsgPort *, (port), A0), \
             AROS_LCA(struct TagItem *, (tags), A1), struct Library *, OpenInputBase, 12, OpenInput)
#define OIN_RemNotify(notify) \
    AROS_LC1NR(void, OIN_RemNotify, AROS_LCA(APTR, (notify), A0), \
               struct Library *, OpenInputBase, 13, OpenInput)
#else
#define OPENINPUT_BASE_NAME OpenInputBase
#define NO_INLINE_STDARG
#include "acgame/gfopeninput/inline/gfopeninput.h"
#endif
#endif

/* GFOpenGPU and GFOpenRTG (the Open SDK's headers, copied into
 * include/acgame/opensdk): whether GFOpenGfx has the drawing calls, and the
 * OpenGPU present below. AROS builds leave them out. */
#ifndef ACGAME_OPENGPU
#if !defined(__AROS__)
#define ACGAME_OPENGPU 1
#else
#define ACGAME_OPENGPU 0
#endif
#endif
#if ACGAME_OPENGPU
#define OPENGPU_BASE_NAME OpenGPUBase
#define OPENRTG_BASE_NAME OpenRTGBase
#include "acgame/opensdk/gfopengpu/gfopengpu.h"
#include "acgame/opensdk/gfopengpu/gfx.h"
#include "acgame/opensdk/inline/gfopengpu.h"
#include "acgame/opensdk/gfopenrtg/gfopenrtg.h"
#include "acgame/opensdk/inline/gfopenrtg.h"
#endif

#ifdef ACGAME_LIBRARY
#ifdef __AROS__
#error "acgame.library is built for AmigaOS 3.x; AROS links libacgame.a"
#endif
/* acgame.library (library/acgame_lib.c): every opener has its own state,
 * its own screen and its own library bases. The library's entry points put
 * the opener's state in a4 for the call (library/acgame_state.h, included
 * first, above), and the names below reach it through a4. */
#define acgame_LowLevelBase (acg_state->lowlevel)
#define acgame_OpenInputBase (acg_state->openinput)
#define acgame_OpenGPUBase (acg_state->opengpu)
#define acgame_OpenRTGBase (acg_state->openrtg)
#define g_acgame_amiga (acg_state->game)
#define g_gpu (acg_state->gpu)
#define malloc(n) AllocVec((n), MEMF_ANY)
#define free(p) do { if (p) FreeVec(p); } while (0)
#else
/* Opened here, not by the startup code, so a machine without them still
 * runs with keyboard and mouse only. */
struct Library *LowLevelBase = NULL;
#if ACGAME_OPENINPUT
struct Library *OpenInputBase = NULL;
#endif
#if ACGAME_OPENGPU
struct Library *OpenGPUBase = NULL;
struct Library *OpenRTGBase = NULL;
#endif
#endif

#if ACGAME_OPENGPU
/* The OpenGPU present (RTG): the game's 8-bit frame goes to OpenGPU as an
 * INDEX8 surface with its palette, and OpenGPU converts it to the screen's
 * true colour and scales it into the middle of a 32-bit screen of the
 * display's size, with a black border. The 68k does no conversion, and a
 * palette change is one more composite. */
typedef struct {
    int active;
    int cleared;                    /* the border filled black once */
    unsigned screen_w, screen_h;
    unsigned dx, dy, dw, dh, scale; /* where the frame goes, an integer scale */
    ULONG format;                   /* the screen's OGPU_FMT_ */
    ULONG table[256];               /* the palette as ARGB32 */
    ULONG stream[48];
} acgame_gpu;
#ifndef ACGAME_LIBRARY
static acgame_gpu g_gpu;
#endif
#endif

#if ACGAME_OPENINPUT
#define ACGAME_OI_MAX 8
typedef struct {
    struct MsgPort *port;           /* GFOpenInput's ADDED and REMOVED messages */
    APTR notify;
    APTR handle;                    /* the pad in use, opened exclusively when it could be */
    ULONG id;
    ULONG flags;                    /* its OICF_ flags */
    int rescan;                     /* look for a (better) pad at the next poll */
    char name[64];
} acgame_openinput;
#endif

#define ACGAME_SCREEN_BUFFERS 3

typedef struct {
    struct Screen *screen;
    struct Window *window;
    acgame_indexed_surface frame;
    acgame_rgb8 last_palette[256];
    uint8_t *pixels;
    uint8_t keys[ACGAME_KEY_COUNT];
    uint8_t raw[ACGAME_RAWKEY_COUNT];
    uint8_t mouse_buttons;
    /* A release of a key or button pressed since the last poll, kept for
     * the next one (see acgame_platform_poll). */
    struct IntuiMessage *held;
    int16_t mouse_dx;
    int16_t mouse_dy;
    unsigned width;
    unsigned height;
    unsigned left;  /* where the frame sits on a larger board screen */
    unsigned top;
    int planar;
    int opengfx;
    int palette_valid;
    int quit_requested;
    /* Planar screens: three Chip RAM buffers (two when Chip RAM is short)
     * through Intuition's ScreenBuffers. C2P writes one that is neither on
     * display nor still being scanned out, then ChangeScreenBuffer shows it
     * at the next vertical blank, so a frame is never seen half converted.
     * The third buffer lets the next frame start while the display still
     * reads the previous one. */
    struct ScreenBuffer *buffer[ACGAME_SCREEN_BUFFERS];
    int buffers;                    /* how many are in use: 0, 2 or 3 */
    struct MsgPort *safe_port;      /* SafeMessages: a buffer taken off the display may be written */
    struct MsgPort *shown_port;     /* DispMessages: the last change is on the display */
    int shown;                      /* the buffer last put on display */
    int replaced[ACGAME_SCREEN_BUFFERS]; /* the buffer each one took the display from */
    int writable[ACGAME_SCREEN_BUFFERS];
    int safe_pending;               /* SafeMessages not yet back */
    int shown_pending;              /* the last change's DispMessage is not back */
    unsigned screen_width;
    unsigned screen_height;
    UWORD *blank_pointer;           /* Chip RAM: an empty sprite while the pointer is hidden */
#if ACGAME_OPENINPUT
    acgame_openinput oi;
#endif
} acgame_amiga_state;

#ifdef ACGAME_LIBRARY
struct acgame_state {
    acgame_amiga_state game;            /* "amiga" is one of the compiler's own macros */
    struct Library *lowlevel;
    struct Library *openinput;
    struct Library *opengpu;
    struct Library *openrtg;
#if ACGAME_OPENGPU
    acgame_gpu gpu;
#endif
};
const unsigned long acgame_state_size = sizeof(struct acgame_state);

/* The last CloseLibrary of a game that did not call
 * acgame_platform_shutdown: its screen, window and pads go back. */
void acgame_state_cleanup(void) {
    if (g_acgame_amiga.screen || g_acgame_amiga.pixels) {
        acgame_platform_shutdown();
    }
}
#else
static acgame_amiga_state g_acgame_amiga;
#endif

/* ---- pads ------------------------------------------------------------------ */

/* ENV:ACGame/<name> as a string; 0 when it is not set. */
static int env_value(const char *name, char *value, LONG size) {
    return GetVar((STRPTR)name, (STRPTR)value, size, 0) > 0;
}

#if ACGAME_OPENINPUT
static void oi_close_pad(acgame_openinput *oi) {
    if (oi->handle) {
        OIN_CloseController(oi->handle);    /* gives back an exclusive pad's legacy feed */
        oi->handle = NULL;
    }
    oi->id = 0;
    oi->flags = 0;
    oi->name[0] = 0;
}

static void oi_close(acgame_openinput *oi) {
    oi_close_pad(oi);
    if (oi->notify) {
        OIN_RemNotify(oi->notify);          /* takes back its messages still in the port */
        oi->notify = NULL;
    }
    if (oi->port) {
        DeleteMsgPort(oi->port);
        oi->port = NULL;
    }
    if (OpenInputBase) {
        CloseLibrary(OpenInputBase);
        OpenInputBase = NULL;
    }
}

/* gfopeninput.library 1.2 or later; the 1.1 skeleton lists nothing, so it
 * counts as missing. ENV:ACGame/GFOpenInput 0 leaves it alone (lowlevel only). */
static void oi_open(acgame_openinput *oi) {
    char value[8];
    ULONG tags[3];
    struct Library *lib;
    memset(oi, 0, sizeof(*oi));
    if (env_value("ACGame/GFOpenInput", value, sizeof(value)) && value[0] == '0') {
        return;
    }
    lib = OpenLibrary((CONST_STRPTR)OPENINPUT_NAME, OPENINPUT_VERSION);
    if (!lib) {
        return;
    }
    if (lib->lib_Version == OPENINPUT_VERSION && lib->lib_Revision < OPENINPUT_REVISION_CORE) {
        CloseLibrary(lib);
        return;
    }
    OpenInputBase = lib;
    oi->port = CreateMsgPort();
    if (oi->port) {
        tags[0] = OIT_Events;
        tags[1] = OIMC_ADDED | OIMC_REMOVED;
        tags[2] = TAG_END;
        oi->notify = OIN_AddNotifyA(oi->port, (struct TagItem *)tags);
    }
    oi->rescan = 1;
}

/* Which pad to take: a plugged-in pad (one that can go away: USB, or a pad on
 * AmiCradle's cores) before the Amiga's own joystick port, and port 2 (the
 * joystick port) before the others. One another program has exclusively is
 * passed over. */
static int oi_rank(const struct OIControllerInfo *info) {
    if (info->oci_Flags & OICF_INUSE) {
        return 0;
    }
    if (info->oci_Flags & OICF_REMOVABLE) {
        return 3;
    }
    return info->oci_LegacyPort == 1 ? 2 : 1;
}

static void oi_pick(acgame_openinput *oi) {
    struct OIControllerInfo list[ACGAME_OI_MAX];
    ULONG n, i, tags[5];
    int tries;
    oi->rescan = 0;
    n = OIN_ListControllers(list, ACGAME_OI_MAX, sizeof(list[0]));
    if (n > ACGAME_OI_MAX) {
        n = ACGAME_OI_MAX;
    }
    if (oi->handle) {
        /* Keep the pad in use unless a plugged-in one has come and this is
         * the Amiga's own port. */
        int better = 0;
        if (oi->flags & OICF_REMOVABLE) {
            return;
        }
        for (i = 0; i < n; ++i) {
            if (list[i].oci_ID != oi->id && oi_rank(&list[i]) == 3) {
                better = 1;
            }
        }
        if (!better) {
            return;
        }
        oi_close_pad(oi);
    }
    for (tries = 0; tries < ACGAME_OI_MAX; ++tries) {
        ULONG best = n;
        int best_rank = 0;
        for (i = 0; i < n; ++i) {
            const int rank = oi_rank(&list[i]);
            if (rank > best_rank) {
                best_rank = rank;
                best = i;
            }
        }
        if (best == n) {
            return;
        }
        /* Exclusive while the game runs: the pad stops driving its Amiga port
         * for older programs until it is closed. */
        tags[0] = OIT_Exclusive;
        tags[1] = TRUE;
        tags[2] = OIT_Player;
        tags[3] = 0;
        tags[4] = TAG_END;
        oi->handle = OIN_OpenControllerA(list[best].oci_ID, (struct TagItem *)tags);
        if (oi->handle) {
            oi->id = list[best].oci_ID;
            oi->flags = list[best].oci_Flags;
            memcpy(oi->name, list[best].oci_Name, sizeof(oi->name));
            oi->name[sizeof(oi->name) - 1] = 0;
            return;
        }
        list[best].oci_Flags |= OICF_INUSE;     /* try the next one */
    }
}

/* 1 when the pad came through GFOpenInput; 0 leaves it to lowlevel.library. */
static int oi_read(acgame_openinput *oi, acgame_pad_state *pad) {
    struct OIState st;
    struct OIMessage *msg;
    if (!OpenInputBase) {
        return 0;
    }
    if (oi->port) {
        while ((msg = (struct OIMessage *)GetMsg(oi->port)) != NULL) {
            if (msg->oim_Class & (OIMC_ADDED | OIMC_REMOVED)) {
                oi->rescan = 1;
            }
            ReplyMsg((struct Message *)msg);
        }
    } else if (!oi->handle) {
        oi->rescan = 1;     /* no notification: look again at each poll */
    }
    if (oi->rescan) {
        oi_pick(oi);
    }
    if (!oi->handle) {
        return 0;
    }
    if (OIN_ReadState(oi->handle, &st, sizeof(st)) != OIERR_OK || !(st.ois_Flags & OISF_CONNECTED)) {
        oi_close_pad(oi);   /* it went: the next poll takes another */
        oi->rescan = 1;
        return 0;
    }
    pad->connected = 1;
    pad->source = ACGAME_PAD_OPENINPUT;
    pad->buttons = (uint32_t)st.ois_Buttons;
    memcpy(pad->axes, st.ois_Axes, sizeof(pad->axes));
    pad->name = oi->name;
    return 1;
}
#endif

/* Joystick or CD32 pad in port 2 (lowlevel unit 1), in the standard layout. */
static void lowlevel_read(acgame_pad_state *pad) {
    ULONG joy, type;
    uint32_t b = 0;
    if (!LowLevelBase) {
        return;
    }
    joy = ReadJoyPort(1);
    type = joy & JP_TYPE_MASK;
    if (type == JP_TYPE_NOTAVAIL || type == JP_TYPE_MOUSE) {
        return;
    }
    if (joy & JPF_JOY_UP) b |= ACGAME_PAD_BIT(ACGAME_PAD_DPAD_UP);
    if (joy & JPF_JOY_DOWN) b |= ACGAME_PAD_BIT(ACGAME_PAD_DPAD_DOWN);
    if (joy & JPF_JOY_LEFT) b |= ACGAME_PAD_BIT(ACGAME_PAD_DPAD_LEFT);
    if (joy & JPF_JOY_RIGHT) b |= ACGAME_PAD_BIT(ACGAME_PAD_DPAD_RIGHT);
    if (joy & JPF_BUTTON_RED) b |= ACGAME_PAD_BIT(ACGAME_PAD_A);
    if (joy & JPF_BUTTON_BLUE) b |= ACGAME_PAD_BIT(ACGAME_PAD_B);
    if (type == JP_TYPE_GAMECTLR) {
        if (joy & JPF_BUTTON_GREEN) b |= ACGAME_PAD_BIT(ACGAME_PAD_X);
        if (joy & JPF_BUTTON_YELLOW) b |= ACGAME_PAD_BIT(ACGAME_PAD_Y);
        if (joy & JPF_BUTTON_PLAY) b |= ACGAME_PAD_BIT(ACGAME_PAD_START);
        if (joy & JPF_BUTTON_REVERSE) b |= ACGAME_PAD_BIT(ACGAME_PAD_LEFTSHOULDER);
        if (joy & JPF_BUTTON_FORWARD) b |= ACGAME_PAD_BIT(ACGAME_PAD_RIGHTSHOULDER);
    }
    if (type == JP_TYPE_UNKNOWN && !b) {
        return;     /* nothing seen in the port yet */
    }
    pad->connected = 1;
    pad->source = ACGAME_PAD_LOWLEVEL;
    pad->buttons = b;
    pad->axes[ACGAME_PAD_LEFTX] = (int16_t)((b & ACGAME_PAD_BIT(ACGAME_PAD_DPAD_LEFT)) ? -32768 :
                                            (b & ACGAME_PAD_BIT(ACGAME_PAD_DPAD_RIGHT)) ? 32767 : 0);
    pad->axes[ACGAME_PAD_LEFTY] = (int16_t)((b & ACGAME_PAD_BIT(ACGAME_PAD_DPAD_UP)) ? -32768 :
                                            (b & ACGAME_PAD_BIT(ACGAME_PAD_DPAD_DOWN)) ? 32767 : 0);
    pad->name = type == JP_TYPE_GAMECTLR ? "CD32 pad, port 2" : "Joystick, port 2";
}

/* The pad's d-pad or left stick, A, B, X and Start, ORed over the keyboard. */
static void pad_to_keys(const acgame_pad_state *pad, acgame_input_state *state) {
    const uint32_t b = pad->buttons;
    const int16_t lx = pad->axes[ACGAME_PAD_LEFTX];
    const int16_t ly = pad->axes[ACGAME_PAD_LEFTY];
    if (!pad->connected) {
        return;
    }
    if ((b & ACGAME_PAD_BIT(ACGAME_PAD_DPAD_UP)) || ly < -16384) state->key[ACGAME_KEY_UP] = 1;
    if ((b & ACGAME_PAD_BIT(ACGAME_PAD_DPAD_DOWN)) || ly > 16384) state->key[ACGAME_KEY_DOWN] = 1;
    if ((b & ACGAME_PAD_BIT(ACGAME_PAD_DPAD_LEFT)) || lx < -16384) state->key[ACGAME_KEY_LEFT] = 1;
    if ((b & ACGAME_PAD_BIT(ACGAME_PAD_DPAD_RIGHT)) || lx > 16384) state->key[ACGAME_KEY_RIGHT] = 1;
    if (b & ACGAME_PAD_BIT(ACGAME_PAD_A)) state->key[ACGAME_KEY_FIRE1] = 1;
    if (b & ACGAME_PAD_BIT(ACGAME_PAD_B)) state->key[ACGAME_KEY_FIRE2] = 1;
    if (b & ACGAME_PAD_BIT(ACGAME_PAD_X)) state->key[ACGAME_KEY_FIRE3] = 1;
    if (b & ACGAME_PAD_BIT(ACGAME_PAD_START)) state->key[ACGAME_KEY_START] = 1;
}

static void close_input(void) {
#if ACGAME_OPENINPUT
    oi_close(&g_acgame_amiga.oi);
#endif
    if (LowLevelBase) {
        CloseLibrary(LowLevelBase);
        LowLevelBase = NULL;
    }
}

/* ---- display ---------------------------------------------------------------- */

/* Takes one SafeMessage: the buffer that the message's change replaced is
 * no longer scanned out and may be written. */
static void take_safe_message(struct Message *msg) {
    acgame_amiga_state *g = &g_acgame_amiga;
    int i;
    for (i = 0; i < g->buffers; ++i) {
        if (msg == &g->buffer[i]->sb_DBufInfo->dbi_SafeMessage) {
            if (g->replaced[i] >= 0) {
                g->writable[g->replaced[i]] = 1;
                g->replaced[i] = -1;
            }
            break;
        }
    }
    --g->safe_pending;
}

static void collect_safe(int block) {
    acgame_amiga_state *g = &g_acgame_amiga;
    struct Message *msg;
    for (;;) {
        while ((msg = GetMsg(g->safe_port)) != NULL) {
            take_safe_message(msg);
            block = 0;
        }
        if (!block || g->safe_pending <= 0) {
            return;
        }
        Wait(1UL << g->safe_port->mp_SigBit);
    }
}

static void collect_shown(void) {
    acgame_amiga_state *g = &g_acgame_amiga;
    while (g->shown_pending) {
        if (GetMsg(g->shown_port)) {
            g->shown_pending = 0;
        } else {
            Wait(1UL << g->shown_port->mp_SigBit);
        }
    }
}

/* Puts buffer i on display at the next vertical blank. */
static int change_buffer(int i) {
    acgame_amiga_state *g = &g_acgame_amiga;
    collect_shown();
    if (!ChangeScreenBuffer(g->screen, g->buffer[i])) {
        return 0;
    }
    g->replaced[i] = g->shown;
    g->writable[i] = 0;
    g->shown = i;
    g->shown_pending = 1;
    ++g->safe_pending;
    return 1;
}

static void close_double_buffer(void) {
    acgame_amiga_state *g = &g_acgame_amiga;
    int i;
    if (g->buffers) {
        collect_shown();
        collect_safe(1);
        while (g->safe_pending > 0) {
            collect_safe(1);
        }
        if (g->shown != 0 && change_buffer(0)) {
            /* the screen's own bitmap goes back on display before the copies are freed */
            collect_shown();
            while (g->safe_pending > 0) {
                collect_safe(1);
            }
        }
    }
    for (i = ACGAME_SCREEN_BUFFERS - 1; i >= 0; --i) {
        if (g->buffer[i]) {
            FreeScreenBuffer(g->screen, g->buffer[i]);
            g->buffer[i] = NULL;
        }
    }
    g->buffers = 0;
    if (g->safe_port) {
        DeleteMsgPort(g->safe_port);
        g->safe_port = NULL;
    }
    if (g->shown_port) {
        DeleteMsgPort(g->shown_port);
        g->shown_port = NULL;
    }
}

static int bitmap_is_planar8(struct BitMap *bitmap);

/* Without Chip RAM for a second buffer frames go single-buffered into the
 * screen's bitmap after WaitTOF, as before. */
static void open_double_buffer(void) {
    acgame_amiga_state *g = &g_acgame_amiga;
    int i, n = 0;
    g->safe_port = CreateMsgPort();
    g->shown_port = CreateMsgPort();
    if (g->safe_port && g->shown_port) {
        for (i = 0; i < ACGAME_SCREEN_BUFFERS; ++i) {
            g->buffer[i] = AllocScreenBuffer(g->screen, NULL, i == 0 ? SB_SCREEN_BITMAP : SB_COPY_BITMAP);
            if (!g->buffer[i] || !bitmap_is_planar8(g->buffer[i]->sb_BitMap)) {
                break;
            }
            g->buffer[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = g->safe_port;
            g->buffer[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = g->shown_port;
            g->replaced[i] = -1;
            g->writable[i] = i != 0;
            n = i + 1;
        }
    }
    if (n < 2) {
        close_double_buffer();
        return;
    }
    g->buffers = n;
    g->shown = 0;
    g->safe_pending = 0;
    g->shown_pending = 0;
}

/* SetPointer's sprite: the two control words, one empty line, the two end
 * words. */
#define ACGAME_BLANK_POINTER_SIZE (6 * sizeof(UWORD))

static void close_display(void) {
    close_double_buffer();
    if (g_acgame_amiga.held) {
        ReplyMsg((struct Message *)g_acgame_amiga.held);
        g_acgame_amiga.held = NULL;
    }
    if (g_acgame_amiga.window) {
        CloseWindow(g_acgame_amiga.window);
        g_acgame_amiga.window = NULL;
    }
    if (g_acgame_amiga.blank_pointer) {
        FreeMem(g_acgame_amiga.blank_pointer, ACGAME_BLANK_POINTER_SIZE);
        g_acgame_amiga.blank_pointer = NULL;
    }
    if (g_acgame_amiga.screen) {
        CloseScreen(g_acgame_amiga.screen);
        g_acgame_amiga.screen = NULL;
    }
#if ACGAME_OPENGPU
    g_gpu.active = 0;
    if (OpenGPUBase) {
        CloseLibrary(OpenGPUBase);
        OpenGPUBase = NULL;
    }
    if (OpenRTGBase) {
        CloseLibrary(OpenRTGBase);
        OpenRTGBase = NULL;
    }
#endif
}

/* Whether GFOpenGfx has graphics.library's drawing calls (gfopengpu.library 0.8
 * and later, patches in): then WriteChunkyPixels on an RTG screen is done by
 * the cores, not the 68k. A planar screen still takes ACGame's own C2P, as
 * GFOpenGfx has no planar path for WriteChunkyPixels yet. */
static int opengfx_active(void) {
#if ACGAME_OPENGPU
    if (!OpenGPUBase) {
        OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
    }
    if (!OpenGPUBase || OpenGPUBase->lib_Revision < OPENGPU_OGFX_ALL_REVISION) {
        return 0;
    }
    return (OGFX_Status() & (OGFX_STATUS_PATCHED | OGFX_STATUS_ENABLED)) ==
           (OGFX_STATUS_PATCHED | OGFX_STATUS_ENABLED);
#else
    return 0;
#endif
}

/* ENV:ACGame/Display (AGA, RTG or AUTO) overrides what the game asked for, so
 * a player can push a game onto the other kind of screen without a rebuild. */
static acgame_display_kind display_from_env(acgame_display_kind wanted) {
    char value[16];
    if (!env_value("ACGame/Display", value, sizeof(value))) {
        return wanted;
    }
    if (value[0] == 'A' || value[0] == 'a') {
        return (value[1] == 'G' || value[1] == 'g') ? ACGAME_DISPLAY_AGA : ACGAME_DISPLAY_AUTO;
    }
    if (value[0] == 'R' || value[0] == 'r') {
        return ACGAME_DISPLAY_RTG;
    }
    return wanted;
}

static ULONG pick_mode(const acgame_video_config *config, acgame_display_kind kind) {
    if (kind == ACGAME_DISPLAY_AGA) {
        /* The native chipset on the monitor the machine booted with: lores
         * for a frame up to 384 wide, hires above that, and interlace for one
         * taller than 256, so a 640x480 frame shows whole (on PAL). The
         * default custom screen (lores) when the mode is not available. */
        ULONG id = 0;
        if (config->width > 384) {
            id |= HIRES_KEY;
        }
        if (config->height > 256) {
            id |= LORESLACE_KEY;
        }
        if (id && ModeNotAvailable(id) == 0) {
            return id;
        }
        return INVALID_ID;
    }
    return BestModeID(BIDTAG_NominalWidth, config->width,
                      BIDTAG_NominalHeight, config->height,
                      BIDTAG_DesiredWidth, config->width,
                      BIDTAG_DesiredHeight, config->height,
                      BIDTAG_Depth, 8,
                      TAG_END);
}

/* The PropertyFlags bit Picasso96 and GFOpenRTG set on a board's modes. */
#define ACGAME_DIPF_BOARD 0x02000000UL

static int is_board_mode(ULONG mode) {
    struct DisplayInfo info;
    if (mode == (ULONG)INVALID_ID ||
        !GetDisplayInfoData(NULL, (UBYTE *)&info, sizeof(info), DTAG_DISP, mode)) {
        return 0;
    }
    return (info.PropertyFlags & (ACGAME_DIPF_BOARD | DIPF_IS_FOREIGN)) != 0;
}

/* BestModeID passes over GFOpenRTG's modes (GFOpenRTG 0.7, 6 Oct 2026), and a
 * board may have no mode of the frame's own size, so walk the database for
 * the smallest 8-bit board mode that holds the frame. */
static ULONG find_board_mode(const acgame_video_config *config, unsigned *width, unsigned *height) {
    ULONG mode = INVALID_ID;
    ULONG best = INVALID_ID;
    unsigned long best_area = 0;
    while ((mode = NextDisplayInfo(mode)) != (ULONG)INVALID_ID) {
        struct DimensionInfo dims;
        unsigned w, h;
        if (!is_board_mode(mode) ||
            !GetDisplayInfoData(NULL, (UBYTE *)&dims, sizeof(dims), DTAG_DIMS, mode) ||
            dims.MaxDepth != 8) {
            continue;
        }
        w = (unsigned)(dims.Nominal.MaxX - dims.Nominal.MinX + 1);
        h = (unsigned)(dims.Nominal.MaxY - dims.Nominal.MinY + 1);
        if (w < config->width || h < config->height) {
            continue;
        }
        if (best == (ULONG)INVALID_ID || (unsigned long)w * h < best_area) {
            best = mode;
            best_area = (unsigned long)w * h;
            *width = w;
            *height = h;
        }
    }
    return best;
}

/* All 256 colours black, in LoadRGB32's form: the screen opens dark
 * instead of in the system's grey until the game's first picture. */
static ULONG g_black_colours[770];

static struct Screen *open_screen_depth(const acgame_video_config *config, ULONG mode, ULONG depth) {
    g_black_colours[0] = 256UL << 16;
    if (mode == (ULONG)INVALID_ID) {
        return OpenScreenTags(NULL,
                              SA_Type, CUSTOMSCREEN,
                              SA_Width, config->width,
                              SA_Height, config->height,
                              SA_Depth, depth,
                              SA_Colors32, (ULONG)g_black_colours,
                              SA_ShowTitle, FALSE,
                              SA_Draggable, FALSE,
                              SA_Quiet, TRUE,
                              TAG_END);
    }
    return OpenScreenTags(NULL,
                          SA_Type, CUSTOMSCREEN,
                          SA_DisplayID, mode,
                          SA_Width, config->width,
                          SA_Height, config->height,
                          SA_Depth, depth,
                          SA_Colors32, (ULONG)g_black_colours,
                          SA_ShowTitle, FALSE,
                          SA_Draggable, FALSE,
                          SA_Quiet, TRUE,
                          TAG_END);
}

static struct Screen *open_screen(const acgame_video_config *config, ULONG mode) {
    return open_screen_depth(config, mode, 8);
}

#if ACGAME_OPENGPU
/* ENV:ACGame/OpenGPU set to 0 or OFF keeps the 8-bit screen on RTG. */
static int gpu_wanted(acgame_display_kind kind) {
    char value[16];
    if (kind == ACGAME_DISPLAY_AGA) {
        return 0;
    }
    if (GetVar((CONST_STRPTR)"ACGame/OpenGPU", (STRPTR)value, sizeof(value), 0) > 0 &&
        (value[0] == '0' || value[0] == 'o' || value[0] == 'O') && value[1] != 'n' && value[1] != 'N') {
        return 0;
    }
    if (!OpenGPUBase) {
        OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
    }
    if (!OpenRTGBase) {
        OpenRTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, OPENRTG_VERSION);
    }
    return OpenGPUBase && OpenRTGBase;
}

/* The 32-bit mode to draw on: the host window's own size (GFOpenRTG's
 * Native mode) where there is one, else a board mode twice the frame (at
 * least 640x480). INVALID_ID when there is none. */
static ULONG gpu_pick_mode(const acgame_video_config *config, unsigned *width, unsigned *height) {
    struct OpenRTGNativeSize native;
    struct OpenRTGMode info;
    ULONG mode;
    unsigned w = config->width * 2u, h = config->height * 2u;

    memset(&native, 0, sizeof(native));
    if (ORTG_NativeSize(1, &native) && native.width >= config->width &&
        native.height >= config->height && native.mode_id) {
        *width = native.width;
        *height = native.height;
        return native.mode_id;
    }
    if (w < 640) {
        w = 640;
    }
    if (h < 480) {
        h = 480;
    }
    mode = ORTG_BestMode(1, w, h, 32);
    if (mode == (ULONG)INVALID_ID || mode == 0 || !ORTG_GetMode(mode, &info) || info.depth != 32 ||
        info.width < config->width || info.height < config->height) {
        return INVALID_ID;
    }
    *width = info.width;
    *height = info.height;
    return mode;
}

/* After the screen opened: its pixels by address in a format OpenGPU
 * composites to, with the 1.7 palette. 0 when OpenGPU can't do it here. */
static int gpu_setup(const acgame_video_config *config, unsigned sw, unsigned sh) {
    struct OpenRTGBitMapInfo info;
    unsigned sx, sy;

    memset(&g_gpu, 0, sizeof(g_gpu));
    if (!ORTG_BitMapInfo(g_acgame_amiga.screen->RastPort.BitMap, &info) || !info.memory) {
        return 0;
    }
    if (info.format == 2) {
        g_gpu.format = OGPU_FMT_ARGB32;
    } else if (info.format == 1) {
        g_gpu.format = OGPU_FMT_RGB565;
    } else {
        return 0;
    }
    if (OGPU_ANSWER(OGPU_Query(OGPU_OP_COMPOSITE, g_gpu.format)) != OGPU_FULL ||
        OGPU_ANSWER(OGPU_Query(OGPU_OP_PALETTE, g_gpu.format)) == OGPU_NONE) {
        return 0;
    }
    g_gpu.screen_w = sw;
    g_gpu.screen_h = sh;
    sx = sw / config->width;
    sy = sh / config->height;
    g_gpu.scale = sx < sy ? sx : sy;
    if (g_gpu.scale < 1) {
        g_gpu.scale = 1;
    }
    g_gpu.dw = config->width * g_gpu.scale;
    g_gpu.dh = config->height * g_gpu.scale;
    g_gpu.dx = (sw - g_gpu.dw) / 2u;
    g_gpu.dy = (sh - g_gpu.dh) / 2u;
    g_gpu.active = 1;
    return 1;
}

/* The frame (8-bit pens) and its palette onto the screen through OpenGPU,
 * scaled into the middle; the first time, the whole screen black first. */
static int gpu_present(const uint8_t *pixels, unsigned pitch, unsigned width, unsigned height,
                       const acgame_rgb8 palette[256]) {
    struct OpenRTGBitMapInfo info;
    ULONG *w = g_gpu.stream;
    ULONG fence = 0;
    unsigned i;

    if (!ORTG_BitMapInfo(g_acgame_amiga.screen->RastPort.BitMap, &info) || !info.memory) {
        return -1;
    }
    for (i = 0; i < 256; ++i) {
        g_gpu.table[i] = 0xFF000000UL | ((ULONG)palette[i].r << 16) |
                         ((ULONG)palette[i].g << 8) | (ULONG)palette[i].b;
    }
    *w++ = OGPU_HDR(OGPU_OP_SURFACE, 6); *w++ = 0; *w++ = (ULONG)info.memory;
    *w++ = info.bytes_per_row; *w++ = OGPU_XY(g_gpu.screen_w, g_gpu.screen_h); *w++ = g_gpu.format;
    *w++ = OGPU_HDR(OGPU_OP_SURFACE, 6); *w++ = 1; *w++ = (ULONG)pixels;
    *w++ = pitch; *w++ = OGPU_XY(width, height); *w++ = OGPU_FMT_INDEX8;
    *w++ = OGPU_HDR(OGPU_OP_PALETTE, 3); *w++ = 1; *w++ = (ULONG)g_gpu.table;
    *w++ = OGPU_HDR(OGPU_OP_TARGET, 2); *w++ = 0;
    if (!g_gpu.cleared) {
        *w++ = OGPU_HDR(OGPU_OP_FILL, 4); *w++ = OGPU_XY(0, 0);
        *w++ = OGPU_XY(g_gpu.screen_w, g_gpu.screen_h); *w++ = 0xFF000000UL;
        g_gpu.cleared = 1;
    }
    *w++ = OGPU_HDR(OGPU_OP_COMPOSITE, 8); *w++ = 1; *w++ = OGPU_XY(0, 0);
    *w++ = OGPU_XY(width, height); *w++ = OGPU_XY(g_gpu.dx, g_gpu.dy);
    *w++ = OGPU_XY(g_gpu.dw, g_gpu.dh); *w++ = 255; *w++ = 0;
    *w++ = OGPU_HDR(OGPU_OP_FENCE, 2); *w++ = 1;
    if (OGPU_Submit(g_gpu.stream, (ULONG)(w - g_gpu.stream), &fence) != 0) {
        return -1;
    }
    OGPU_Wait(fence);
    return 0;
}
#endif

static int bitmap_is_planar8(struct BitMap *bitmap) {
    unsigned p;
    if (!bitmap || GetBitMapAttr(bitmap, BMA_DEPTH) < 8 ||
        !(GetBitMapAttr(bitmap, BMA_FLAGS) & BMF_STANDARD)) {
        return 0;
    }
    for (p = 0; p < 8; ++p) {
        if (!bitmap->Planes[p]) {
            return 0;
        }
    }
    return 1;
}

static void build_palette_table(const acgame_rgb8 palette[256], ULONG table[770]) {
    unsigned i;
    table[0] = (256UL << 16);
    for (i = 0; i < 256; ++i) {
        table[1 + i * 3 + 0] = (ULONG)palette[i].r * 0x01010101UL;
        table[1 + i * 3 + 1] = (ULONG)palette[i].g * 0x01010101UL;
        table[1 + i * 3 + 2] = (ULONG)palette[i].b * 0x01010101UL;
    }
    table[769] = 0;
}

static int update_palette(const acgame_rgb8 palette[256]) {
    ULONG table[770];
    if (!g_acgame_amiga.screen) {
        return -1;
    }
    if (g_acgame_amiga.palette_valid &&
        memcmp(g_acgame_amiga.last_palette, palette, sizeof(g_acgame_amiga.last_palette)) == 0) {
        return 0;
    }
    build_palette_table(palette, table);
    LoadRGB32(&g_acgame_amiga.screen->ViewPort, table);
    memcpy(g_acgame_amiga.last_palette, palette, sizeof(g_acgame_amiga.last_palette));
    g_acgame_amiga.palette_valid = 1;
    return 0;
}

static int c2p_into(struct BitMap *bitmap, const acgame_indexed_surface *frame) {
    uint8_t *planes[8];
    unsigned p;
    for (p = 0; p < 8; ++p) {
        planes[p] = (uint8_t *)bitmap->Planes[p];
    }
    return acgame_c2p8_fast(frame->pixels, frame->pitch, planes, bitmap->BytesPerRow,
                            frame->width, frame->height);
}

/* Planar, buffered: convert into a buffer off the display, then show it and
 * its palette together at the next vertical blank. */
static int present_double_buffered(const acgame_indexed_surface *frame) {
    acgame_amiga_state *g = &g_acgame_amiga;
    int draw = -1, i, result;
    if (!frame->pixels || frame->width != g->width || frame->height != g->height) {
        return -1;
    }
    collect_safe(0);
    for (;;) {
        for (i = 0; i < g->buffers && draw < 0; ++i) {
            if (i != g->shown && g->writable[i]) {
                draw = i;
            }
        }
        if (draw >= 0) {
            break;
        }
        if (g->safe_pending <= 0) {
            /* every change has been answered: nothing off display is scanned */
            for (i = 0; i < g->buffers; ++i) {
                g->writable[i] = i != g->shown;
            }
            continue;
        }
        collect_safe(1);
    }
    result = c2p_into(g->buffer[draw]->sb_BitMap, frame);
    if (result != 0) {
        return result;
    }
    /* A refused change (Intuition busy) drops this frame; the buffer stays
     * writable for the next one. */
    change_buffer(draw);
    return update_palette(frame->palette);
}

static int copy_to_screen(const acgame_indexed_surface *frame) {
    struct BitMap *bitmap;

    if (!g_acgame_amiga.screen || !frame || !frame->pixels ||
        frame->width != g_acgame_amiga.width || frame->height != g_acgame_amiga.height) {
        return -1;
    }
    if (!g_acgame_amiga.planar) {
        const unsigned x = g_acgame_amiga.left;
        const unsigned y = g_acgame_amiga.top;
        WriteChunkyPixels(&g_acgame_amiga.screen->RastPort, x, y,
                          x + frame->width - 1, y + frame->height - 1,
                          frame->pixels, (LONG)frame->pitch);
        return 0;
    }
    bitmap = g_acgame_amiga.screen->RastPort.BitMap;
    return c2p_into(bitmap, frame);
}

static void set_key(unsigned raw, int down) {
    g_acgame_amiga.raw[raw & 0x7fu] = (uint8_t)down;
    switch (raw) {
    case 0x4c:
        g_acgame_amiga.keys[ACGAME_KEY_UP] = (uint8_t)down;
        break;
    case 0x4d:
        g_acgame_amiga.keys[ACGAME_KEY_DOWN] = (uint8_t)down;
        break;
    case 0x4f:
        g_acgame_amiga.keys[ACGAME_KEY_LEFT] = (uint8_t)down;
        break;
    case 0x4e:
        g_acgame_amiga.keys[ACGAME_KEY_RIGHT] = (uint8_t)down;
        break;
    case 0x40:
        g_acgame_amiga.keys[ACGAME_KEY_FIRE1] = (uint8_t)down;
        break;
    case 0x64:
        g_acgame_amiga.keys[ACGAME_KEY_FIRE2] = (uint8_t)down;
        break;
    case 0x44:
        g_acgame_amiga.keys[ACGAME_KEY_START] = (uint8_t)down;
        break;
    case 0x45:
        g_acgame_amiga.keys[ACGAME_KEY_ESCAPE] = (uint8_t)down;
        if (down) {
            g_acgame_amiga.quit_requested = 1;
        }
        break;
    default:
        break;
    }
}

int acgame_platform_init(const acgame_video_config *config) {
    /* DELTAMOVE: relative mouse movement for games that steer with it.
     * INACTIVEWINDOW: the key and button releases then go elsewhere. */
    ULONG idcmp = IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_DELTAMOVE |
                  IDCMP_INACTIVEWINDOW;
    acgame_display_kind kind;
    acgame_video_config screen_config;
    ULONG mode;

    memset(&g_acgame_amiga, 0, sizeof(g_acgame_amiga));

    if (!config || config->mode != ACGAME_VIDEO_INDEXED8 ||
        config->width == 0 || config->height == 0 || (config->width & 7u)) {
        return -1;
    }

    kind = display_from_env(config->display);
    screen_config = *config;
#if ACGAME_OPENGPU
    if (gpu_wanted(kind)) {
        unsigned w = 0, h = 0;
        mode = gpu_pick_mode(config, &w, &h);
        if (mode != (ULONG)INVALID_ID) {
            screen_config.width = w;
            screen_config.height = h;
            g_acgame_amiga.screen = open_screen_depth(&screen_config, mode, 32);
            if (g_acgame_amiga.screen && !gpu_setup(config, w, h)) {
                CloseScreen(g_acgame_amiga.screen);
                g_acgame_amiga.screen = NULL;
            }
        }
        if (!g_acgame_amiga.screen) {
            screen_config = *config;
        }
    }
    if (g_acgame_amiga.screen) {
        goto opened;
    }
#endif
    mode = pick_mode(config, kind);
    if (kind == ACGAME_DISPLAY_RTG && !is_board_mode(mode)) {
        unsigned w = 0, h = 0;
        mode = find_board_mode(config, &w, &h);
        if (mode != (ULONG)INVALID_ID) {
            screen_config.width = w;
            screen_config.height = h;
        }
    }
    g_acgame_amiga.screen = open_screen(&screen_config, mode);
    if (!g_acgame_amiga.screen && mode != (ULONG)INVALID_ID && kind == ACGAME_DISPLAY_AUTO) {
        screen_config = *config;
        g_acgame_amiga.screen = open_screen(&screen_config, INVALID_ID);
    }
    if (!g_acgame_amiga.screen) {
        return -1;
    }

#if ACGAME_OPENGPU
opened:
#endif
    g_acgame_amiga.planar = bitmap_is_planar8(g_acgame_amiga.screen->RastPort.BitMap);
    if ((kind == ACGAME_DISPLAY_AGA && !g_acgame_amiga.planar) ||
        (kind == ACGAME_DISPLAY_RTG && g_acgame_amiga.planar) ||
        (!g_acgame_amiga.planar && ((struct Library *)GfxBase)->lib_Version < 40)) {
        /* WriteChunkyPixels is graphics V40 (OS 3.1). */
        close_display();
        return -1;
    }

    if (g_acgame_amiga.planar) {
        open_double_buffer();
    }

    g_acgame_amiga.window = OpenWindowTags(
        NULL,
        WA_CustomScreen, g_acgame_amiga.screen,
        WA_Left, 0,
        WA_Top, 0,
        WA_Width, screen_config.width,
        WA_Height, screen_config.height,
        WA_Activate, TRUE,
        WA_Borderless, TRUE,
        WA_Backdrop, TRUE,
        WA_RMBTrap, TRUE,
        WA_NoCareRefresh, TRUE,
        WA_ReportMouse, TRUE,
        WA_IDCMP, idcmp,
        TAG_END);
    if (!g_acgame_amiga.window) {
        close_display();
        return -1;
    }
    g_acgame_amiga.pixels = (uint8_t *)malloc((size_t)config->width * config->height);
    if (!g_acgame_amiga.pixels) {
        close_display();
        return -1;
    }
    memset(g_acgame_amiga.pixels, 0, (size_t)config->width * config->height);

    g_acgame_amiga.opengfx = opengfx_active();
    LowLevelBase = OpenLibrary((CONST_STRPTR)"lowlevel.library", 40);
#if ACGAME_OPENINPUT
    oi_open(&g_acgame_amiga.oi);
#endif

    g_acgame_amiga.screen_width = screen_config.width;
    g_acgame_amiga.screen_height = screen_config.height;
    g_acgame_amiga.width = config->width;
    g_acgame_amiga.height = config->height;
    g_acgame_amiga.left = (screen_config.width - config->width) / 2u;
    g_acgame_amiga.top = (screen_config.height - config->height) / 2u;
    g_acgame_amiga.frame.width = (uint16_t)config->width;
    g_acgame_amiga.frame.height = (uint16_t)config->height;
    g_acgame_amiga.frame.pitch = config->width;
    g_acgame_amiga.frame.pixels = g_acgame_amiga.pixels;
    return 0;
}

void acgame_platform_shutdown(void) {
    free(g_acgame_amiga.pixels);
    g_acgame_amiga.pixels = NULL;
    close_input();
    close_display();
    memset(&g_acgame_amiga, 0, sizeof(g_acgame_amiga));
}

acgame_indexed_surface *acgame_platform_begin_indexed_frame(void) {
    return g_acgame_amiga.pixels ? &g_acgame_amiga.frame : NULL;
}

int acgame_platform_present_indexed(const acgame_indexed_surface *frame) {
    if (!frame || !g_acgame_amiga.screen) {
        return -1;
    }
#if ACGAME_OPENGPU
    if (g_gpu.active) {
        memcpy(g_acgame_amiga.last_palette, frame->palette, sizeof(g_acgame_amiga.last_palette));
        g_acgame_amiga.palette_valid = 1;
        return gpu_present(frame->pixels, frame->pitch, frame->width, frame->height, frame->palette);
    }
#endif
    if (g_acgame_amiga.buffers) {
        return present_double_buffered(frame);
    }
    /* The picture and then its palette, straight after each other: with the
     * vertical blank wait between them, a new palette could be on show with
     * the old picture for a whole frame (films that change the palette
     * every frame flashed wrong colours). Only a frame with a new palette
     * waits for the blank: on a single-buffered (RTG) screen the wait cost
     * every frame half a refresh on average, and the game sat idle in it. */
    if (!g_acgame_amiga.palette_valid ||
        memcmp(g_acgame_amiga.last_palette, frame->palette, sizeof(g_acgame_amiga.last_palette)) != 0) {
        WaitTOF();
    }
    if (copy_to_screen(frame) != 0) {
        return -1;
    }
    return update_palette(frame->palette);
}

int acgame_platform_set_palette(const acgame_rgb8 palette[256]) {
    if (!palette || !g_acgame_amiga.screen) {
        return -1;
    }
#if ACGAME_OPENGPU
    if (g_gpu.active) {
        /* A true-colour screen has no palette: the last frame again with this one. */
        memcpy(g_acgame_amiga.last_palette, palette, sizeof(g_acgame_amiga.last_palette));
        return gpu_present(g_acgame_amiga.frame.pixels, g_acgame_amiga.frame.pitch,
                           g_acgame_amiga.frame.width, g_acgame_amiga.frame.height, palette);
    }
#endif
    WaitTOF();
    return update_palette(palette);
}

int acgame_platform_display_is_planar(void) {
    return g_acgame_amiga.screen ? g_acgame_amiga.planar : -1;
}

int acgame_platform_get_info(acgame_platform_info *info) {
    if (!info || !g_acgame_amiga.screen) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    info->planar = g_acgame_amiga.planar;
    info->opengfx = g_acgame_amiga.opengfx;
#if ACGAME_OPENINPUT
    if (OpenInputBase) {
        info->openinput = ((int)OpenInputBase->lib_Version << 16) | (int)OpenInputBase->lib_Revision;
    }
#endif
    info->screen_width = g_acgame_amiga.screen_width;
    info->screen_height = g_acgame_amiga.screen_height;
#if ACGAME_OPENGPU
    if (g_gpu.active) {
        /* The frame fills the screen through the scale: to the game, the
         * screen is its own size (it has nothing to centre itself). */
        info->screen_width = g_acgame_amiga.width;
        info->screen_height = g_acgame_amiga.height;
        info->opengfx = 1;
    }
#endif
    return 0;
}

int acgame_platform_present_ham8(const uint8_t *ham_codes, size_t pitch,
                                 const acgame_rgb8 base_palette[64]) {
    (void)ham_codes;
    (void)pitch;
    (void)base_palette;
    return -1;
}

void acgame_platform_poll(acgame_input_state *state) {
    struct IntuiMessage *message;
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    g_acgame_amiga.mouse_dx = 0;
    g_acgame_amiga.mouse_dy = 0;
    /* quit_requested is "Esc was pressed since the last poll", not a latch:
     * a game that uses Esc for a menu or to skip a scene would otherwise see
     * quit at every poll after the first press. */
    g_acgame_amiga.quit_requested = 0;

    /*
     * A key or button pressed and let go between two polls would never be
     * seen held; a game that is slow to poll (a busy frame, a long AGA
     * conversion) would miss a quick click or tap. So a release of something
     * pressed since this poll began is kept back, and handled first at the
     * next poll: the press is seen in this one, the release in the next.
     */
    if (g_acgame_amiga.window && g_acgame_amiga.window->UserPort) {
        uint8_t pressed_raw[ACGAME_RAWKEY_COUNT];
        uint8_t pressed_buttons = 0;

        memset(pressed_raw, 0, sizeof(pressed_raw));
        message = g_acgame_amiga.held;
        g_acgame_amiga.held = NULL;
        if (!message) {
            message = (struct IntuiMessage *)GetMsg(g_acgame_amiga.window->UserPort);
        }
        while (message != NULL) {
            if (message->Class == IDCMP_RAWKEY) {
                const unsigned code = (unsigned)message->Code;
                if ((code & 0x80u) && pressed_raw[code & 0x7fu]) {
                    g_acgame_amiga.held = message;
                    break;
                }
                if (!(code & 0x80u)) {
                    pressed_raw[code & 0x7fu] = 1;
                }
                set_key(code & 0x7fu, (code & 0x80u) == 0);
            } else if (message->Class == IDCMP_MOUSEMOVE) {
                g_acgame_amiga.mouse_dx = (int16_t)(g_acgame_amiga.mouse_dx + message->MouseX);
                g_acgame_amiga.mouse_dy = (int16_t)(g_acgame_amiga.mouse_dy + message->MouseY);
            } else if (message->Class == IDCMP_MOUSEBUTTONS) {
                if ((message->Code == SELECTUP && (pressed_buttons & 1)) ||
                    (message->Code == MENUUP && (pressed_buttons & 2))) {
                    g_acgame_amiga.held = message;
                    break;
                }
                if (message->Code == SELECTDOWN) {
                    g_acgame_amiga.mouse_buttons |= 1;
                    pressed_buttons |= 1;
                } else if (message->Code == SELECTUP) {
                    g_acgame_amiga.mouse_buttons &= (uint8_t)~1u;
                } else if (message->Code == MENUDOWN) {
                    g_acgame_amiga.mouse_buttons |= 2;
                    pressed_buttons |= 2;
                } else if (message->Code == MENUUP) {
                    g_acgame_amiga.mouse_buttons &= (uint8_t)~2u;
                }
            } else if (message->Class == IDCMP_INACTIVEWINDOW) {
                /* Another window has the input now and gets the releases of
                 * what is held, so nothing stays held when the game is back. */
                memset(g_acgame_amiga.keys, 0, sizeof(g_acgame_amiga.keys));
                memset(g_acgame_amiga.raw, 0, sizeof(g_acgame_amiga.raw));
                g_acgame_amiga.mouse_buttons = 0;
            }
            ReplyMsg((struct Message *)message);
            message = (struct IntuiMessage *)GetMsg(g_acgame_amiga.window->UserPort);
        }
    }

    memcpy(state->key, g_acgame_amiga.keys, sizeof(state->key));
#if ACGAME_OPENINPUT
    if (!oi_read(&g_acgame_amiga.oi, &state->pad)) {
        lowlevel_read(&state->pad);
    }
#else
    lowlevel_read(&state->pad);
#endif
    pad_to_keys(&state->pad, state);
    state->mouse_dx = g_acgame_amiga.mouse_dx;
    state->mouse_dy = g_acgame_amiga.mouse_dy;
    state->mouse_buttons = g_acgame_amiga.mouse_buttons;
    state->quit_requested = (uint8_t)g_acgame_amiga.quit_requested;
}

const uint8_t *acgame_platform_rawkeys(void) {
    return g_acgame_amiga.raw;
}

int acgame_platform_pointer(int *x, int *y) {
    if (!g_acgame_amiga.screen) {
        return -1;
    }
#if ACGAME_OPENGPU
    if (g_gpu.active) {
        /* Back through the scale: in frame pixels, outside it when outside. */
        int px = (int)g_acgame_amiga.screen->MouseX - (int)g_gpu.dx;
        int py = (int)g_acgame_amiga.screen->MouseY - (int)g_gpu.dy;
        if (x) {
            *x = px < 0 ? -1 - (-px - 1) / (int)g_gpu.scale : px / (int)g_gpu.scale;
        }
        if (y) {
            *y = py < 0 ? -1 - (-py - 1) / (int)g_gpu.scale : py / (int)g_gpu.scale;
        }
        return 0;
    }
#endif
    if (x) {
        *x = (int)g_acgame_amiga.screen->MouseX - (int)g_acgame_amiga.left;
    }
    if (y) {
        *y = (int)g_acgame_amiga.screen->MouseY - (int)g_acgame_amiga.top;
    }
    return 0;
}

int acgame_platform_show_pointer(int show) {
    if (!g_acgame_amiga.window) {
        return -1;
    }
    if (show) {
        if (g_acgame_amiga.blank_pointer) {
            ClearPointer(g_acgame_amiga.window);
            FreeMem(g_acgame_amiga.blank_pointer, ACGAME_BLANK_POINTER_SIZE);
            g_acgame_amiga.blank_pointer = NULL;
        }
        return 0;
    }
    if (!g_acgame_amiga.blank_pointer) {
        g_acgame_amiga.blank_pointer = AllocMem(ACGAME_BLANK_POINTER_SIZE, MEMF_CHIP | MEMF_CLEAR);
        if (!g_acgame_amiga.blank_pointer) {
            return -1;
        }
        SetPointer(g_acgame_amiga.window, g_acgame_amiga.blank_pointer, 1, 16, 0, 0);
    }
    return 0;
}

uint32_t acgame_platform_ticks_ms(void) {
#ifdef ACGAME_LIBRARY
    /* timer.device's E-clock (the library has no C library's gettimeofday) */
    struct EClockVal ev;
    ULONG hz = ReadEClock(&ev);
    unsigned long long t = ((unsigned long long)ev.ev_hi << 32) | ev.ev_lo;
    return hz ? (uint32_t)(t * 1000u / hz) : 0;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)((uint32_t)tv.tv_sec * 1000u + (uint32_t)(tv.tv_usec / 1000));
#endif
}

int acgame_platform_audio_s16stereo(const int16_t *samples, unsigned frames,
                                    unsigned sample_rate) {
    (void)samples;
    (void)frames;
    (void)sample_rate;
    return -1;
}

#else

int acgame_platform_init(const acgame_video_config *config) {
    (void)config;
    return -1;
}
void acgame_platform_shutdown(void) {}
acgame_indexed_surface *acgame_platform_begin_indexed_frame(void) { return NULL; }
int acgame_platform_present_indexed(const acgame_indexed_surface *frame) {
    (void)frame;
    return -1;
}
int acgame_platform_set_palette(const acgame_rgb8 palette[256]) {
    (void)palette;
    return -1;
}
int acgame_platform_display_is_planar(void) { return -1; }
int acgame_platform_get_info(acgame_platform_info *info) {
    (void)info;
    return -1;
}
int acgame_platform_present_ham8(const uint8_t *ham_codes, size_t pitch,
                                 const acgame_rgb8 base_palette[64]) {
    (void)ham_codes;
    (void)pitch;
    (void)base_palette;
    return -1;
}
void acgame_platform_poll(acgame_input_state *state) {
    if (state) {
        memset(state, 0, sizeof(*state));
    }
}
const uint8_t *acgame_platform_rawkeys(void) {
    static const uint8_t none[ACGAME_RAWKEY_COUNT];
    return none;
}
int acgame_platform_pointer(int *x, int *y) {
    (void)x;
    (void)y;
    return -1;
}
int acgame_platform_show_pointer(int show) {
    (void)show;
    return -1;
}
uint32_t acgame_platform_ticks_ms(void) { return 0; }
int acgame_platform_audio_s16stereo(const int16_t *samples, unsigned frames,
                                    unsigned sample_rate) {
    (void)samples;
    (void)frames;
    (void)sample_rate;
    return -1;
}

#endif
