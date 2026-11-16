/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * Which Allegro library a game gets from ACG_OpenAllegro(version):
 * gfopenallegro's allegro4.library (the Allegro 4 API) or allegro5.library
 * (the Allegro 5 API), both drawing through acgame.library.
 */
#include "acgame/acgame.h"

const char *acgame_allegro_lib_name(int version)
{
    switch (version) {
    case 4: return "allegro4.library";
    case 5: return "allegro5.library";
    default: return 0;
    }
}
