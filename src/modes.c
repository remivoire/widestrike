/*
 * WideStrike - display mode table
 *
 * The game fills a 64 entry table with every 16 bpp mode DirectDraw
 * reports (sub_4B0B50 is the DDEnumCallback):
 *
 *     dword_C41700        : count          (int)
 *     dword_C41704[8*i+0] : width          (int)
 *     dword_C41704[8*i+4] : height         (int)
 *
 * Two problems on modern machines:
 *   1. the callback never checks the capacity, so a driver that reports
 *      more than 64 modes walks straight into the globals that follow
 *      (surface pointers, current mode index ...),
 *   2. DirectDraw on current drivers usually reports far fewer 16 bpp
 *      modes than the display actually supports, so widescreen modes
 *      never show up in the in-game list.
 *
 * We replace the callback with our own: same sorting rule, plus a hard
 * capacity check, plus the modes we gather from EnumDisplaySettings.
 */
#include "wstrike.h"

#ifndef DDENUMRET_OK
#define DDENUMRET_OK 1
#endif

/* minimal DDSURFACEDESC (v1, 108 bytes) - we only need the geometry and
 * the pixel format bit count, and we do not want to pull in ddraw.h */
typedef struct ws_ddsd {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG  lPitch;
    DWORD dwBackBufferCount;
    DWORD dwMipMapCount;
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    void *lpSurface;
    DWORD ck[8];              /* 4 x DDCOLORKEY */
    DWORD pfSize;
    DWORD pfFlags;
    DWORD pfFourCC;
    DWORD pfBitCount;         /* offset 84 == ddpfPixelFormat.dwRGBBitCount */
} ws_ddsd_t;



static int *ws_mode_count(void) { return (int *)(g_base + RVA_MODE_COUNT); }
static int *ws_mode_w(int i)    { return (int *)(g_base + RVA_MODE_TABLE + 8 * i); }
static int *ws_mode_h(int i)    { return (int *)(g_base + RVA_MODE_TABLE + 8 * i + 4); }

static int ws_mode_find(int w, int h)
{
    int i, n = *ws_mode_count();
    for (i = 0; i < n; i++)
        if (*ws_mode_w(i) == w && *ws_mode_h(i) == h) return i;
    return -1;
}

/* identical ordering rule to the original callback:
 * first slot whose width AND height are both >= the new mode */
static int ws_mode_insert(int w, int h)
{
    int n = *ws_mode_count();
    int i, j;

    if (w < 320 || h < 200) return 0;
    if (n >= MODE_TABLE_MAX) return 0;
    if (ws_mode_find(w, h) >= 0) return 0;

    for (i = 0; i < n; i++) {
        if (*ws_mode_w(i) >= w && *ws_mode_h(i) >= h) break;
    }
    for (j = n; j > i; j--) {
        *ws_mode_w(j) = *ws_mode_w(j - 1);
        *ws_mode_h(j) = *ws_mode_h(j - 1);
    }
    *ws_mode_w(i) = w;
    *ws_mode_h(i) = h;
    *ws_mode_count() = n + 1;
    return 1;
}

static int ws_mode_supported_16(int w, int h)
{
    DEVMODEA dm;
    LONG r;

    memset(&dm, 0, sizeof(dm));
    dm.dmSize       = sizeof(dm);
    dm.dmFields     = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    dm.dmBitsPerPel = 16;
    dm.dmPelsWidth  = (DWORD)w;
    dm.dmPelsHeight = (DWORD)h;

    r = ChangeDisplaySettingsExA(NULL, &dm, NULL, CDS_TEST, NULL);
    return r == DISP_CHANGE_SUCCESSFUL;
}

typedef struct { int w, h; } ws_mode_t;

static int ws_collect_display_modes(ws_mode_t *out, int max, int max_w, int max_h)
{
    DEVMODEA dm;
    DWORD    i;
    int      n = 0;
    int      tested = 0, supported = 0;

    for (i = 0; i < 1024 && n < max; i++) {
        int w, h, dup = 0, k;

        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsA(NULL, i, &dm)) break;
        if (!dm.dmPelsWidth || !dm.dmPelsHeight) continue;

        w = (int)dm.dmPelsWidth;
        h = (int)dm.dmPelsHeight;
        if (w < 640 || h < 400) continue;
        if (max_w && w > max_w) continue;
        if (max_h && h > max_h) continue;

        for (k = 0; k < n; k++) if (out[k].w == w && out[k].h == h) { dup = 1; break; }
        if (dup) continue;

        out[n].w = w;
        out[n].h = h;
        n++;
    }

    /* keep only modes the driver really accepts at 16 bpp, unless the
     * driver accepts none at all (then the test is useless, e.g. it is
     * handled by a DirectDraw compatibility layer) */
    if (g_cfg.test_16bpp && n > 0) {
        int dst = 0;
        for (i = 0; i < (DWORD)n; i++) {
            tested++;
            if (ws_mode_supported_16(out[i].w, out[i].h)) {
                out[dst++] = out[i];
                supported++;
            }
        }
        if (supported > 0) {
            ws_log("modes: 16bpp test kept %d of %d display modes", supported, tested);
            n = dst;
        } else {
            ws_log("modes: no mode passed the 16bpp test, injecting unfiltered");
        }
    }
    return n;
}

static void ws_parse_custom(const char *s, int max_w, int max_h)
{
    while (s && *s) {
        int w = 0, h = 0;
        while (*s == ',' || *s == ' ' || *s == ';') s++;
        if (!*s) break;
        w = atoi(s);
        while (*s && *s != 'x' && *s != 'X') s++;
        if (*s) s++;
        h = atoi(s);
        while (*s && *s != ',') s++;
        if (w >= 640 && h >= 400 && (!max_w || w <= max_w) && (!max_h || h <= max_h))
            ws_mode_insert(w, h);
    }
}

static void ws_inject_modes(void)
{
    static ws_mode_t list[256];
    int  n, i, added = 0;
    int  desk_w = GetSystemMetrics(SM_CXSCREEN);
    int  desk_h = GetSystemMetrics(SM_CYSCREEN);
    int  max_w  = g_cfg.max_width  > 0 ? g_cfg.max_width  : desk_w;
    int  max_h  = g_cfg.max_height > 0 ? g_cfg.max_height : desk_h;

    /* Native mode: the table holds exactly one entry - the render size.
     * The engine uses index 0 for its initial setup and resolves every
     * (w,h) probe through ws_lookup_impl(), so both display inits then agree
     * on the same resolution and the renderer never sees a stale size. */
    if (g_cfg.force_desktop) {
        int rw, rh;
        ws_render_size(&rw, &rh);
        if (rw >= 640 && rh >= 400) {
            ws_mode_insert(rw, rh);
            ws_log("modes: native mode, table = %dx%d (1/64)", rw, rh);
            return;
        }
    }

    /* always keep the classics reachable (within the configured cap) */
    if (!max_w || 640 <= max_w)  added += ws_mode_insert(640, 480);
    if ((!max_w || 800 <= max_w) && (!max_h || 600 <= max_h))  added += ws_mode_insert(800, 600);
    if ((!max_w || 1024 <= max_w) && (!max_h || 768 <= max_h)) added += ws_mode_insert(1024, 768);

    n = ws_collect_display_modes(list, (int)(sizeof(list) / sizeof(list[0])), max_w, max_h);
    for (i = 0; i < n; i++) added += ws_mode_insert(list[i].w, list[i].h);

    if (g_cfg.custom_modes[0])
        ws_parse_custom(g_cfg.custom_modes, max_w, max_h);

    ws_log("modes: desktop %dx%d, injected %d entries, table now %d/%d",
           desk_w, desk_h, added, *ws_mode_count(), MODE_TABLE_MAX);
}

/* replacement for sub_4B0B50 */
static HRESULT WINAPI ws_enum_cb(ws_ddsd_t *d, LPVOID ctx)
{
    (void)ctx;
    if (!g_active) return DDENUMRET_OK;

    /* The engine zeroes the count and re-enumerates every time it (re)creates
     * the display.  Inject whenever we see an empty table, not just once -
     * otherwise a re-enumeration (where the driver reports no 16 bpp mode and
     * therefore calls us with nothing useful) leaves the table empty and
     * sub_4B18A0((640,480)) returns -1, which turns into a 0x0 surface. */
    if (*ws_mode_count() == 0)
        ws_inject_modes();

    if (!d) return DDENUMRET_OK;

    if (d->pfBitCount == 16)
        ws_mode_insert((int)d->dwWidth, (int)d->dwHeight);

    return DDENUMRET_OK;
}

void ws_log_mode_table(void)
{
    int i, n = *ws_mode_count();
    char buf[2048];
    size_t off = 0;
    int idx = *(int *)(g_base + RVA_MODE_INDEX);

    buf[0] = 0;
    for (i = 0; i < n && off + 24 < sizeof(buf); i++) {
        off += (size_t)_snprintf(buf + off, sizeof(buf) - off, "%s%d:%dx%d",
                                 i ? " " : "", i, *ws_mode_w(i), *ws_mode_h(i));
    }
    ws_log("modes: %s", buf);
    ws_log("modes: nCurrentScreenMode = %d (%s)", idx,
           (idx >= 0 && idx < n) ? "valid" : "OUT OF RANGE");
}

int ws_patch_modes(void)
{
    static const BYTE expect[5] = { 0xB8, 0x50, 0x0B, 0x4B, 0x00 }; /* mov eax, 4B0B50 */
    DWORD  addr;
    BYTE   patch[4];

    if (!g_cfg.modes) {
        ws_log("modes: prevented by config");
        return 0;
    }
    if (!ws_verify_bytes(RVA_ENUM_CB_IMM - 1, expect, sizeof(expect))) {
        ws_log("!! mode callback site mismatch, resolution injection skipped");
        return 0;
    }

    addr = (DWORD)(uintptr_t)&ws_enum_cb;
    memcpy(patch, &addr, 4);
    if (!ws_write(RVA_ENUM_CB_IMM, patch, 4)) return 0;

    ws_log("modes: enumeration callback replaced");
    return 1;
}

/* public wrapper so the lookup hook can repopulate an empty table */
void ws_inject_modes_public(void)
{
    if (g_active) ws_inject_modes();
}
