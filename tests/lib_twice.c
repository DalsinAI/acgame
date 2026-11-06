/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * ACGameLibTwice [LIBRARY]: acgame.library's openers each have a state of
 * their own. Opens the library twice (two games), opens a screen through
 * the first, and checks that the second still has none; then the other way
 * round, a frame and a poll on each, and the first closed without
 * acgame_platform_shutdown (the library's close takes its screen back).
 * Prints PASS or the first FAIL. LIBRARY: the file to open (default
 * PROGDIR:Libs/acgame.library, then acgame.library).
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

#include <libraries/acgame.h>
#define ACGAME_BASE_NAME base
#include <inline/acgame.h>

static int fails;

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); fails++; } } while (0)

static struct Library *open_lib(const char *name)
{
    struct Library *b = NULL;
    if (name) return OpenLibrary((CONST_STRPTR)name, ACGAME_LIB_VERSION);
    b = OpenLibrary((CONST_STRPTR)"PROGDIR:Libs/" ACGAME_LIB_NAME, ACGAME_LIB_VERSION);
    return b ? b : OpenLibrary((CONST_STRPTR)ACGAME_LIB_NAME, ACGAME_LIB_VERSION);
}

static void frame_and_poll(struct Library *base, const char *who)
{
    acgame_indexed_surface *f = ACG_BeginIndexedFrame();
    acgame_input_state in;
    char what[64];
    snprintf(what, sizeof what, "%s: a frame", who);
    CHECK(f != NULL, what);
    if (f) {
        unsigned y;
        for (y = 0; y < f->height; y++) memset(f->pixels + y * f->pitch, (int)y, f->width);
        snprintf(what, sizeof what, "%s: present", who);
        CHECK(ACG_PresentIndexed(f) == 0, what);
    }
    ACG_Poll(&in);
}

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : NULL;
    acgame_video_config config = {320, 200, 50, ACGAME_VIDEO_INDEXED8, ACGAME_DISPLAY_AUTO};
    acgame_platform_info info;
    struct Library *a = open_lib(name), *b = open_lib(name), *base;
    ULONG t0, t1;

    if (!a || !b) {
        printf("FAIL: could not open acgame.library %d twice\n", ACGAME_LIB_VERSION);
        return 20;
    }
    printf("acgame.library %u.%u, openers' bases %p and %p\n",
           (unsigned)a->lib_Version, (unsigned)a->lib_Revision, (void *)a, (void *)b);
    CHECK(a != b, "two opens give two bases");

    base = a;
    t0 = ACG_TicksMS();
    CHECK(ACG_PlatformInit(&config) == 0, "first: screen opened");
    CHECK(ACG_GetInfo(&info) == 0, "first: has a screen");
    base = b;
    CHECK(ACG_GetInfo(&info) != 0, "second: no screen while the first has one");
    CHECK(ACG_BeginIndexedFrame() == NULL, "second: no frame while the first has one");
    CHECK(ACG_DisplayIsPlanar() == -1, "second: no display");

    CHECK(ACG_PlatformInit(&config) == 0, "second: screen opened as well");
    CHECK(ACG_GetInfo(&info) == 0, "second: has a screen");
    frame_and_poll(b, "second");
    base = a;
    frame_and_poll(a, "first");
    base = b;
    ACG_PlatformShutdown();
    CHECK(ACG_GetInfo(&info) != 0, "second: shut down");
    base = a;
    CHECK(ACG_GetInfo(&info) == 0, "first: still has its screen after the second shut down");
    t1 = ACG_TicksMS();
    CHECK(t1 >= t0, "ticks go forward");

    CloseLibrary(a);                        /* without ACG_PlatformShutdown: the close takes it back */
    CloseLibrary(b);

    if (fails) {
        printf("%d FAILED\n", fails);
        return 10;
    }
    printf("PASS (%lu ms)\n", (unsigned long)(t1 - t0));
    return 0;
}
