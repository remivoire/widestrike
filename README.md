# WideStrike

A widescreen and stability patch for **Joint Strike Fighter** (Innerloop / Eidos, 1997)

WideStrike makes this amazing 90s flightsim run properly on a modern PC: fullscreen
without the historic displaymode problems, widescreen resolutions with a correct
field of view and the software renderer hardened against crashes that only
modern hardware can trigger.

It is a single DLL. Drop it next to the game, run the game and done!

---

## Installation

1. Copy **`glide2x.dll`** into the folder that contains `jsf.exe`.
2. Start the game.
3. Pick your resolution under `OPTIONS -> SCREEN RESOLUTION`.

`wstrike.ini` and `wstrike.log` appear next to the executable on first run.

To uninstall, delete `glide2x.dll` (and optionally the two files above).

> WideStrike is an unofficial fan patch. It contains no game data and requires a
> legitimate copy of Joint Strike Fighter.

### Why is it called `glide2x.dll`?

`jsf.exe` links 3dfx's Glide library at load time, so the game will not even
start unless a `glide2x.dll` is present. WideStrike takes that slot: Windows
loads it before the game runs, which is how it can patch anything without a
launcher, injector or wrapper. The Glide functions themselves are inert - the
game keeps using its own software renderer. Now, who uses 3DFX in 2026? Lol

### Requirements

* 32-bit Windows with DirectDraw (any Windows from XP to 11).
* The four other libraries `jsf.exe` links against: `winplay.dll`, `winstr.dll`,
  `IFORCE2.dll`, `DPLAYX.dll`. A complete CD install already has them. If yours
  does not, `make stubs` builds replacements (see **Missing game DLLs** below).

---

## What it changes

**Widescreen with a correct field of view.**
The engine assumed a 4:3 screen and stretched the image on anything else. The
projection is now derived from your actual screen shape: same vertical framing,
wider horizontal view, no distortion. Works at 16:9, 16:10 and ultrawide.

**Every resolution your display offers**
The in-game resolution list is rebuilt from the display, not from what
DirectDraw happens to report. Includes widescreen and high-DPI modes the game
never knew about....

**Fullscreen without display mode switching**
No more `Could not set display mode!`. The game renders in its native 16-bit
format and the result is converted to your desktop format on the way to the
screen. Alt-Tab behaves like a normal window.

**Borderless window**
A real full-screen borderless window instead of a caption and taskbar.

**Stability fixes**
Several latent engine bugs that modern machines can trigger - a crash when
looking at the sky, a divide-by-zero when opening the Graphics menu, and two
rasteriser overruns that surface at high resolutions - are fixed.

**No CD check included**
The volume-label probe is satisfied automatically, so a download or hard-disk
install works. Who needs CD checks? Exactly.

---

## Configuration - `wstrike.ini`

Created next to `jsf.exe` on first run. Every setting is optional; delete a line
to use its default.

| key | default | meaning |
|---|---|---|
| `enabled` | `1` | `0` = load and do nothing |
| `log` | `1` | write `wstrike.log` |
| `fov` | `1` | `1` widescreen (recommended), `2` crop vertically instead, `0` original 4:3 projection |
| `aspect` | `1` | `1` fill the screen, `2` keep 4:3 with side bars, `3` integer scale (pixel-perfect) |
| `modes` | `1` | add every resolution the display offers |
| `max_width` / `max_height` | `0` | `0` = desktop size; caps the resolutions offered |
| `custom_modes` | widescreen list | extra `1280x720,1920x1080,...` entries |
| `windowed` | `1` | borderless window instead of exclusive full-screen |
| `cd_check` | `1` | skip the CD volume-label probe |
| `clip_guard` | `1` | fix the projected-coordinate guards |
| `force_software` | `1` | never use the 3dfx renderer |
| `test_16bpp` | `1` | only offer modes the driver accepts at 16 bpp |
| `force_desktop` / `render_w` / `render_h` | `0` | advanced, see below |

### Choosing how the picture is scaled

The engine can only draw its 2D layer 640 pixels wide, so the 3D view is what
gets the widescreen treatment; the menus and HUD are original 4:3 artwork. That
leaves a choice about the final scaling:

| `aspect` | result |
|---|---|
| `1` | fills the screen. True widescreen; 3D geometry is correct, 2D art is 33% wider than originally drawn. |
| `2` | 4:3 picture centred with black side bars. Everything keeps its original proportions. |
| `3` | integer scaling, pixel-perfect, with bars where the screen does not divide evenly. Sharpest text and art, smallest image. |

### Advanced

`force_desktop=1` makes the engine render at `render_w` x `render_h` (default:
the desktop size) instead of its own mode. The 3D view works at any size, but
the 2D layer will tile above 640 pixels wide, so this is a debugging aid rather
than a usable setting.

---

## Troubleshooting

**"Could not set display mode!" or "Could not create off-screen surface!"**
The patch did not load. Confirm `glide2x.dll` sits next to `jsf.exe` and check
`wstrike.log` - if the file does not exist, the DLL was never loaded.

**The game will not start / "a required DLL is missing"**
`jsf.exe` needs `winplay.dll`, `winstr.dll`, `IFORCE2.dll` and `DPLAYX.dll`.
Windows resolves all of them before running any code, so the game (and the
patch) cannot start without them. A full CD install has all four.

DPLAYX.dll might be irrelevant if you have DirectPlay enabled on your Windows
install.

**Missing game DLLs**
`make stubs` in the source tree builds inert `winplay.dll`, `winstr.dll`,
`IFORCE2.dll` and `DPLAYX.dll`. They let the game start, but movie playback,
force feedback and the multiplayer lobby will not work. Prefer the originals if
you have them! This works for basic singleplayer gameplay though :)

**Everything is black but I can hear audio**
An older build had this when pixel-format conversion failed. Make sure you are
running the current `glide2x.dll`; `wstrike.log` should contain
`disp : BltFast hooked`.

**Text and menus look stretched**
That is `aspect=1`. Set `aspect=2` for original proportions with side bars, or
`aspect=3` for pixel-perfect integer scaling.

**Performance**
The renderer is 1997 software rasterisation on one CPU core. 640x480 and 800x600
run comfortably but 1920x1080 is ~6.8x the pixels. If a frame rate is tight, lower
the resolution in the game menu. Even modern hardware might struggle..

**Something else**
`wstrike.log` records which patches applied and the detected mode table - it is
the first thing worth attaching to a bug report.

Do keep in mind the game by nature is still pretty unstable, especially on modern
hardware. This patch aims to fix most of those, but without bug reports, it can't
improve! Please do keep them coming.

---

## Building from source

MSYS2 with 32-bit MinGW:

```
pacman -S mingw-w64-i686-gcc make
cd patch
make                # -> build/glide2x.dll
make stubs          # optional replacement DLLs
make clean
```

Or from PowerShell:

```
C:\msys64\usr\bin\bash.exe -lc "cd '/e/Joint Strike Fighter/patch' && make"
```

Before shipping a build, run the offline checks:

```
powershell -ExecutionPolicy Bypass -File tools\verify.ps1 -Exe ..\jsf.exe
gcc -m32 -O2 tools/host_test.c -o build/host_test.exe
build\host_test.exe ..\jsf.exe build\glide2x.dll
```

The first verifies every patch site against the file on disk; the second applies
the real patch code to a memory-mapped copy of the game and confirms every group
took effect, including under image relocation.

---

## Technical documentation

The details live in `docs/`:

* [`docs/technical.md`](docs/technical.md) - how the engine works, what was
  broken and why, including the projection maths, the 640-pixel 2D limit and the
  rasteriser bugs
* [`docs/patch-reference.md`](docs/patch-reference.md) - every address and byte
  the patch touches with the original values it verifies

---

## Layout

```
patch/
  README.md               this file
  docs/                   engine notes and patch reference
  Makefile                build (MSYS2 / mingw32 gcc)
  wstrike.ini             default configuration
  src/                    patch sources
  stubs/                  optional replacement DLLs
  tools/                  static verifier, loader harness, stub generator
  build/glide2x.dll       <- copy this next to jsf.exe
```

## Credits

Patch by the WideStrike contributors. Joint Strike Fighter and the Innerloop
Terraform engine are the property of their respective rights holders; this project 
ships no game code or assets and is not affiliated with or endorsed by them.
