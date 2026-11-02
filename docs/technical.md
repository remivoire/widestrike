# Joint Strike Fighter - engine notes

Everything WideStrike relies on verified against
`jsf.exe` (1,072,128 bytes,
SHA-256 `c05392396b6b5b2fc83e0cbda4a71becdcd6c5689fb682b469756b845065a4e0`).

Addresses are file/image addresses (image base `0x400000`). Runtime addresses are
`base + (addr - 0x400000)` so the patch survives ASLR.

---

## 1. Architecture

**Joint Strike Fighter** (Innerloop / Eidos, 1997). Microsoft Visual C++ build
(the CRT `R60xx` strings and standard prologues are present), 2,691 functions,
768 KB of `.text`.

The game is a **C game with a hand-written assembly software rasteriser**. That
split matters for patching: the C side calls the assembly blitters through
ordinary `call` instructions, so the boundaries are explicit and stable.

The renderer core lives in `0x401000`-`0x40F000` (415 functions, 53 KB). The very
first function is unambiguous hand-written assembly, which is the clearest
evidence of the design:

```
401000  60                 pusha
401001  89 C8              mov  eax, ecx
401003  89 25 04 50 1D 01  mov  ds:dword_11D5004, esp   ; save host stack
401009  81 EC 20 4E 00 00  sub  esp, 4E20h             ; 20 KB private stack
40100F  83 E4 E0           and  esp, 0FFFFFFE0h         ; 32-byte align
...
4010A3  8B 25 00 50 1D 01  mov  esp, ds:dword_11D5000   ; restore
```

Other routines carry hand-arranged calling conventions, which is why Hex-Rays
renders them as `__userpurge ... @<edx:eax>` (arguments returned in `edx:eax`)
or `__usercall @<eax>(char @<sil>)` (a parameter in the low byte of `esi`).

Two renderers exist, selected by `eRenderMode` in `SESSION.FLP`
(`SESSION_RENDER_MODE_SOFTWARE` / `SESSION_RENDER_MODE_3DFX`, strings at
`0x4C4B00` / `0x4C4B20`, written from the template at `0x4C4C40` by
`sub_419BC0`):

* **software** (default) - the assembly rasteriser blits into a locked
  DirectDraw back buffer;
* **3dfx / Glide** - hardware triangles plus linear-framebuffer software
  drawing. `glide2x.dll` is a static import, so some Glide DLL must exist for
  the process to start at all.

`dword_4E55A8` is the "3dfx active" flag, tested in more than 50 functions.

---

## 2. Projection (`sub_41B8A0`, `0x41B8A0`)

Runs once per frame from `sub_41BF70` (call site `0x41BFE2`) and builds the whole
3D projection:

```
htan   = fov * pi/4096          ; fov = 1024.0 at camera+0xA4
vtan   = 0.75 * htan            ; hard-coded 3/4 == 4:3
xscale = (W/2) / htan           ; camera+0x38, 0x1177DB0
yscale = (H/2) / vtan           ; camera+0x3C, 0x1177DB8
```

The `0.75` is the engine assuming a 4:3 viewport. Both scales are identical at
4:3 and diverge at every other aspect, which is why a naive resolution change
looks horizontally stretched (4/3 too wide at 16:9).

The same two tangents drive the frustum/FOV culling in `sub_4507A0`
(`0x450900`, `0x450948`), so they must stay consistent with the render scales.

### WideStrike's correction

The buffer the engine renders into and the image finally presented can have
different aspects, and the final geometry is what matters:

```
k     = (4/3) / output_aspect          ; 0.75 at 16:9, 1.0 at 4:3
xscale *= k
htan   = htan / k
```

This is a ratio applied to the engine's own values, so it is independent of
buffer size and never touches fixed-point state. At 4:3 the wrapper returns
without changing anything.

Resulting numbers (`tools/verify.ps1` prints this table):

| resolution | stock x/y scale | stock stretch | patched x/y | patched stretch | hfov |
|---|---|---|---|---|---|
| 640x480   | 407.4 / 407.4   | 1.00x | 407.4 / 407.4   | 1.00x | 76.3° |
| 800x600   | 509.3 / 509.3   | 1.00x | 509.3 / 509.3   | 1.00x | 76.3° |
| 1280x720  | 814.9 / 611.2   | 1.33x | 611.2 / 611.2   | 1.00x | 92.6° |
| 1920x1080 | 1222.3 / 916.7  | 1.33x | 916.7 / 916.7   | 1.00x | 92.6° |
| 3440x1440 | 2190.0 / 1222.3 | 1.79x | 1222.3 / 1222.3 | 1.00x | 109.2° |

`fov=2` selects the opposite trade-off (preserve horizontal framing, crop
vertically); `fov=0` leaves the projection untouched.

---

## 3. Mode table

`sub_4B0B50` is the `DDEnumCallback` that fills the game's mode table:

```
dword_C41700        int  count
dword_C41704 + 8*i  int  width      (64 slots: 0x841704 .. 0x841904)
dword_C41704 + 8*i  int  height
dword_C4190C        int  nCurrentScreenMode
```

Two problems on current Windows:

* the callback never checks the 64-entry capacity, so a driver reporting many
  modes overwrites the globals that follow the table (surface pointers, the
  current mode index, ...);
* DirectDraw now reports far fewer 16 bpp modes than the display supports, so
  widescreen modes never appear in the in-game list.

Three supporting functions:

| function | purpose |
|---|---|
| `sub_4B1890` (`0x4B1890`) | returns the count |
| `sub_4B18A0` (`0x4B18A0`) | `(w,h)` -> index, returns `-1` when absent |
| `sub_4B1880` (`0x4B1880`) | `&table[2*index]` |

`sub_4B15A0` zeroes the count and re-enumerates every time the display is
(re)created, and `sub_4B1230` stores its first argument into `dword_C41904`,
which selects the `SetDisplayMode` branch of `sub_4B0C70`.

### Failure modes we had to fix

1. `-1` from the lookup makes the engine read `dword_C41704[-2]` and ask
   DirectDraw for a **0×0 surface**.
2. The graphics screen does `nCurrentScreenMode %= count` at `0x43B87A`; with an
   empty table that is an **integer divide by zero** (`0xC0000094`).
3. `EnumDisplayModes` on current drivers can report no modes at all, so the
   callback never runs and the table stays empty.

WideStrike replaces the callback (capacity-checked, same sorted-insertion rule),
injects every resolution `EnumDisplaySettingsA` reports, and hooks both the count
getter and the lookup so neither can return `-1`/`0`.

---

## 4. Projected-coordinate guards

Six projection routines validate a vertex like this (imm addresses
`0x44889C 0x4488B2 0x45032D 0x450345 0x450E7F 0x450E99`):

```
test  reg,reg
jl    negative
mov   ecx,reg
cmp   ecx,4000h
jg    reject
```

The value comes from a perspective divide followed by `fistp`; a vertex on the
camera plane yields ±inf/NaN, `fistp` produces `INT_MIN`, and `neg` leaves it
unchanged. `INT_MIN` compares as *negative*, so the signed `jg` lets it through
and a garbage screen coordinate reaches the span filler.

Switching `jg` (`0F 8F`) to `ja` (`0F 87`) rejects `INT_MIN` while leaving every
legitimate value unchanged.

---

## 5. Display path

The engine calls `SetDisplayMode(w, h, 16)` and writes 16-bit pixels straight
into the locked back buffer. Windows 8+ drivers expose no 16 bpp modes at all
(`ChangeDisplaySettingsEx(CDS_TEST)` rejects every candidate), so the call fails
with `Could not set display mode!`.

The rasteriser is left untouched; only presentation changes:

| step | mechanism |
|---|---|
| no mode switch | `dword_C3D704` forced to 0 -> DirectDraw runs `DDSCL_NORMAL` |
| borderless window | `sub_49C780`'s windowed branch plus `WS_POPUP`/`SetWindowPos` |
| no 16 bpp desktop | the `cmp eax,10h / jnz` guard at `0x4B0F27` is removed |
| 16 bpp target | `IDirectDraw::CreateSurface` hooked; the back buffer is rewritten to explicit RGB565 |
| colour packing | `dword_C41928 = 0x0B050605`, `dword_C4192C = 5` (r5@11, g6@5, b5@0 - the same encoding the 3dfx path installs) |
| presentation | the engine's windowed path does `BltFast`; we route it through `Blt`, which converts 565 -> 32 bpp and scales |

`BltFast` cannot convert pixel formats, which is why the initial versions showed
a black screen with working audio. `Blt` with a destination rect that differs
from the source rect converts *and* stretches in one pass.

### The 640-pixel 2D limit

The 2D layer writes scanlines with a hard **640-pixel stride**. Measured by
forcing render sizes and screenshotting each:

| render size | result |
|---|---|
| 1920x1080 | title art tiled exactly 3x (1920/640) |
| 1280x720 | tiled exactly 2x |
| 800x600 | sheared (1.25x) |
| 640x480 | correct |

So the engine cannot render 2D wider than 640 pixels. The 3D pipeline adapts;
the 2D one does not. Menus therefore run at 640x480 and the in-flight view can
run at the selected mode (the game creates the two separately).

---

## 6. HUD

The HUD and menus are already drawn at native pixel size:

* the 2D layer writes directly into the same 16 bpp back buffer at native
  coordinates - there is no virtual buffer to scale up;
* text comes from four pre-rendered bitmap fonts in `MISC\` (`FONT18X2`,
  `FONT12X1`, `FONT8X9`, `FONT6X8`); `sub_46D710` picks the largest that fits a
  44x25 character box (`dword_BFAAF0[4]`, `dword_4DB930[10*n]`);
* positions derive from the live surface size (`sub_4B1CF0` / `sub_4B1D40`).

WideStrike therefore does not touch 2D drawing at all.

---

## 7. Rasteriser hardening

Two latent assumptions produce `0xC0000005` write faults in the span filler
(`sub_40BE5B`, at `0x40BFDE` / `0x40C03E`).

### 7.1 Undefined scanline pointers

`dword_1177E30` is a **2048-entry** table. `sub_41B6F0` initialises only
`[0 .. height-1]`:

```c
for (i = 0; i < height; i++)
    rowptr[i] = surface + i * pitch;
```

The fillers fetch `mov reg, dword_1177E30[y*4]` and write through the result, so
a scanline past `height` reads a zero or stale entry. At 1080p that leaves 968
entries undefined.

Unused entries are pointed at the last real scanline every frame, so a stray
scanline paints the bottom row instead of dereferencing garbage.

### 7.2 Negative span length

```
mov [esp+arg_1EC], ecx
shr ecx, 1              ; length -> dword stores
loop:
mov [edi], edx          ; two 16 bpp pixels
dec ecx
jg  loop
```

`shr` is *logical*: a negative length becomes a ~4 billion iteration counter and
the fill runs off the end of the framebuffer. The span start is clamped to `>= 0`
but the length never is. The four such sites are rewritten to `sar ecx,1`, which
is identical for valid lengths and makes a negative one exit immediately.

---

## 8. Miscellaneous

* **CD probe** - `WinMain` loops on `sub_417490() == -1`, which walks `A:`..`Z:`,
  requires `GetDriveTypeA() == DRIVE_CDROM` and compares the volume label with
  the install label. Never succeeds on a download/HDD install.
* **Renderer lock** - selecting 3dfx without a real Glide library makes the game
  `exit(1)` with `UNABLE TO OPEN 3DFX 640x480 SCREEN`; the branch at `0x419C08`
  is nop'ed so a stale `eRenderMode` falls back to software.
* **Static imports** - `glide2x.dll`, `winplay.dll`, `winstr.dll`, `IFORCE2.dll`
  and `DPLAYX.dll` are all resolved before any `DllMain` runs, so the game will
  not start (and no patch can run) unless they all exist. `DPLAYX.dll` is
  imported by **ordinal 4**, not by name.
