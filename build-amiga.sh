#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. ACGame, MIT licence (see LICENSE at the repository root).
# SPDX-License-Identifier: MIT
# acgame.library and its stubs with the os32-gcc16 stove (GCC 16.2, libnix):
#   acgame.library   the library: bare (no startup code, no C library), its
#                    RomTag's stub first; 68040, soft float
#   libacgame.a      the stubs a game links: the same calls as the static
#                    libacgame.a (Makefile.amigaos), each going to the library
#   tests/ACGameLibSmoke, ACGameLibTwice   the AGA/RTG smoke test on the
#                    library, and two openers' states kept apart
#   tests/ACGameStaticSmoke   the smoke test with the backend linked in
#   build-amiga.sh            STOVE=dir (default ~/AmiCradle/stoves/os32-gcc16)  OUT=dir
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
AR=${AR:-"$P/bin/m68k-amigaos-ar"}
NM=${NM:-"$P/bin/m68k-amigaos-nm"}
OUT=${OUT:-"$HERE/build-amiga"}
mkdir -p "$OUT/obj" "$OUT/tests"

# The 68040 soft-float link; -fno-delete-null-pointer-checks: address 0 is real memory.
SOFT="-specs=$HERE/specs/m68k-soft-m68040.specs"
WARN="-Wall -Wextra -Werror"
# The library: a4 holds the opener's state (library/acgame_state.h), so no
# file of it may use a4 for anything else.
LIBCFLAGS="-m68040 $SOFT -O2 -fomit-frame-pointer -fno-delete-null-pointer-checks -ffixed-a4 -DACGAME_LIBRARY $WARN -I$HERE/include"

# shellcheck disable=SC2086
"$CC" $LIBCFLAGS -c -o "$OUT/obj/c2p_ref.o" "$HERE/src/c2p_ref.c"
# shellcheck disable=SC2086
"$CC" $LIBCFLAGS -c -o "$OUT/obj/ham8.o" "$HERE/src/ham8.c"
# shellcheck disable=SC2086
"$CC" $LIBCFLAGS -c -o "$OUT/obj/platform_lib.o" "$HERE/src/platform_aros_aga.c"
# acgame_lib.c first: its start() and RomTag lead the hunk. It carries the
# library's own memcpy, memset and memcmp; -fno-tree-loop-distribute-patterns
# keeps GCC from turning their loops into calls to themselves.
# shellcheck disable=SC2086
"$CC" $LIBCFLAGS -fno-toplevel-reorder -fno-builtin -fno-tree-loop-distribute-patterns \
    -nostartfiles -nostdlib -o "$OUT/acgame.library" \
    "$HERE/library/acgame_lib.c" "$OUT/obj/c2p_ref.o" "$OUT/obj/ham8.o" "$OUT/obj/platform_lib.o" -lgcc
if "$NM" -u "$OUT/acgame.library" | grep .; then
    echo "acgame.library has undefined symbols" >&2; exit 1
fi
echo "$OUT/acgame.library ($(wc -c < "$OUT/acgame.library") bytes)"
sha256sum "$OUT/acgame.library"

# the stubs, for games (libnix)
# shellcheck disable=SC2086
"$CC" -noixemul -m68040 $SOFT -O2 -fno-delete-null-pointer-checks $WARN -I"$HERE/include" \
    -c -o "$OUT/obj/acgame_stubs.o" "$HERE/stubs/acgame_stubs.c"
rm -f "$OUT/libacgame.a"
"$AR" rcs "$OUT/libacgame.a" "$OUT/obj/acgame_stubs.o"
echo "$OUT/libacgame.a ($(wc -c < "$OUT/libacgame.a") bytes)"

# the tests, linked as a game links: the stubs, not the backend
# shellcheck disable=SC2086
"$CC" -noixemul -m68040 $SOFT -O2 -fno-delete-null-pointer-checks $WARN -I"$HERE/include" \
    -o "$OUT/tests/ACGameLibSmoke" "$HERE/examples/aga_smoke.c" "$OUT/libacgame.a"
# shellcheck disable=SC2086
"$CC" -noixemul -m68040 $SOFT -O2 -fno-delete-null-pointer-checks $WARN -I"$HERE/include" \
    -o "$OUT/tests/ACGameLibTwice" "$HERE/tests/lib_twice.c"
# the same smoke test with the backend linked in (the static build), to time
# the library against
# shellcheck disable=SC2086
"$CC" -noixemul -m68040 $SOFT -O2 -fno-delete-null-pointer-checks $WARN -I"$HERE/include" \
    -o "$OUT/tests/ACGameStaticSmoke" "$HERE/examples/aga_smoke.c" \
    "$HERE/src/c2p_ref.c" "$HERE/src/ham8.c" "$HERE/src/platform_aros_aga.c"
for f in ACGameLibSmoke ACGameLibTwice ACGameStaticSmoke; do
    echo "$OUT/tests/$f ($(wc -c < "$OUT/tests/$f") bytes)"
done
