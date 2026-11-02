/*
 * WideStrike - rasteriser hardening
 *
 * The software rasteriser has two latent 1997-era assumptions that a modern
 * machine (1920x1080, arbitrary geometry) can violate:
 *
 * 1. Scanline pointer table
 *
 *        void *rowptr[2048];                    // 0x1177E30
 *        for (i = 0; i < height; i++)           // sub_41B6F0
 *            rowptr[i] = surface + i * pitch;
 *
 *    Only [0, height) is initialised.  The span fillers fetch
 *    `mov reg, dword_1177E30[y*4]` and write through the result, so a
 *    scanline index past `height` reads a stale or zero entry and the write
 *    faults (0xC0000005 in e.g. sub_40BE5B at 0x40C03E / 0x40BFDE).
 *
 *    There are 968 unused slots at 1080p, so we point every unused entry at
 *    the last real scanline.  An out-of-range scanline then draws on the
 *    bottom row instead of dereferencing garbage - a cosmetic artefact at
 *    worst, never a crash.
 *
 * 2. Span length
 *
 *        mov [esp+arg_1EC], ecx
 *        shr ecx, 1            ; length -> dwords
 *        jz  done
 *      loop:
 *        mov [edi], edx        ; two 16 bpp pixels at a time
 *        dec ecx
 *        jg  loop
 *
 *    `shr` is *logical*: a negative span length becomes a ~4 billion
 *    iteration counter and the fill runs off the end of the framebuffer.
 *    The span start is clamped to >= 0 but the length never is.  `sar`
 *    behaves identically for every valid (non-negative) length and makes a
 *    negative one fall straight out of the loop.
 */
#include "wstrike.h"

#define ROW_TABLE_ENTRIES 2048

/* ---------------------------------------------------------------- 1 */

void ws_harden_row_table(void)
{
    void        **tab  = (void **)(g_base + RVA_ROWPTR);
    int           h, i;
    void         *last;
    static int    logged;

    if (!g_cfg.harden) return;

    h = ws_screen_h();
    if (h <= 1 || h >= ROW_TABLE_ENTRIES) return;

    /* Take the pointer the engine itself just wrote for the last real scanline
     * instead of recomputing one from getters whose roles are ambiguous
     * (sub_4B1CA0 is the primary in some paths, the back buffer in others).
     * Recomputing produced bogus pointers, which anything indexing a scanline
     * past `height` would then draw through. */
    last = tab[h - 1];
    if (!last) return;

    for (i = h; i < ROW_TABLE_ENTRIES; i++)
        tab[i] = last;

    if (!logged) {
        ws_log("harden: row table margin [%d..%d] -> last scanline %p",
               h, ROW_TABLE_ENTRIES - 1, last);
        logged = 1;
    }
}

/* ---------------------------------------------------------------- 2 */

int ws_patch_span_guards(void)
{
    /* 89 8C 24 <disp32> D1 E9   ==   mov [esp+disp], ecx ; shr ecx,1 */
    static const BYTE pat[7] = { 0x89, 0x8C, 0x24, 0, 0, 0, 0 };
    BYTE  *text;
    size_t i, len;
    int    patched = 0;

    if (!g_active) return 0;

    text = (BYTE *)(g_base + 0x1000);          /* .text rva 0x1000 */
    len  = 0xC0000u;                           /* .text size        */

    for (i = 0; i + 9 <= len; i++) {
        if (memcmp(text + i, pat, 3) != 0) continue;
        if (text[i + 7] != 0xD1 || text[i + 8] != 0xE9) continue;

        {
            BYTE sar = 0xF9;                   /* D1 F9 = sar ecx,1 */
            if (ws_write(0x1000u + i + 8, &sar, 1))
                patched++;
        }
    }

    ws_log("harden: %d span-length guards (shr -> sar)", patched);
    return patched;
}
