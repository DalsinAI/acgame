/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * ACGame's sound mixer (include/acgame/audio.h). No system calls and no C
 * library: it is built into acgame.library bare, and into the host tests.
 * Resampling is nearest-frame (16.16 steps), mixing is 32-bit with a clamp
 * to 16 bits; both are cheap on a 68040.
 */
#include "acgame/audio.h"

#define LOCK(m)   do { if ((m)->lock) (m)->lock((m)->lock_data); } while (0)
#define UNLOCK(m) do { if ((m)->unlock) (m)->unlock((m)->lock_data); } while (0)

/* rate / out_rate as 16.16, without a 64-bit division */
static uint32_t step_for(uint32_t rate, uint32_t out_rate)
{
    if (!out_rate) return 0;
    return ((rate / out_rate) << 16) + (((rate % out_rate) << 16) / out_rate);
}

void acgame_mixer_init(acgame_mixer *m, unsigned out_rate)
{
    unsigned i;
    unsigned char *p = (unsigned char *)m;
    void (*lock)(void *) = m->lock;
    void (*unlock)(void *) = m->unlock;
    void *lock_data = m->lock_data;
    /* the voices and counters only: the ring and the accumulator need no clearing */
    for (i = 0; i < (unsigned)((const unsigned char *)&m->stream - p); i++) p[i] = 0;
    m->lock = lock;
    m->unlock = unlock;
    m->lock_data = lock_data;
    m->out_rate = out_rate;
    m->stream_rate = out_rate;
}

unsigned acgame_mixer_push(acgame_mixer *m, const int16_t *samples, unsigned frames,
                           unsigned rate)
{
    uint32_t head = m->head;
    uint32_t room = ACGAME_AUDIO_STREAM_FRAMES - (head - m->tail);
    unsigned i;
    if (rate) m->stream_rate = rate;
    if (!samples) return 0;
    if (frames > room) frames = room;
    for (i = 0; i < frames; i++) {
        uint32_t at = ((head + i) & (ACGAME_AUDIO_STREAM_FRAMES - 1)) * 2;
        m->stream[at] = samples[i * 2];
        m->stream[at + 1] = samples[i * 2 + 1];
    }
    m->head = head + frames;   /* after the frames: the mix reads only what is there */
    return frames;
}

unsigned acgame_mixer_queued(const acgame_mixer *m)
{
    return m->head - m->tail;
}

static void set_gain(acgame_voice *v, int volume, int pan)
{
    int32_t vol, l, r;
    if (volume < 0) volume = 0;
    if (volume > ACGAME_AUDIO_VOLUME_MAX) volume = ACGAME_AUDIO_VOLUME_MAX;
    if (pan < 0) pan = 0;
    if (pan > 255) pan = 255;
    vol = volume + (volume >> 7);          /* 0..256 */
    l = pan <= ACGAME_AUDIO_PAN_CENTRE ? 256 : (255 - pan) * 2;
    r = pan >= ACGAME_AUDIO_PAN_CENTRE ? 256 : pan * 2;
    v->gain_l = (vol * l) >> 8;
    v->gain_r = (vol * r) >> 8;
}

int acgame_mixer_voice_play(acgame_mixer *m, int voice, const void *data, unsigned frames,
                            unsigned format, unsigned rate, int volume, int pan)
{
    acgame_voice *v;
    if (!data || !frames || !rate || voice >= ACGAME_AUDIO_VOICES || voice < -1) return -1;
    LOCK(m);
    if (voice < 0) {
        for (voice = 0; voice < ACGAME_AUDIO_VOICES && m->voice[voice].active; voice++) {}
        if (voice == ACGAME_AUDIO_VOICES) {
            UNLOCK(m);
            return -1;
        }
    }
    v = &m->voice[voice];
    v->data = data;
    v->frames = frames;
    v->format = format;
    v->rate = rate;
    v->pos = 0;
    v->frac = 0;
    v->step = step_for(rate, m->out_rate);
    set_gain(v, volume, pan);
    v->active = 1;
    UNLOCK(m);
    return voice;
}

int acgame_mixer_voice_set(acgame_mixer *m, int voice, int volume, int pan, unsigned rate)
{
    acgame_voice *v;
    int32_t old_vol, old_pan;
    if (voice < 0 || voice >= ACGAME_AUDIO_VOICES) return -1;
    v = &m->voice[voice];
    LOCK(m);
    /* the volume and pan back from the two gains: the louder side is the volume */
    old_vol = v->gain_l > v->gain_r ? v->gain_l : v->gain_r;
    if (old_vol >= 256) old_vol = 255;
    if (!old_vol) old_pan = ACGAME_AUDIO_PAN_CENTRE;
    else if (v->gain_l > v->gain_r) old_pan = (v->gain_r * 128) / v->gain_l;
    else if (v->gain_r > v->gain_l) old_pan = 255 - (v->gain_l * 127) / v->gain_r;
    else old_pan = ACGAME_AUDIO_PAN_CENTRE;
    if (volume >= 0 || pan >= 0)
        set_gain(v, volume >= 0 ? volume : old_vol, pan >= 0 ? pan : old_pan);
    if (rate) {
        v->rate = rate;
        v->step = step_for(rate, m->out_rate);
    }
    UNLOCK(m);
    return 0;
}

void acgame_mixer_voice_stop(acgame_mixer *m, int voice)
{
    if (voice < 0 || voice >= ACGAME_AUDIO_VOICES) return;
    LOCK(m);
    m->voice[voice].active = 0;
    UNLOCK(m);
}

int acgame_mixer_voice_position(acgame_mixer *m, int voice)
{
    int r;
    if (voice < 0 || voice >= ACGAME_AUDIO_VOICES) return -1;
    LOCK(m);
    r = m->voice[voice].active ? (int)m->voice[voice].pos : -1;
    UNLOCK(m);
    return r;
}

/* One voice into the accumulator; a voice that ends stops. */
#define MIX_LOOP(FETCH_L, FETCH_R)                                            \
    for (i = 0; i < frames; i++) {                                            \
        acc[i * 2] += ((FETCH_L) * gl) >> 8;                                  \
        acc[i * 2 + 1] += ((FETCH_R) * gr) >> 8;                              \
        frac += step;                                                         \
        pos += frac >> 16;                                                    \
        frac &= 0xffff;                                                       \
        if (pos >= len) {                                                     \
            if (!loop) { v->active = 0; break; }                              \
            pos %= len;                                                       \
        }                                                                     \
    }

static void mix_voice(acgame_voice *v, int32_t *acc, unsigned frames)
{
    const int32_t gl = v->gain_l, gr = v->gain_r;
    const uint32_t len = v->frames, step = v->step;
    const int loop = (v->format & ACGAME_SND_LOOP) != 0;
    uint32_t pos = v->pos, frac = v->frac;
    unsigned i;
    switch (v->format & (ACGAME_SND_16BIT | ACGAME_SND_STEREO | ACGAME_SND_UNSIGNED)) {
    case ACGAME_SND_8BIT | ACGAME_SND_MONO | ACGAME_SND_SIGNED: {
        const int8_t *s = (const int8_t *)v->data;
        MIX_LOOP((int32_t)s[pos] * 256, (int32_t)s[pos] * 256)
        break;
    }
    case ACGAME_SND_8BIT | ACGAME_SND_MONO | ACGAME_SND_UNSIGNED: {
        const uint8_t *s = (const uint8_t *)v->data;
        MIX_LOOP(((int32_t)s[pos] - 128) * 256, ((int32_t)s[pos] - 128) * 256)
        break;
    }
    case ACGAME_SND_8BIT | ACGAME_SND_STEREO | ACGAME_SND_SIGNED: {
        const int8_t *s = (const int8_t *)v->data;
        MIX_LOOP((int32_t)s[pos * 2] * 256, (int32_t)s[pos * 2 + 1] * 256)
        break;
    }
    case ACGAME_SND_8BIT | ACGAME_SND_STEREO | ACGAME_SND_UNSIGNED: {
        const uint8_t *s = (const uint8_t *)v->data;
        MIX_LOOP(((int32_t)s[pos * 2] - 128) * 256, ((int32_t)s[pos * 2 + 1] - 128) * 256)
        break;
    }
    case ACGAME_SND_16BIT | ACGAME_SND_MONO | ACGAME_SND_SIGNED: {
        const int16_t *s = (const int16_t *)v->data;
        MIX_LOOP((int32_t)s[pos], (int32_t)s[pos])
        break;
    }
    case ACGAME_SND_16BIT | ACGAME_SND_MONO | ACGAME_SND_UNSIGNED: {
        const uint16_t *s = (const uint16_t *)v->data;
        MIX_LOOP((int32_t)s[pos] - 32768, (int32_t)s[pos] - 32768)
        break;
    }
    case ACGAME_SND_16BIT | ACGAME_SND_STEREO | ACGAME_SND_SIGNED: {
        const int16_t *s = (const int16_t *)v->data;
        MIX_LOOP((int32_t)s[pos * 2], (int32_t)s[pos * 2 + 1])
        break;
    }
    default: {   /* 16-bit stereo unsigned */
        const uint16_t *s = (const uint16_t *)v->data;
        MIX_LOOP((int32_t)s[pos * 2] - 32768, (int32_t)s[pos * 2 + 1] - 32768)
        break;
    }
    }
    v->pos = pos;
    v->frac = frac;
}

void acgame_mixer_mix(acgame_mixer *m, int16_t *out, unsigned frames)
{
    int32_t *acc = m->acc;
    unsigned i, n;
    if (frames > ACGAME_AUDIO_MIX_MAX) frames = ACGAME_AUDIO_MIX_MAX;
    /* the stream first: it fills the accumulator (silence past its end) */
    {
        uint32_t tail = m->tail, head = m->head;
        uint32_t step = step_for(m->stream_rate, m->out_rate), frac = m->stream_frac;
        for (i = 0; i < frames && tail != head; i++) {
            uint32_t at = (tail & (ACGAME_AUDIO_STREAM_FRAMES - 1)) * 2;
            acc[i * 2] = m->stream[at];
            acc[i * 2 + 1] = m->stream[at + 1];
            frac += step;
            tail += frac >> 16;
            frac &= 0xffff;
            if ((int32_t)(head - tail) < 0) tail = head;   /* a step past the end */
        }
        m->tail = tail;
        m->stream_frac = tail == head ? 0 : frac;
        for (; i < frames; i++) acc[i * 2] = acc[i * 2 + 1] = 0;
    }
    LOCK(m);
    for (n = 0; n < ACGAME_AUDIO_VOICES; n++)
        if (m->voice[n].active) mix_voice(&m->voice[n], acc, frames);
    UNLOCK(m);
    for (i = 0; i < frames * 2; i++) {
        int32_t s = acc[i];
        out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}
