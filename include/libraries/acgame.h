/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * acgame.library: ACGame's game layer as a shared library for AmigaOS 3.x.
 * Its calls are acgame.h's and platform.h's (library/acgame_lib.sfd). A game
 * links libacgame.a's library stubs (stubs/acgame_stubs.c) and keeps calling
 * acgame_platform_init() and the rest as before; the stubs open the library.
 *
 * Every game ships the acgame.library it was built against, in its own
 * Libs/ drawer, and the stubs open that copy first. A library's version only
 * ever adds calls at the end, so a game also runs on any later one.
 *
 * 1.2: ACG_OpenAllegro(version): a game says "I need Allegro 4" (or 5) and
 * gets gfopenallegro's allegro4.library (allegro5.library), from its own
 * Libs/ drawer first, then LIBS:. The base is the game's: it closes it with
 * CloseLibrary() before it closes acgame.library.
 */
#ifndef LIBRARIES_ACGAME_H
#define LIBRARIES_ACGAME_H

#define ACGAME_LIB_NAME     "acgame.library"
#define ACGAME_LIB_VERSION  1
#define ACGAME_LIB_REVISION 2

#include "../acgame/acgame.h"
#include "../acgame/platform.h"

#endif
