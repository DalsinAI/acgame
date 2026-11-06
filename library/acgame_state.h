/* Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
 * SPDX-License-Identifier: MIT
 *
 * acgame.library: where an opener's state is while the library runs.
 *
 * Every OpenLibrary of acgame.library gets a library base of its own, and
 * that base holds the opener's state (struct acgame_state, defined in
 * src/platform_aros_aga.c: its screen, window, frame, keys, pads and the
 * libraries it opened). Each entry point (acgame_lib.c) keeps the caller's
 * a4, puts the state in a4, calls the backend, and gives the caller's a4
 * back. The backend's code reaches its state through acg_state, so the
 * backend's source is the same for libacgame.a and the library.
 *
 * Every file of the library is built with -ffixed-a4, so GCC never uses a4
 * for anything else. The Amiga's libraries keep a4 across calls, so the
 * state stays put while the backend calls Intuition, graphics and the rest.
 */
#ifndef ACGAME_LIBRARY_STATE_H
#define ACGAME_LIBRARY_STATE_H

struct acgame_state;
register struct acgame_state *acg_state __asm__("a4");

/* in src/platform_aros_aga.c */
extern const unsigned long acgame_state_size;
void acgame_state_cleanup(void);

#endif
