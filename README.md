# ACGame

ACGame is the common game layer for AmiCradle's game ports on an AGA or RTG
Amiga with a 68040: c2p, HAM8, AGA and RTG 8-bit screens, keyboard, mouse and
pads (GFOpenInput, with lowlevel.library as the fallback).

The first rule is architectural: Fast RAM is the workspace; Chip RAM is the
presentation boundary. A large Z3 allocation does not make AGA DMA read Fast RAM.

## acgame.library

`build-amiga.sh` (the os32-gcc16 stove; `STOVE=` and `OUT=`) makes:

- `acgame.library` 1.0: the whole layer as a shared library, bare (no startup
  code, no C library), 68040 soft float. Its calls are in
  `library/acgame_lib.sfd`; `include/proto/acgame.h` calls them directly.
- `libacgame.a`: the stubs a game links. They carry acgame.h's and
  platform.h's calls unchanged, so a port changes nothing but the library it
  links. The stubs open `PROGDIR:Libs/acgame.library` first, then the
  system's, at least the version they were built with.
- `tests/ACGameLibSmoke` (the smoke test, linked as a game links) and
  `tests/ACGameLibTwice` (two openers at once, each with its own screen).

Every opener gets a library base of its own holding its game's state (its
screen, window, frame, keys and pads), so two games can run at once. The
library's entry points put that state in a4 for the call
(`library/acgame_state.h`); the backend's source is the same for the library
and the static build.

**Every game ships the acgame.library it was built against**, in its own
`Libs/` drawer next to the game. A new version only adds calls at the end, so
a game also runs on any later one.

## What is in it

- `acgame_c2p8_ref` and `acgame_c2p8_fast`: 8-bit chunky to eight AGA-style
  bitplanes. The reference is simple and correct; the fast one gives the same
  bytes through an 8x8 bit transpose in 32-bit registers and is what the
  backend uses.
- `acgame_ham8_encode_row`: greedy RGB-to-HAM8 scanline encoder with a
  64-colour base palette, plus a matching decoder for tests/offline tooling.
- The Amiga backend (`src/platform_aros_aga.c`, AmigaOS 3.x and AROS m68k):
  AGA or RTG 8-bit screens, the OpenGPU present on RTG, keyboard, mouse, and
  pads through GFOpenInput with lowlevel.library as the fallback. See
  `AROS_AGA.md`.

Gameplay should normally use indexed 256-colour AGA. HAM8 is intended for
title art, portraits, briefings, loading screens and other static or slowly
changing presentation where horizontal dependency is acceptable.

## Other builds

- `Makefile.amigaos`: the static `libacgame.a` with the backend linked in, and
  the smoke programs (AmigaOS 3.x).
- AROS m68k links the static build; acgame.library is AmigaOS 3.x only.
- `tests/run_tests.sh`: the host tests of c2p and HAM8.

## Still to come

- Paula-first audio with an optional AHI backend;
- timer and file wrappers suitable for AmigaOS 3.x and AROS 68k;
- fixed 320x200, 320x240 and 320x256 indexed AGA surfaces and HAM8 display setup.

Copyright (c) 2026 AmiCradle Team. MIT licence (LICENSE).
