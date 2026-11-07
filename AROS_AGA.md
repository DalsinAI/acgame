# ACGame Amiga backend (AmigaOS 3.x and AROS m68k)

One backend, `src/platform_aros_aga.c`, builds for AmigaOS 3.x with the
`os32-gcc16` stove (GCC 16.2 with the Team's fixes, libnix, the GFOpenGPU SDK
0.7: `make -f Makefile.amigaos STOVE=...`) and for AROS m68k with the AROS
toolchain. It needs only the operating system, so it runs on a real Amiga with
no GFOpenGPU and no GFOpenInput; on AmiCradle the same program hands its work to
the cores and the GPU where the system offers it.

## Display

- Intuition opens an 8-bit custom screen. `acgame_video_config.display` picks
  the kind: `AUTO` (the system's best 8-bit mode, an RTG one where one exists),
  `AGA` (the native chipset's default mode) or `RTG`. The variable
  `ENV:ACGame/Display` (`AGA`, `RTG` or `AUTO`) overrides what the game asked for.
- The game renders an 8-bit chunky frame in normal (Fast) memory.
- On a planar 8-plane screen (native AGA) `acgame_c2p8_fast` converts the frame
  into the screen's planes: an 8x8 bit transpose in 32-bit registers, about 30
  instructions for eight pixels where the reference converter takes 64 tests.
  With gfopengpu.library 0.29 or later (GFOpenGfx in) the frame goes through
  `WriteChunkyPixels` instead: GFOpenGfx's core does the one c2p for every
  game and the browser, 32 pixels at a time a long to each plane, and its
  provider chain can hand it to faster hardware. ACGame's own C2P is the
  fallback without it, and `ENV:ACGame/C2P` set to `OWN` keeps it anyway, to
  compare the two.
- On any other 8-bit screen (GFOpenRTG, or another RTG system) the frame goes
  through graphics.library's `WriteChunkyPixels` (V40, OS 3.1 and later).
  GFOpenGfx (gfopengpu.library 0.8 and later) owns that call, so on AmiCradle the
  copy into the board is done by the cores; on a real Amiga with Picasso96 it is
  the RTG system's own code. `acgame_platform_get_info()` says which.
- `LoadRGB32` updates the 256-colour palette only when it changes.
- On a planar screen frames are buffered through Intuition's ScreenBuffers
  (three Chip RAM bitmaps, or two when Chip RAM is short): C2P writes one that
  is neither shown nor still scanned out, and `ChangeScreenBuffer` shows it
  with its palette at the next vertical blank, so no half-converted frame is
  seen. The third buffer keeps the full frame rate while the display's
  safe-to-write reply for the previous buffer is still on its way. Without
  Chip RAM for a second buffer, and on RTG screens, `WaitTOF` is the
  presentation boundary.

## Input

- A borderless backdrop window supplies keyboard, mouse buttons and relative
  mouse movement.
- Pads come through **GFOpenInput** (gfopeninput.library 1.2 or later) when it is
  there: hot-plug, analogue sticks and triggers, every pad in SDL 2's
  GameController layout. ACGame takes one pad, a plugged-in one (USB, or a pad
  on AmiCradle's cores) before the Amiga's own joystick port, and opens it
  with `OIT_Exclusive` while the game runs, so the pad stops driving its Amiga
  port for older programs until the game closes. When the pad goes, the next
  one that comes is taken.
- When gfopeninput.library is missing, or is the 1.1 skeleton, or
  `ENV:ACGame/GFOpenInput` is `0`, a joystick or CD32 pad in port 2 comes through
  lowlevel.library's `ReadJoyPort`, as before.
- `acgame_input_state.pad` has the pad in full (buttons as
  `ACGAME_PAD_BIT(ACGAME_PAD_A)` and so on, the six axes, where it came from
  and its name). The d-pad or left stick, A, B, X and Start are also ORed into
  the keyboard's `UP`, `DOWN`, `LEFT`, `RIGHT`, `FIRE1`, `FIRE2`, `FIRE3` and
  `START`, so a game that reads only `key[]` gets the pad too.
- GFOpenInput's frozen header is copied unchanged into
  `include/acgame/gfopeninput/` (from gfopeninput `include/`), so ACGame
  builds with nothing else installed. The library bases have ACGame's own
  names (`acgame_LowLevelBase`, `acgame_OpenInputBase`, `acgame_OpenGPUBase`),
  so a game that also links SDL 2 has no clash.

## Threads

ACGame has no threads of its own. Everything it does runs on the game's task,
so there is nothing to move to GFOpenMulticore under the Team's Threading Rule.

## Programs

- `acgame-aga-smoke`, `acgame-rtg-smoke`, `acgame-smoke` (`examples/aga_smoke.c`,
  AGA, RTG or the system's pick): `[AGA|RTG|AUTO] [FRAMES]`. Animates a 320x200
  checker, says which screen and which path it got, and how many frames a second.
- `acgame-joy` (`examples/joy_test.c`): `[AGA|RTG|AUTO] [FRAMES]`. A lamp for
  each standard button, the two sticks and the two triggers; the top strip is
  green for GFOpenInput, yellow for lowlevel.library, red for no pad. Changes are
  printed too.

Escape quits each of them.

## Building

```sh
make -f Makefile.amigaos STOVE=~/AmiCradle/stoves/os32-gcc16/prefix
sh tests/run_tests.sh          # the converters and HAM8 encoder on the host
```

The build is `-Wall -Wextra -Werror -fno-delete-null-pointer-checks` and must
stay clean; it also builds with the older `os32` (GCC 6.5) stove. AROS builds
the same three source files with `-I include` and nothing else (the game ports'
`scripts/build_game_ports_job.py`).

## Current limitations

- PCM audio and HAM8 presentation are still stubs.
