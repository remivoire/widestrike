/*
 * WideStrike - mode index lookup
 *
 * sub_4B18A0 (0x4B18A0) maps a (width,height) pair to an index into the mode
 * table, returning -1 when the pair is not present:
 *
 *     int sub_4B18A0(int w [ecx], int h [edx])
 *     {
 *         if (count <= 0) return -1;
 *         for (i = 0; i < count; i++)
 *             if (w == table[i].w && h == table[i].h) return i;
 *         return -1;
 *     }
 *
 * The engine calls it with (640,480) before every display setup.  If the
 * driver reported no 16 bpp modes the table can be empty at that moment, the
 * lookup returns -1, and sub_4B0C70 then reads table[-1] and asks DirectDraw
 * for a 0x0 surface - which fails and leaves the renderer drawing into a
 * surface the display code never intended.
 *
 * We replace the function: repopulate the table if it is empty, do the same
 * search, and fall back to index 0 (always 640x480) instead of -1.
 */
#include "wstrike.h"

int ws_lookup_impl(int w, int h);

int ws_lookup_impl(int w, int h)
{
    int i, n = *(int *)(g_base + RVA_MODE_COUNT);

    if (n <= 0) {
        extern void ws_inject_modes_public(void);
        ws_inject_modes_public();
        n = *(int *)(g_base + RVA_MODE_COUNT);
    }

    /* Native resolution mode: resolve everything to the entry that matches
     * the desktop.  The engine then creates its back buffer at that size AND
     * its own table agrees with it, so nothing is inconsistent - it simply
     * renders 1920x1080 instead of 640x480. */
    if (g_cfg.force_desktop) {
        int dw, dh;
        ws_render_size(&dw, &dh);
        for (i = 0; i < n; i++) {
            int *e = (int *)(g_base + RVA_MODE_TABLE + 8 * i);
            if (e[0] == dw && e[1] == dh)
                return i;
        }
    }

    for (i = 0; i < n; i++) {
        int *e = (int *)(g_base + RVA_MODE_TABLE + 8 * i);
        if (e[0] == w && e[1] == h)
            return i;
    }

    if (n > 0) {
        int *e = (int *)(g_base + RVA_MODE_TABLE);
        ws_log("modes: %dx%d not in table (%d entries), falling back to index 0 (%dx%d)",
               w, h, n, e[0], e[1]);
        return 0;
    }
    return -1;
}

int ws_patch_mode_lookup(void)
{
    extern void ws_lookup_thunk(void);
    static const BYTE expect[3] = { 0x56, 0x55, 0x53 };   /* push esi/ebp/ebx */
    DWORD rel;
    BYTE  patch[5];

    if (!ws_verify_bytes(RVA_MODE_LOOKUP, expect, sizeof(expect))) {
        ws_log("!! mode lookup prologue mismatch, skipped");
        return 0;
    }

    rel = (DWORD)((uintptr_t)&ws_lookup_thunk - (g_base + RVA_MODE_LOOKUP + 5));
    patch[0] = 0xE9;
    memcpy(patch + 1, &rel, 4);

    if (!ws_write(RVA_MODE_LOOKUP, patch, 5)) return 0;
    ws_log("modes: (w,h) lookup hooked (never returns -1)");
    return 1;
}

/*
 * sub_4B1890 (0x4B1890) returns the mode table count:
 *
 *     int sub_4B1890(void) { return dword_C41700; }
 *
 * The graphics screen does `nCurrentScreenMode %= count` (0x43B87A) and
 * crashes with an integer divide by zero when count is 0.  That happens when
 * the menu re-enumerates the display and DirectDraw reports no modes at all,
 * so our enumeration callback never runs and the table stays empty.
 *
 * Replace the getter so it repopulates the table and never returns 0.
 */
int ws_mode_count_hook(void);

int ws_mode_count_hook(void)
{
    int n = *(int *)(g_base + RVA_MODE_COUNT);

    if (n <= 0) {
        extern void ws_inject_modes_public(void);
        ws_inject_modes_public();
        n = *(int *)(g_base + RVA_MODE_COUNT);
    }
    return n;
}

int ws_patch_mode_count(void)
{
    static const BYTE expect[6] = { 0xA1, 0x00, 0x17, 0xC4, 0x00, 0xC3 };
    DWORD rel;
    BYTE  patch[5];

    if (!ws_verify_bytes(RVA_MODE_COUNT_GET, expect, sizeof(expect))) {
        ws_log("!! mode count getter mismatch, skipped");
        return 0;
    }
    rel = (DWORD)((uintptr_t)&ws_mode_count_hook - (g_base + RVA_MODE_COUNT_GET + 5));
    patch[0] = 0xE9;
    memcpy(patch + 1, &rel, 4);
    if (!ws_write(RVA_MODE_COUNT_GET, patch, 5)) return 0;
    ws_log("modes: count getter hooked (never returns 0)");
    return 1;
}
