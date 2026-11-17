#include "acgame/acgame.h"
#include "acgame/audio.h"
#include "acgame/platform.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_c2p_roundtrip(void) {
    enum { W = 16, H = 3, PP = W / 8 };
    uint8_t chunky[W * H];
    uint8_t roundtrip[W * H];
    uint8_t plane_mem[8][PP * H];
    uint8_t *planes[8];
    const uint8_t *cplanes[8];
    unsigned i, p;
    for (i = 0; i < W * H; ++i) chunky[i] = (uint8_t)((i * 37u + 11u) & 255u);
    memset(roundtrip, 0, sizeof roundtrip);
    memset(plane_mem, 0, sizeof plane_mem);
    for (p = 0; p < 8; ++p) { planes[p] = plane_mem[p]; cplanes[p] = plane_mem[p]; }
    assert(acgame_c2p8_ref(chunky, W, planes, PP, W, H) == 0);
    assert(acgame_p2c8_ref(roundtrip, W, cplanes, PP, W, H) == 0);
    assert(memcmp(chunky, roundtrip, sizeof chunky) == 0);
}

static void test_c2p_bit_order(void) {
    uint8_t src[8] = {1,0,0,0,0,0,0,1};
    uint8_t mem[8] = {0};
    uint8_t *planes[8] = {&mem[0],&mem[1],&mem[2],&mem[3],&mem[4],&mem[5],&mem[6],&mem[7]};
    assert(acgame_c2p8_ref(src, 8, planes, 1, 8, 1) == 0);
    assert(mem[0] == 0x81);
    for (unsigned p = 1; p < 8; ++p) assert(mem[p] == 0);
}

/* The fast converter gives the reference's planes, byte for byte, over every
 * pixel value in every position and a pitch wider than the frame. */
static void test_c2p_fast_matches_ref(void) {
    enum { W = 64, H = 9, CP = W + 5, PP = W / 8 + 3 };
    uint8_t chunky[CP * H];
    uint8_t ref_mem[8][PP * H];
    uint8_t fast_mem[8][PP * H];
    uint8_t *ref[8];
    uint8_t *fast[8];
    uint32_t seed = 12345u;
    unsigned i, p;
    for (i = 0; i < sizeof chunky; ++i) {
        seed = seed * 1103515245u + 12345u;
        chunky[i] = (uint8_t)(i < 256 ? i : (seed >> 16));
    }
    memset(ref_mem, 0x5a, sizeof ref_mem);
    memset(fast_mem, 0x5a, sizeof fast_mem);
    for (p = 0; p < 8; ++p) { ref[p] = ref_mem[p]; fast[p] = fast_mem[p]; }
    assert(acgame_c2p8_ref(chunky, CP, ref, PP, W, H) == 0);
    assert(acgame_c2p8_fast(chunky, CP, fast, PP, W, H) == 0);
    assert(memcmp(ref_mem, fast_mem, sizeof ref_mem) == 0);
    assert(acgame_c2p8_fast(chunky, CP, fast, PP, 12, H) == -1);
}

static void test_ham8_controls(void) {
    acgame_rgb8 pal[64] = {{0}};
    acgame_rgb8 src[4] = {
        {0,0,0},
        {252,0,0},
        {252,0,252},
        {252,252,252}
    };
    acgame_rgb8 out[4];
    uint8_t codes[4];
    assert(acgame_ham8_encode_row(src, 4, pal, codes, out) == 0);
    assert(codes[0] == 0x00);
    assert(codes[1] == 0xbf);
    assert(codes[2] == 0x7f);
    assert(codes[3] == 0xff);
    for (unsigned i = 0; i < 4; ++i) {
        assert(out[i].r == src[i].r && out[i].g == src[i].g && out[i].b == src[i].b);
    }
}

static void test_ham8_palette_reset(void) {
    acgame_rgb8 pal[64] = {{0}};
    acgame_rgb8 src[2];
    acgame_rgb8 out[2];
    uint8_t codes[2];
    pal[5] = (acgame_rgb8){17, 34, 51};
    src[0] = pal[5];
    src[1] = pal[5];
    assert(acgame_ham8_encode_row(src, 2, pal, codes, out) == 0);
    assert(codes[0] == 5);
    assert(out[0].r == 17 && out[0].g == 34 && out[0].b == 51);
}

static void test_allegro_lib_name(void) {
    assert(strcmp(acgame_allegro_lib_name(4), "allegro4.library") == 0);
    assert(strcmp(acgame_allegro_lib_name(5), "allegro5.library") == 0);
    assert(acgame_allegro_lib_name(0) == NULL);
    assert(acgame_allegro_lib_name(3) == NULL);
    assert(acgame_allegro_lib_name(6) == NULL);
    assert(acgame_allegro_lib_name(-4) == NULL);
}

/* ---- the sound mixer ------------------------------------------------------ */

static acgame_mixer g_mix;
static int g_locks;
static void t_lock(void *d) { (void)d; g_locks++; }
static void t_unlock(void *d) { (void)d; g_locks--; assert(g_locks == 0); }

static void test_mixer_stream_passthrough(void) {
    int16_t in[8] = {100, -100, 200, -200, 300, -300, 32767, -32768};
    int16_t out[12];
    acgame_mixer_init(&g_mix, 22050);
    assert(acgame_mixer_push(&g_mix, in, 4, 22050) == 4);
    assert(acgame_mixer_queued(&g_mix) == 4);
    acgame_mixer_mix(&g_mix, out, 6);
    assert(memcmp(out, in, sizeof(in)) == 0);
    assert(out[8] == 0 && out[9] == 0 && out[10] == 0 && out[11] == 0);  /* silence past the end */
    assert(acgame_mixer_queued(&g_mix) == 0);
}

static void test_mixer_stream_resample(void) {
    int16_t in[8] = {1, 1, 2, 2, 3, 3, 4, 4};
    int16_t out[16];
    unsigned i;
    /* 11025 into 22050: every frame twice */
    acgame_mixer_init(&g_mix, 22050);
    acgame_mixer_push(&g_mix, in, 4, 11025);
    acgame_mixer_mix(&g_mix, out, 8);
    for (i = 0; i < 8; i++) assert(out[i * 2] == (int16_t)(i / 2 + 1) && out[i * 2 + 1] == out[i * 2]);
    /* 44100 into 22050: every other frame */
    acgame_mixer_init(&g_mix, 22050);
    acgame_mixer_push(&g_mix, in, 4, 44100);
    acgame_mixer_mix(&g_mix, out, 2);
    assert(out[0] == 1 && out[2] == 3);
    assert(acgame_mixer_queued(&g_mix) == 0);
}

static void test_mixer_stream_full(void) {
    static int16_t big[(ACGAME_AUDIO_STREAM_FRAMES + 10) * 2];
    int16_t out[4];
    acgame_mixer_init(&g_mix, 22050);
    assert(acgame_mixer_push(&g_mix, big, ACGAME_AUDIO_STREAM_FRAMES + 10, 22050) == ACGAME_AUDIO_STREAM_FRAMES);
    assert(acgame_mixer_push(&g_mix, big, 1, 22050) == 0);
    acgame_mixer_mix(&g_mix, out, 2);
    assert(acgame_mixer_push(&g_mix, big, 5, 22050) == 2);   /* room for what was played */
    assert(acgame_mixer_push(&g_mix, NULL, 5, 0) == 0);
}

static void test_mixer_voice_formats(void) {
    static const uint8_t u8[2] = {128 + 64, 128 - 64};
    static const int8_t s8[2] = {64, -64};
    static const uint16_t u16[2] = {32768 + 1000, 32768 - 1000};
    static const int16_t s16st[4] = {1000, -2000, 3000, -4000};
    int16_t out[8];
    acgame_mixer_init(&g_mix, 22050);
    g_mix.lock = t_lock;
    g_mix.unlock = t_unlock;
    assert(acgame_mixer_voice_play(&g_mix, -1, u8, 2, ACGAME_SND_8BIT | ACGAME_SND_UNSIGNED, 22050, 255, 128) == 0);
    acgame_mixer_mix(&g_mix, out, 3);
    assert(out[0] == 64 * 256 && out[1] == 64 * 256 && out[2] == -64 * 256 && out[3] == -64 * 256);
    assert(out[4] == 0 && out[5] == 0);            /* not looped: it ended */
    assert(acgame_mixer_voice_position(&g_mix, 0) == -1);
    assert(acgame_mixer_voice_play(&g_mix, 3, s8, 2, ACGAME_SND_8BIT, 22050, 255, 128) == 3);
    acgame_mixer_mix(&g_mix, out, 2);
    assert(out[0] == 64 * 256 && out[2] == -64 * 256);
    acgame_mixer_voice_play(&g_mix, 0, u16, 2, ACGAME_SND_16BIT | ACGAME_SND_UNSIGNED, 22050, 255, 128);
    acgame_mixer_mix(&g_mix, out, 2);
    assert(out[0] == 1000 && out[1] == 1000 && out[2] == -1000);
    acgame_mixer_voice_play(&g_mix, 0, s16st, 2, ACGAME_SND_16BIT | ACGAME_SND_STEREO, 22050, 255, 128);
    acgame_mixer_mix(&g_mix, out, 2);
    assert(out[0] == 1000 && out[1] == -2000 && out[2] == 3000 && out[3] == -4000);
    assert(g_locks == 0);
}

static void test_mixer_voice_loop_pan_volume(void) {
    static const int16_t s[3] = {1000, 2000, 3000};
    int16_t out[16];
    int v;
    acgame_mixer_init(&g_mix, 22050);
    v = acgame_mixer_voice_play(&g_mix, -1, s, 3, ACGAME_SND_16BIT | ACGAME_SND_LOOP, 22050, 255, 0);
    acgame_mixer_mix(&g_mix, out, 8);
    assert(out[0] == 1000 && out[1] == 0);        /* pan 0: left only */
    assert(out[6] == 1000 && out[8] == 2000 && out[14] == 2000);   /* looped */
    assert(acgame_mixer_voice_position(&g_mix, v) == 2);
    acgame_mixer_voice_set(&g_mix, v, -1, 255, 0); /* right only, same volume */
    acgame_mixer_mix(&g_mix, out, 1);
    assert(out[0] == 0 && out[1] == 3000);
    acgame_mixer_voice_set(&g_mix, v, 0, -1, 0);   /* silent */
    acgame_mixer_mix(&g_mix, out, 1);
    assert(out[0] == 0 && out[1] == 0);
    acgame_mixer_voice_set(&g_mix, v, 128, 128, 0);  /* about half, centre */
    acgame_mixer_mix(&g_mix, out, 1);
    assert(out[0] == (2000 * 129) >> 8 && out[1] == out[0]);   /* frame 1 at gain 129/256 */
    acgame_mixer_voice_set(&g_mix, v, 255, 128, 44100);  /* twice the rate: every other frame */
    acgame_mixer_mix(&g_mix, out, 2);
    assert(out[0] == 3000 && out[2] == 2000);       /* frames 2, then 4 (looped: 1) */
    acgame_mixer_voice_stop(&g_mix, v);
    assert(acgame_mixer_voice_position(&g_mix, v) == -1);
}

static void test_mixer_voices_and_clamp(void) {
    static const int16_t loud[1] = {30000};
    int16_t in[2] = {10000, -10000};
    int16_t out[2];
    int i;
    acgame_mixer_init(&g_mix, 22050);
    for (i = 0; i < ACGAME_AUDIO_VOICES; i++)
        assert(acgame_mixer_voice_play(&g_mix, -1, loud, 1, ACGAME_SND_16BIT | ACGAME_SND_LOOP, 22050, 255, 128) == i);
    assert(acgame_mixer_voice_play(&g_mix, -1, loud, 1, ACGAME_SND_16BIT, 22050, 255, 128) == -1);  /* all busy */
    acgame_mixer_push(&g_mix, in, 1, 22050);
    acgame_mixer_mix(&g_mix, out, 1);
    assert(out[0] == 32767 && out[1] == 32767);     /* clamped, not wrapped */
    assert(acgame_mixer_voice_play(&g_mix, 16, loud, 1, 0, 22050, 255, 128) == -1);
    assert(acgame_mixer_voice_play(&g_mix, 0, NULL, 1, 0, 22050, 255, 128) == -1);
    assert(acgame_mixer_voice_play(&g_mix, 0, loud, 1, 0, 0, 255, 128) == -1);
    assert(acgame_mixer_voice_set(&g_mix, -1, 1, 1, 0) == -1);
    assert(acgame_mixer_voice_position(&g_mix, 99) == -1);
}

static void test_host_has_no_sound_device(void) {
    int16_t in[2] = {0, 0};
    assert(acgame_platform_audio_open(22050, 0) == -1);
    assert(acgame_platform_audio_s16stereo(in, 1, 22050) == -1);
    assert(acgame_platform_audio_queued() == -1);
    assert(acgame_platform_voice_play(-1, in, 1, 0, 22050, 255, 128) == -1);
    acgame_platform_audio_close();
}

int main(void) {
    test_c2p_roundtrip();
    test_c2p_bit_order();
    test_c2p_fast_matches_ref();
    test_ham8_controls();
    test_ham8_palette_reset();
    test_allegro_lib_name();
    test_mixer_stream_passthrough();
    test_mixer_stream_resample();
    test_mixer_stream_full();
    test_mixer_voice_formats();
    test_mixer_voice_loop_pan_volume();
    test_mixer_voices_and_clamp();
    test_host_has_no_sound_device();
    puts("acgame reference tests: OK");
    return 0;
}
