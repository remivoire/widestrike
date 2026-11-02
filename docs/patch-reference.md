# Patch reference

Every location WideStrike touches, with the original bytes it verifies before
writing. If a site does not match, the patch is skipped and logged instead of
being applied blind - so a different build of `jsf.exe` degrades safely.

All RVAs are relative to the image base `0x400000`.

## Static byte patches

| RVA | VA | len | original | new | purpose |
|---|---|---|---|---|---|
| `0x17490` | `0x417490` | 3 | `55 ...` | `33 C0 C3` | CD volume probe returns "found" |
| `0xAA5EA` | `0x4AA5EA` | 4 | `01 00 00 00` | `00 00 00 00` | `mov eax,1` -> `mov eax,0`; windowed flag |
| `0xB0F2C` | `0x4B0F2C` | 6 | `0F 85 BF 00 00 00` | `90 x6` | drop "screenmode must be 16-bit" |
| `0xB126E` | `0x4B126E` | 5 | `A3 04 19 C4 00` | `90 x5` | stop `sub_4B1230` writing `dword_C41904` |
| `0x19C08` | `0x419C08` | 6 | `0F 85 ...` | `90 x6` | force software renderer |
| `0x4889C` | `0x44889C` | 1 | `8F` (at +5) | `87` | projected-coordinate guard `jg` -> `ja` |
| `0x488B2` | `0x4488B2` | 1 | `8F` | `87` | " |
| `0x5032D` | `0x45032D` | 1 | `8F` | `87` | " |
| `0x50345` | `0x450345` | 1 | `8F` | `87` | " |
| `0x50E7F` | `0x450E7F` | 1 | `8F` | `87` | " |
| `0x50E99` | `0x450E99` | 1 | `8F` | `87` | " |

## Code hooks (5-byte `E9` jump, or redirected call)

| RVA | VA | target | purpose |
|---|---|---|---|
| `0x1BFE2` | `0x41BFE2` | `ws_projection_hook` | per-frame hook: aspect correction + row-table hardening |
| `0xB16DD` | `0x4B16DD` | `ws_enum_cb` | replaces the `DDEnumModes` callback (`mov eax, 4B0B50` immediate) |
| `0xB1890` | `0x4B1890` | `ws_mode_count_hook` | count getter never returns 0 |
| `0xB18A0` | `0x4B18A0` | `ws_lookup_thunk` | `(w,h)` lookup never returns -1 |
| `0xDD343C` | `0x11D343C` | `ws_ddcreate` | `DirectDrawCreate` IAT slot |

## Runtime hooks (COM vtable)

| object | slot | target |
|---|---|---|
| `IDirectDraw` | 6 (`CreateSurface`) | `ws_create_surface` - forces RGB565 on the back buffer, records the primary |
| `IDirectDrawSurface` (primary) | 7 (`BltFast`) | `ws_bltfast` - converting/scaling present |

## Pattern patches

| pattern | count | action |
|---|---|---|
| `89 8C 24 <disp32> D1 E9` (`mov [esp+d],ecx ; shr ecx,1`) | 4 | last byte -> `F9` (`sar ecx,1`), negative span lengths exit the loop |

## Data

| address | value | purpose |
|---|---|---|
| `0x1177E30` | `[height .. 2047]` -> last scanline | scanline pointer table margin |
| `0x841928` | `0x0B050605` | pixel format: r5 bits / g6 bits / b5 bits / r shift |
| `0x84192C` | `0x00000005` | pixel format: g shift = 5, b shift = 0 |

## Verifying

```
powershell -ExecutionPolicy Bypass -File tools\verify.ps1 -Exe ..\jsf.exe
build\host_test.exe ..\jsf.exe build\glide2x.dll
```

`verify.ps1` checks every static site against the file on disk and prints the
projection table. `host_test` loads the DLL, resolves the decorated Glide
exports, maps `jsf.exe` with `DONT_RESOLVE_DLL_REFERENCES`, runs the real patch
code against that image and confirms every group applied - including when the
image is relocated, which is what ASLR does on modern Windows.
