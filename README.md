# ACGame

ACGame is the common game layer for AmiCradle's game ports on AmigaOS 3.x:
c2p, HAM8, AGA and RTG 8-bit screens, keyboard, mouse and pads (GFOpenInput,
with lowlevel.library as the fallback).

It is built as **acgame.library**, a shared library. Every game that uses
ACGame ships the acgame.library it was built against in its own `Libs/` drawer.
