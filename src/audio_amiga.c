/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * ACGame's sound output (acgame.library 1.3): the mixer (src/acgame_audio.c)
 * in a task of its own, playing through AHI, or through Paula (audio.device)
 * when AHI is not there.
 *
 * AHI: ahi.device's default unit (the music unit the user set in AHI's
 * prefs; on AmiCradle the host's sound), 16-bit stereo, two buffers linked
 * (ahir_Link) so they play back to back.
 * Paula: one left and one right channel from audio.device, 8 bits, two
 * buffers queued on each, the two channels started together.
 *
 * The output opens at the first sound call (or ACG_AudioOpen) and closes
 * with the library (or ACG_AudioClose). ENV:ACGame/Audio picks the device
 * ("ahi", "paula" or "off"); ENV:ACGame/AudioRate the rate (default 22050).
 */
#ifdef ACGAME_LIBRARY
#include "../library/acgame_state.h"   /* a4, before any function */
#endif
#include "acgame/platform.h"
#include "acgame/audio.h"

#if defined(__AROS__) || defined(__amigaos__) || defined(AMIGA)

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/semaphores.h>
#include <devices/audio.h>
#include <dos/dostags.h>
#include <graphics/gfxbase.h>
#ifdef ACGAME_LIBRARY
#include "../include/oa_protos_begin.h"
#endif
#include <proto/exec.h>
#include <proto/dos.h>
#ifdef ACGAME_LIBRARY
#include "../include/oa_protos_end.h"
#endif

extern struct GfxBase *GfxBase;

/* ahi.device's request (AHI 4, devices/ahi.h), so ACGame builds without the
 * AHI developer files. */
struct ACGAHIRequest {
    struct IOStdReq ahir_Std;
    UWORD ahir_Version;
    UWORD ahir_Pad1;
    ULONG ahir_Private[2];
    ULONG ahir_Type;
    ULONG ahir_Frequency;
    LONG ahir_Volume;
    LONG ahir_Position;
    struct ACGAHIRequest *ahir_Link;
};
#define ACG_AHIST_S16S 3UL
#define ACG_AHI_DEFAULT_UNIT 0

#define BACKEND_AHI   1
#define BACKEND_PAULA 2
#define ACG_PAULA_RATE_MAX 28000u   /* period 127: the fastest on a normal display */

typedef struct acgame_audio {
    acgame_mixer mix;               /* first: the biggest part */
    struct SignalSemaphore sem;
    struct Task *parent;
    struct Process *proc;
    BYTE done_sig;
    volatile int quit;
    unsigned flags;                 /* ACGAME_AUDIO_ */
    unsigned rate;
    unsigned buffer_frames;
    int backend;
    int16_t mixbuf[ACGAME_AUDIO_MIX_MAX * 2];
} acgame_audio;

struct acg_start {
    struct Message msg;
    acgame_audio *audio;
    LONG ok;
};

/* in src/platform_aros_aga.c: the opener's audio (NULL: never opened,
 * (void *)1: it failed, and the next sound call does not try again) */
void **acgame_audio_slot(void);
#define AUDIO_FAILED ((void *)1)

static void sem_lock(void *s) { ObtainSemaphore((struct SignalSemaphore *)s); }
static void sem_unlock(void *s) { ReleaseSemaphore((struct SignalSemaphore *)s); }

/* ---- the audio task -------------------------------------------------------- */

static int stop_asked(acgame_audio *a)
{
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) a->quit = 1;
    return a->quit;
}

static int run_ahi(acgame_audio *a, struct acg_start *start)
{
    struct MsgPort *port = CreateMsgPort();
    struct ACGAHIRequest *req[2] = { NULL, NULL };
    struct ACGAHIRequest *link = NULL;
    void *buf[2] = { NULL, NULL };
    ULONG bytes = a->buffer_frames * 4;
    int opened = 0, i = 0;
    int inflight[2] = { 0, 0 };
    if (!port) return 0;
    req[0] = (struct ACGAHIRequest *)CreateIORequest(port, sizeof(struct ACGAHIRequest));
    if (!req[0]) goto out;
    req[0]->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)"ahi.device", ACG_AHI_DEFAULT_UNIT, (struct IORequest *)req[0], 0))
        goto out;
    opened = 1;
    req[1] = (struct ACGAHIRequest *)AllocVec(sizeof(struct ACGAHIRequest), MEMF_PUBLIC);
    buf[0] = AllocVec(bytes, MEMF_PUBLIC);
    buf[1] = AllocVec(bytes, MEMF_PUBLIC);
    if (!req[1] || !buf[0] || !buf[1]) goto out;
    CopyMem(req[0], req[1], sizeof(struct ACGAHIRequest));
    a->backend = BACKEND_AHI;
    start->ok = 1;
    ReplyMsg(&start->msg);          /* the game goes on; from here a is ours until quit */
    while (!stop_asked(a)) {
        struct ACGAHIRequest *r = req[i];
        acgame_mixer_mix(&a->mix, (int16_t *)buf[i], a->buffer_frames);
        r->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
        r->ahir_Std.io_Command = CMD_WRITE;
        r->ahir_Std.io_Data = buf[i];
        r->ahir_Std.io_Length = bytes;
        r->ahir_Std.io_Offset = 0;
        r->ahir_Type = ACG_AHIST_S16S;
        r->ahir_Frequency = a->rate;
        r->ahir_Volume = 0x10000;
        r->ahir_Position = 0x8000;
        r->ahir_Link = link;
        SendIO((struct IORequest *)r);
        inflight[i] = 1;
        if (link) {                 /* the one playing now: wait till it is done */
            while (!CheckIO((struct IORequest *)link)) {
                if (Wait(SIGBREAKF_CTRL_C | (1UL << port->mp_SigBit)) & SIGBREAKF_CTRL_C) {
                    a->quit = 1;
                    break;
                }
            }
            if (a->quit) break;
            WaitIO((struct IORequest *)link);
            inflight[i ^ 1] = 0;
        }
        link = r;
        i ^= 1;
    }
    for (i = 0; i < 2; i++)
        if (inflight[i]) {
            AbortIO((struct IORequest *)req[i]);
            WaitIO((struct IORequest *)req[i]);
        }
out:
    if (opened) CloseDevice((struct IORequest *)req[0]);
    if (buf[0]) FreeVec(buf[0]);
    if (buf[1]) FreeVec(buf[1]);
    if (req[1]) FreeVec(req[1]);
    if (req[0]) DeleteIORequest((struct IORequest *)req[0]);
    DeleteMsgPort(port);
    return a->backend == BACKEND_AHI;
}

/* Paula: channels 0 and 3 are left, 1 and 2 right; one of each. */
static const UBYTE paula_pairs[] = { 3, 5, 10, 12 };

static int run_paula(acgame_audio *a, struct acg_start *start)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOAudio *alloc = NULL;
    struct IOAudio *w[2][2] = { { NULL, NULL }, { NULL, NULL } };   /* [channel][buffer] */
    BYTE *chip[2][2] = { { NULL, NULL }, { NULL, NULL } };
    ULONG n = a->buffer_frames, mask, clock, i, c;
    UWORD period;
    int opened = 0, started = 0, b = 0;
    int inflight[2] = { 0, 0 };
    if (!port) return 0;
    alloc = (struct IOAudio *)CreateIORequest(port, sizeof(struct IOAudio));
    if (!alloc) goto out;
    alloc->ioa_Request.io_Message.mn_Node.ln_Pri = 50;
    alloc->ioa_Data = (UBYTE *)paula_pairs;
    alloc->ioa_Length = sizeof(paula_pairs);
    if (OpenDevice((CONST_STRPTR)"audio.device", 0, (struct IORequest *)alloc, 0)) goto out;
    opened = 1;
    mask = (ULONG)alloc->ioa_Request.io_Unit;
    for (c = 0; c < 2; c++)
        for (i = 0; i < 2; i++) {
            w[c][i] = (struct IOAudio *)AllocVec(sizeof(struct IOAudio), MEMF_PUBLIC);
            chip[c][i] = (BYTE *)AllocVec(n, MEMF_CHIP);
            if (!w[c][i] || !chip[c][i]) goto out;
            CopyMem(alloc, w[c][i], sizeof(struct IOAudio));
            w[c][i]->ioa_Request.io_Unit = (struct Unit *)(mask & (c ? 6UL : 9UL));
        }
    clock = (GfxBase && (GfxBase->DisplayFlags & PAL)) ? 3546895UL : 3579545UL;
    period = (UWORD)(clock / a->rate);
    /* both channels stopped, their period and volume set, two buffers each
     * queued, then started together */
    alloc->ioa_Request.io_Command = CMD_STOP;
    DoIO((struct IORequest *)alloc);
    for (c = 0; c < 2; c++) {
        struct IOAudio *pv = w[c][0];
        pv->ioa_Request.io_Command = ADCMD_PERVOL;
        pv->ioa_Request.io_Flags = 0;
        pv->ioa_Period = period;
        pv->ioa_Volume = 64;
        DoIO((struct IORequest *)pv);
    }
    a->backend = BACKEND_PAULA;
    start->ok = 1;
    ReplyMsg(&start->msg);
    for (;;) {
        int16_t *mixed = a->mixbuf;
        if (inflight[b]) {          /* this buffer's pair is still queued: wait till it has played */
            while (!CheckIO((struct IORequest *)w[0][b]) || !CheckIO((struct IORequest *)w[1][b])) {
                if (Wait(SIGBREAKF_CTRL_C | (1UL << port->mp_SigBit)) & SIGBREAKF_CTRL_C) {
                    a->quit = 1;
                    break;
                }
            }
            if (a->quit) break;
            WaitIO((struct IORequest *)w[0][b]);
            WaitIO((struct IORequest *)w[1][b]);
            inflight[b] = 0;
        }
        if (stop_asked(a)) break;
        acgame_mixer_mix(&a->mix, mixed, n);
        for (i = 0; i < n; i++) {
            chip[0][b][i] = (BYTE)(mixed[i * 2] >> 8);
            chip[1][b][i] = (BYTE)(mixed[i * 2 + 1] >> 8);
        }
        for (c = 0; c < 2; c++) {
            struct IOAudio *r = w[c][b];
            r->ioa_Request.io_Command = CMD_WRITE;
            r->ioa_Data = (UBYTE *)chip[c][b];
            r->ioa_Length = n;
            r->ioa_Period = period;
            r->ioa_Volume = 64;
            r->ioa_Cycles = 1;
            SendIO((struct IORequest *)r);  /* SendIO clears io_Flags: the channel's ADCMD_PERVOL stays */
        }
        inflight[b] = 1;
        if (!started && inflight[0] && inflight[1]) {
            alloc->ioa_Request.io_Command = CMD_START;  /* both channels at once */
            DoIO((struct IORequest *)alloc);
            started = 1;
        }
        b ^= 1;
    }
    if (!started) {
        alloc->ioa_Request.io_Command = CMD_START;  /* so a queued buffer can be taken back */
        DoIO((struct IORequest *)alloc);
    }
    for (i = 0; i < 2; i++)
        if (inflight[i])
            for (c = 0; c < 2; c++) {
                AbortIO((struct IORequest *)w[c][i]);
                WaitIO((struct IORequest *)w[c][i]);
            }
out:
    for (c = 0; c < 2; c++)
        for (i = 0; i < 2; i++) {
            if (chip[c][i]) FreeVec(chip[c][i]);
            if (w[c][i]) FreeVec(w[c][i]);
        }
    if (opened) CloseDevice((struct IORequest *)alloc);   /* frees the channels */
    if (alloc) DeleteIORequest((struct IORequest *)alloc);
    DeleteMsgPort(port);
    return a->backend == BACKEND_PAULA;
}

static void audio_entry(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct acg_start *start;
    acgame_audio *a;
    WaitPort(&me->pr_MsgPort);
    start = (struct acg_start *)GetMsg(&me->pr_MsgPort);
    a = start->audio;
    start->ok = 0;
    if (!(a->flags & ACGAME_AUDIO_PAULA) && run_ahi(a, start)) {
        /* played through AHI until quit */
    } else if (!start->ok && !(a->flags & ACGAME_AUDIO_AHI)) {
        if (a->rate > ACG_PAULA_RATE_MAX) {
            a->rate = ACG_PAULA_RATE_MAX;
            a->mix.out_rate = a->rate;
        }
        run_paula(a, start);
    }
    if (!start->ok) {
        ReplyMsg(&start->msg);      /* no device: the game frees a */
        return;
    }
    Forbid();                       /* the game frees a once we are gone */
    Signal(a->parent, 1UL << a->done_sig);
}

/* ---- the game's side ------------------------------------------------------- */

static int env_word(const char *name, char *value, LONG size)
{
    return GetVar((STRPTR)name, (STRPTR)value, size, 0) > 0;
}

static unsigned env_number(const char *name, unsigned dflt)
{
    char v[16];
    unsigned n = 0;
    int i;
    if (!env_word(name, v, sizeof(v))) return dflt;
    for (i = 0; v[i] >= '0' && v[i] <= '9'; i++) n = n * 10 + (unsigned)(v[i] - '0');
    return n ? n : dflt;
}

static acgame_audio *audio_now(void)
{
    void *p = *acgame_audio_slot();
    return p == AUDIO_FAILED ? NULL : (acgame_audio *)p;
}

int acgame_platform_audio_open(unsigned rate, unsigned flags)
{
    void **slot = acgame_audio_slot();
    acgame_audio *a;
    struct MsgPort *reply;
    struct acg_start start;
    char word[8];
    struct TagItem tags[5];
    if (*slot && *slot != AUDIO_FAILED) return (int)((acgame_audio *)*slot)->rate;
    *slot = AUDIO_FAILED;
    if (env_word("ACGame/Audio", word, sizeof(word))) {
        if ((word[0] | 0x20) == 'o' && (word[1] | 0x20) == 'f') return -1;     /* off */
        if ((word[0] | 0x20) == 'p') flags = ACGAME_AUDIO_PAULA;
        if ((word[0] | 0x20) == 'a') flags = ACGAME_AUDIO_AHI;
    }
    if (!rate) rate = env_number("ACGame/AudioRate", 22050);
    if (rate < 4000) rate = 4000;
    if (rate > 48000) rate = 48000;
    a = (acgame_audio *)AllocVec(sizeof(acgame_audio), MEMF_PUBLIC | MEMF_CLEAR);
    if (!a) return -1;
    reply = CreateMsgPort();
    a->done_sig = AllocSignal(-1);
    if (!reply || a->done_sig < 0) goto fail;
    InitSemaphore(&a->sem);
    a->mix.lock = sem_lock;
    a->mix.unlock = sem_unlock;
    a->mix.lock_data = &a->sem;
    a->flags = flags;
    a->rate = rate;
    /* about 1/20 s a buffer, even, two of them in flight */
    a->buffer_frames = (rate / 20) & ~1u;
    if (a->buffer_frames > ACGAME_AUDIO_MIX_MAX) a->buffer_frames = ACGAME_AUDIO_MIX_MAX;
    acgame_mixer_init(&a->mix, rate);
    a->parent = FindTask(NULL);
    tags[0].ti_Tag = NP_Entry;     tags[0].ti_Data = (ULONG)audio_entry;
    tags[1].ti_Tag = NP_Name;      tags[1].ti_Data = (ULONG)"ACGame audio";
    tags[2].ti_Tag = NP_Priority;  tags[2].ti_Data = 20;
    tags[3].ti_Tag = NP_StackSize; tags[3].ti_Data = 8192;
    tags[4].ti_Tag = TAG_END;      tags[4].ti_Data = 0;
    a->proc = CreateNewProc(tags);
    if (!a->proc) goto fail;
    start.msg.mn_Node.ln_Type = NT_MESSAGE;
    start.msg.mn_ReplyPort = reply;
    start.msg.mn_Length = sizeof(start);
    start.audio = a;
    start.ok = 0;
    PutMsg(&a->proc->pr_MsgPort, &start.msg);
    WaitPort(reply);
    GetMsg(reply);
    DeleteMsgPort(reply);
    reply = NULL;
    if (!start.ok) goto fail;      /* the task has ended */
    *slot = a;
    return (int)a->rate;
fail:
    if (reply) DeleteMsgPort(reply);
    if (a->done_sig >= 0) FreeSignal(a->done_sig);
    FreeVec(a);
    return -1;
}

void acgame_platform_audio_close(void)
{
    void **slot = acgame_audio_slot();
    acgame_audio *a = audio_now();
    if (a) {
        SetSignal(0, 1UL << a->done_sig);
        a->quit = 1;
        Signal(&a->proc->pr_Task, SIGBREAKF_CTRL_C);
        Wait(1UL << a->done_sig);
        FreeSignal(a->done_sig);
        FreeVec(a);
    }
    *slot = NULL;
}

static acgame_audio *audio_get(void)
{
    void *p = *acgame_audio_slot();
    if (!p) {
        acgame_platform_audio_open(0, ACGAME_AUDIO_ANY);
        p = *acgame_audio_slot();
    }
    return p == AUDIO_FAILED ? NULL : (acgame_audio *)p;
}

int acgame_platform_audio_s16stereo(const int16_t *samples, unsigned frames,
                                    unsigned sample_rate)
{
    acgame_audio *a = audio_get();
    if (!a) return -1;
    acgame_mixer_push(&a->mix, samples, frames, sample_rate);
    return 0;   /* frames past a full queue are dropped, as a late sound card would */
}

int acgame_platform_audio_queued(void)
{
    acgame_audio *a = audio_now();
    return a ? (int)acgame_mixer_queued(&a->mix) : -1;
}

int acgame_platform_voice_play(int voice, const void *data, unsigned frames, unsigned format,
                               unsigned rate, int volume, int pan)
{
    acgame_audio *a = audio_get();
    return a ? acgame_mixer_voice_play(&a->mix, voice, data, frames, format, rate, volume, pan) : -1;
}

int acgame_platform_voice_set(int voice, int volume, int pan, unsigned rate)
{
    acgame_audio *a = audio_now();
    return a ? acgame_mixer_voice_set(&a->mix, voice, volume, pan, rate) : -1;
}

void acgame_platform_voice_stop(int voice)
{
    acgame_audio *a = audio_now();
    if (a) acgame_mixer_voice_stop(&a->mix, voice);
}

int acgame_platform_voice_position(int voice)
{
    acgame_audio *a = audio_now();
    return a ? acgame_mixer_voice_position(&a->mix, voice) : -1;
}

#else   /* the host: no sound device */

int acgame_platform_audio_open(unsigned rate, unsigned flags)
{
    (void)rate;
    (void)flags;
    return -1;
}
void acgame_platform_audio_close(void) {}
int acgame_platform_audio_s16stereo(const int16_t *samples, unsigned frames,
                                    unsigned sample_rate)
{
    (void)samples;
    (void)frames;
    (void)sample_rate;
    return -1;
}
int acgame_platform_audio_queued(void) { return -1; }
int acgame_platform_voice_play(int voice, const void *data, unsigned frames, unsigned format,
                               unsigned rate, int volume, int pan)
{
    (void)voice; (void)data; (void)frames; (void)format; (void)rate; (void)volume; (void)pan;
    return -1;
}
int acgame_platform_voice_set(int voice, int volume, int pan, unsigned rate)
{
    (void)voice; (void)volume; (void)pan; (void)rate;
    return -1;
}
void acgame_platform_voice_stop(int voice) { (void)voice; }
int acgame_platform_voice_position(int voice)
{
    (void)voice;
    return -1;
}

#endif
