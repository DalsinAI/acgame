/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * ACGame's sound mixer (acgame.library 1.3): one stream and 16 voices mixed
 * into 16-bit stereo at the output's rate, which AHI (or Paula, when AHI is
 * not there) plays. The mixer is plain C with no system calls, so the host
 * tests run it; src/audio_amiga.c gives it a task and a device.
 *
 * The stream: a game pushes interleaved signed 16-bit stereo frames at its
 * own rate (acgame_platform_audio_s16stereo) and they play in order.
 * Voices: a game starts a sample (8 or 16 bit, mono or stereo, signed or
 * unsigned, looped or not) on a voice with a volume, pan and rate, and can
 * change those while it plays. The sample's memory stays the game's and
 * must stay valid until the voice is stopped or has finished.
 */
#ifndef ACGAME_AUDIO_H
#define ACGAME_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A voice's sample format and play flags (ACG_VoicePlay's format). */
#define ACGAME_SND_8BIT     0x0u
#define ACGAME_SND_16BIT    0x1u
#define ACGAME_SND_MONO     0x0u
#define ACGAME_SND_STEREO   0x2u
#define ACGAME_SND_SIGNED   0x0u
#define ACGAME_SND_UNSIGNED 0x4u   /* Allegro 4's samples are unsigned */
#define ACGAME_SND_LOOP     0x8u

/* acgame_platform_audio_open's flags */
#define ACGAME_AUDIO_ANY    0x0u   /* AHI, else Paula */
#define ACGAME_AUDIO_AHI    0x1u   /* AHI only */
#define ACGAME_AUDIO_PAULA  0x2u   /* Paula (audio.device) only */

#define ACGAME_AUDIO_VOICES        16
#define ACGAME_AUDIO_STREAM_FRAMES 8192u   /* a power of two */
#define ACGAME_AUDIO_MIX_MAX       2048u   /* frames per mix call at most */
#define ACGAME_AUDIO_VOLUME_MAX    255
#define ACGAME_AUDIO_PAN_CENTRE    128

typedef struct {
    const void *data;
    uint32_t frames;
    uint32_t format;
    uint32_t rate;
    uint32_t pos;           /* the frame playing */
    uint32_t frac;          /* and the 16-bit fraction past it */
    uint32_t step;          /* frames per output frame, 16.16 */
    int32_t gain_l, gain_r; /* 0..256 */
    int active;
} acgame_voice;

typedef struct acgame_mixer {
    uint32_t out_rate;
    /* Taken around every change to the voices and around a mix (the game's
     * task changes them, the audio task mixes). NULL: no lock. */
    void (*lock)(void *data);
    void (*unlock)(void *data);
    void *lock_data;
    acgame_voice voice[ACGAME_AUDIO_VOICES];
    /* The stream: one writer (the game), one reader (the mix). head and
     * tail count frames for ever; the ring holds head - tail of them. */
    volatile uint32_t head, tail;
    volatile uint32_t stream_rate;
    uint32_t stream_frac;
    int16_t stream[ACGAME_AUDIO_STREAM_FRAMES * 2];
    int32_t acc[ACGAME_AUDIO_MIX_MAX * 2];
} acgame_mixer;

void acgame_mixer_init(acgame_mixer *m, unsigned out_rate);
/* Frames taken (all, or as many as fit). A new rate applies to frames still
 * queued too. */
unsigned acgame_mixer_push(acgame_mixer *m, const int16_t *samples, unsigned frames,
                           unsigned rate);
/* Stream frames queued and not yet mixed. */
unsigned acgame_mixer_queued(const acgame_mixer *m);
/* voice -1: the first free one. Volume 0-255, pan 0 (left) to 255 (right),
 * 128 the centre at full volume on both sides. The voice used, or -1. */
int acgame_mixer_voice_play(acgame_mixer *m, int voice, const void *data, unsigned frames,
                            unsigned format, unsigned rate, int volume, int pan);
/* -1 (or rate 0) keeps a value. 0, or -1 for a voice out of range. */
int acgame_mixer_voice_set(acgame_mixer *m, int voice, int volume, int pan, unsigned rate);
void acgame_mixer_voice_stop(acgame_mixer *m, int voice);
/* The frame a voice is playing, or -1 when it is not playing. */
int acgame_mixer_voice_position(acgame_mixer *m, int voice);
/* frames (at most ACGAME_AUDIO_MIX_MAX) of signed 16-bit stereo. */
void acgame_mixer_mix(acgame_mixer *m, int16_t *out, unsigned frames);

#ifdef __cplusplus
}
#endif

#endif
