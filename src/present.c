/*
 * WideStrike - modern display path + CD check
 *
 * ---------------------------------------------------------------------
 * 1. CD check
 * ---------------------------------------------------------------------
 * WinMain does:
 *
 *     while (sub_417490() == -1) { ... "insert the CD" ... }
 *
 * sub_417490 walks A:..Z:, tests GetDriveTypeA() == DRIVE_CDROM and compares
 * the volume label with the install label.  On a download/HHD install there is
 * no such volume, so the game never gets past the dialog.  We make the probe
 * report success.
 *
 * ---------------------------------------------------------------------
 * 2. Display: 16 bpp is gone
 * ---------------------------------------------------------------------
 * The engine calls SetDisplayMode(w, h, 16) and lets DirectDraw hand it a
 * 16 bpp back buffer; the software rasteriser writes 16 bit pixels straight
 * into the locked surface.  Since Windows 8 the display drivers no longer
 * expose 16 bpp modes at all (ChangeDisplaySettingsEx(CDS_TEST) rejects every
 * one of them), so SetDisplayMode fails -> "Could not set display mode!".
 *
 * We keep the renderer untouched and change only the presentation:
 *
 *   - run DirectDraw in windowed mode (DDSCL_NORMAL): no mode switch at all
 *   - the window itself is already created borderless, desktop sized
 *   - the back buffer is created as an explicit RGB565 offscreen, which is
 *     exactly the format the rasteriser wants (565: r5 g6 b5)
 *   - the engine's existing windowed present path does
 *         Blt(primary, x, y, backbuffer, &rect, DDBLT_WAIT)
 *     from the 565 offscreen to the desktop format primary, and DirectDraw
 *     converts 565 -> 32 bpp for us
 *
 * Result: borderless full screen at the desktop resolution, correct colours,
 * no display mode change.  Verified entry points:
 *   sub_4AA5E9  mov eax, 1                -> dword_C3D704 (windowed flag)
 *   sub_4B0F27  cmp eax, 10h / jnz       -> "screenmode must be 16-bit"
 *   0x11D343C   IAT slot for DirectDrawCreate
 *   vtbl[6]     IDirectDraw::CreateSurface
 */
#include "wstrike.h"

/* DDSURFACEDESC layout (v1, 108 bytes) */
#define DDSD_CAPS        0x00000001u
#define DDSD_HEIGHT      0x00000002u
#define DDSD_WIDTH       0x00000004u
#define DDSD_PIXELFORMAT 0x00001000u
#define DDPF_RGB         0x00000040u

#define DDSD_OFF_HEIGHT  2
#define DDSD_OFF_WIDTH   3
#define DDSD_OFF_FLAGS   1
#define DDPF_OFF         18          /* 72 / 4 */

#define VTBL_INDEX_CREATE_SURFACE 6
#define VTBL_INDEX_BLT            5
#define VTBL_INDEX_BLTFAST        7

typedef HRESULT (__stdcall *ddcreate_t)(void *guid, void **dd, void *unk);
typedef HRESULT (__stdcall *createsurf_t)(void *self, DWORD *desc, void **surf, void *unused);
typedef HRESULT (__stdcall *blt_t)(void *self, RECT *dst, void *src, RECT *srcrc, DWORD flags, void *fx);
typedef HRESULT (__stdcall *bltfast_t)(void *self, DWORD x, DWORD y, void *src, RECT *srcrc, DWORD trans);

static ddcreate_t      g_ddcreate;
static createsurf_t    g_createsurface;
static bltfast_t       g_bltfast;
static void           *g_primary;
static int             g_hooked_dd;
static int             g_hooked_bltfast;
static int             g_blt_count;
static int             g_borderless_done;


/* --------------------------------------------------------------- 565 */

static void ws_set_565_format(void)
{
    /* same encoding the 3dfx path installs: r5 @11, g6 @5, b5 @0
     * dword_C41928 = { r_bits, g_bits, b_bits, r_shift }
     * dword_C4192C = { g_shift, b_shift }                              */
    DWORD masks  = (5u) | (6u << 8) | (5u << 16) | (11u << 24);  /* 0x0B050605 */
    DWORD shifts = (5u) | (0u << 8);                             /* 5          */

    if (!g_cfg.fmt565) {
        ws_log("disp : RGB565 format globals left alone (fmt565=0)");
        return;
    }

    ws_write(RVA_FMT_MASKS, &masks, 4);
    ws_write(RVA_FMT_SHIFTS, &shifts, 4);
}

static int ws_force_565(DWORD *d)
{
    if (!d) return 0;
    if (!(d[DDSD_OFF_FLAGS] & DDSD_HEIGHT) || !(d[DDSD_OFF_FLAGS] & DDSD_WIDTH))
        return 0;
    if (d[DDSD_OFF_WIDTH] < 320 || d[DDSD_OFF_HEIGHT] < 200)
        return 0;                       /* not a real render target */
    if (d[DDSD_OFF_FLAGS] & DDSD_PIXELFORMAT)
        return 0;                       /* caller already picked a format */

    d[DDSD_OFF_FLAGS] |= DDSD_PIXELFORMAT;
    d[DDPF_OFF + 0] = 32;               /* dwSize   */
    d[DDPF_OFF + 1] = DDPF_RGB;         /* dwFlags  */
    d[DDPF_OFF + 2] = 0;                /* dwFourCC */
    d[DDPF_OFF + 3] = 16;               /* dwRGBBitCount */
    d[DDPF_OFF + 4] = 0xF800;           /* red mask   */
    d[DDPF_OFF + 5] = 0x07E0;           /* green mask */
    d[DDPF_OFF + 6] = 0x001F;           /* blue mask  */
    d[DDPF_OFF + 7] = 0;                /* alpha mask */
    return 1;
}

/* ------------------------------------------------------- surface hook */

/*
 * The engine's window is created with WS_CAPTION (style 0x14C00000) and ends
 * up 3,26 - 1914x980 inside a 1920x1080 desktop.  Turn it into a real
 * borderless full-screen window the first time we present.
 */
static void ws_make_borderless(HWND hwnd)
{
    int sw, sh;

    if (!hwnd) return;
    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);

    if (GetWindowLongA(hwnd, GWL_STYLE) & WS_CAPTION) {
        SetWindowLongA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        ws_log("disp : window style -> WS_POPUP (borderless)");
    }
    SetWindowPos(hwnd, HWND_TOP, 0, 0, sw, sh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

/*
 * The engine presents with BltFast (vtable slot 7):
 *
 *     BltFast(x, y, backbuffer, &srcRect, DDBLTFAST_WAIT)
 *
 * BltFast is a straight copy - it cannot convert pixel formats, so a 565
 * back buffer copied onto a 32 bpp desktop primary draws nothing (black
 * screen with working audio).  Reroute only that call through Blt, which
 * does go through the colour conversion path.
 */
static HRESULT __stdcall ws_bltfast(void *self, DWORD x, DWORD y, void *src, RECT *srcrc, DWORD trans)
{
    void **vt;
    blt_t  blt;
    RECT   dst, cr;
    POINT  pt;
    HRESULT hr;
    HWND   hwnd;
    int    sw = GetSystemMetrics(SM_CXSCREEN);
    int    sh = GetSystemMetrics(SM_CYSCREEN);

    if (self != g_primary || !src || !g_bltfast)
        return g_bltfast(self, x, y, src, srcrc, trans);

    /* present=0 passes the engine's own BltFast through untouched (diagnostic):
     * the frame then lands unscaled in the top-left corner. */
    if (!g_cfg.present)
        return g_bltfast(self, x, y, src, srcrc, trans);

    vt  = *(void ***)self;
    blt = (blt_t)vt[VTBL_INDEX_BLT];
    if (!blt)
        return g_bltfast(self, x, y, src, srcrc, trans);

    /* The destination must stay inside the window's client area and inside
     * the primary surface, otherwise Blt returns DDERR_INVALIDRECT and
     * nothing is drawn (black screen with working audio). */
    hwnd = *(HWND *)(g_base + RVA_MAIN_HWND);

    if (hwnd && !g_borderless_done) {
        g_borderless_done = 1;
        ws_make_borderless(hwnd);
    }
    if (hwnd && GetClientRect(hwnd, &cr) && cr.right > 0 && cr.bottom > 0) {
        pt.x = 0; pt.y = 0;
        ClientToScreen(hwnd, &pt);
        dst.left   = pt.x;
        dst.top    = pt.y;
        dst.right  = pt.x + cr.right;
        dst.bottom = pt.y + cr.bottom;
    } else {
        dst.left   = (LONG)x;
        dst.top    = (LONG)y;
        dst.right  = (LONG)x + sw;
        dst.bottom = (LONG)y + sh;
    }

    if (dst.right  > sw) dst.right  = sw;
    if (dst.bottom > sh) dst.bottom = sh;

    /* aspect=2 pillarbox/letterbox the 4:3 frame;
     * aspect=3 integer scale (pixel-perfect, black bars all round) */
    if ((g_cfg.aspect == 2 || g_cfg.aspect == 3) && srcrc && cr.right > 0 && cr.bottom > 0) {
        int srcw = srcrc->right - srcrc->left;
        int srch = srcrc->bottom - srcrc->top;
        int cw = cr.right, ch = cr.bottom, dw, dh;

        if (srcw > 0 && srch > 0) {
            if (g_cfg.aspect == 3) {
                int n = cw / srcw, m = ch / srch;
                if (m < n) n = m;
                if (n < 1) n = 1;
                dw = srcw * n;
                dh = srch * n;
            } else if ((__int64)cw * srch > (__int64)ch * srcw) {
                dh = ch; dw = (int)((__int64)ch * srcw / srch);
            } else {
                dw = cw; dh = (int)((__int64)cw * srch / srcw);
            }
            dst.left   = pt.x + (cw - dw) / 2;
            dst.top    = pt.y + (ch - dh) / 2;
            dst.right  = dst.left + dw;
            dst.bottom = dst.top + dh;

            {
                struct { DWORD dwSize; DWORD pad[19]; DWORD dwFillColor; DWORD pad2[8]; } fx;
                RECT    all;
                memset(&fx, 0, sizeof(fx));
                fx.dwSize = sizeof(fx);
                fx.dwFillColor = 0;
                all.left = pt.x; all.top = pt.y;
                all.right = pt.x + cw; all.bottom = pt.y + ch;
                blt(self, &all, NULL, NULL, 0x00000400u | 0x01000000u, &fx); /* COLORFILL|WAIT */
            }
        }
    }

    if (dst.right <= dst.left || dst.bottom <= dst.top)
        return g_bltfast(self, x, y, src, srcrc, trans);

    hr = blt(self, &dst, src, srcrc, 0x01000000u, NULL);

    /* fall back to the engine's own 1:1 placement (no scaling) */
    if (FAILED(hr)) {
        RECT one;
        one.left = (LONG)x;
        one.top  = (LONG)y;
        one.right  = (LONG)x + (srcrc ? (srcrc->right - srcrc->left) : sw);
        one.bottom = (LONG)y + (srcrc ? (srcrc->bottom - srcrc->top) : sh);
        if (one.right  > sw) one.right  = sw;
        if (one.bottom > sh) one.bottom = sh;
        if (one.right > one.left && one.bottom > one.top)
            hr = blt(self, &one, src, srcrc, 0x01000000u, NULL);
    }

    if (SUCCEEDED(hr)) {
        if (g_blt_count < 20) {
            ws_log("disp : present #%d %ldx%ld -> (%ld,%ld)-(%ld,%ld) hr=0",
                   g_blt_count,
                   srcrc ? (long)(srcrc->right - srcrc->left) : -1L,
                   srcrc ? (long)(srcrc->bottom - srcrc->top) : -1L,
                   (long)dst.left, (long)dst.top, (long)dst.right, (long)dst.bottom);
        }
        g_blt_count++;
        return hr;
    }

    if (g_blt_count < 20) {
        ws_log("!! Blt failed #%d hr=%08X (dst %ld,%ld-%ld,%ld, client %ldx%ld)",
               g_blt_count, (unsigned)hr, (long)dst.left, (long)dst.top,
               (long)dst.right, (long)dst.bottom, (long)cr.right, (long)cr.bottom);
    }
    g_blt_count++;
    return g_bltfast(self, x, y, src, srcrc, trans);
}

static void ws_hook_surface_vtable(void *surf)
{
    void **vt = *(void ***)surf;
    DWORD  old;

    if (!vt || g_hooked_bltfast) return;
    g_bltfast = (bltfast_t)vt[VTBL_INDEX_BLTFAST];
    if (!g_bltfast) return;

    if (!VirtualProtect(&vt[VTBL_INDEX_BLTFAST], sizeof(void *),
                        PAGE_EXECUTE_READWRITE, &old)) {
        ws_log("!! cannot write surface vtable (%lu)", GetLastError());
        return;
    }
    vt[VTBL_INDEX_BLTFAST] = (void *)ws_bltfast;
    VirtualProtect(&vt[VTBL_INDEX_BLTFAST], sizeof(void *), old, &old);

    g_hooked_bltfast = 1;
    ws_log("disp : BltFast hooked (565 -> desktop conversion via Blt)");
}

static HRESULT __stdcall ws_create_surface(void *self, DWORD *desc, void **surf, void *unused)
{
    HRESULT hr;
    DWORD   flags = desc ? desc[DDSD_OFF_FLAGS] : 0;
    DWORD   caps  = desc ? desc[26] : 0;
    DWORD   w     = desc ? desc[DDSD_OFF_WIDTH]  : 0;
    DWORD   h     = desc ? desc[DDSD_OFF_HEIGHT] : 0;

    if (desc && (flags & DDSD_CAPS) && (caps & 0x200u)) {   /* PRIMARYSURFACE */
        hr = g_createsurface(self, desc, surf, unused);
        if (SUCCEEDED(hr) && surf && *surf) {
            g_primary = *surf;
            ws_log("disp : primary %p (%08X/%lu)", *surf, (unsigned)hr, GetLastError());
            ws_hook_surface_vtable(*surf);
        } else {
            ws_log("disp : primary create failed hr=%08X", (unsigned)hr);
        }
        return hr;
    }

    /* an out-of-range nCurrentScreenMode makes the engine ask for a 0x0
     * surface, which DirectDraw rejects -> "Could not create off-screen
     * surface!".  Substitute the desktop size instead of failing. */
    if (desc && (flags & DDSD_WIDTH) && (flags & DDSD_HEIGHT) &&
        (!w || !h)) {
        w = (DWORD)GetSystemMetrics(SM_CXSCREEN);
        h = (DWORD)GetSystemMetrics(SM_CYSCREEN);
        desc[DDSD_OFF_WIDTH]  = w;
        desc[DDSD_OFF_HEIGHT] = h;
        ws_log("disp : zero-size surface requested -> %ux%u", w, h);
    }

    ws_force_565(desc);

    hr = g_createsurface(self, desc, surf, unused);

    ws_log("disp : create %ux%u flags=%08X caps=%08X -> hr=%08X%s",
           w, h, (unsigned)flags, (unsigned)caps, (unsigned)hr,
           (desc && (desc[DDSD_OFF_FLAGS] & DDSD_PIXELFORMAT) &&
            !(flags & DDSD_PIXELFORMAT)) ? " [565]" : "");

    return hr;
}

static void ws_hook_dd_vtable(void *dd)
{
    void **vt = *(void ***)dd;
    DWORD  old;

    if (!vt || g_hooked_dd) return;
    g_createsurface = (createsurf_t)vt[VTBL_INDEX_CREATE_SURFACE];
    if (!g_createsurface) return;

    if (!VirtualProtect(&vt[VTBL_INDEX_CREATE_SURFACE], sizeof(void *),
                        PAGE_EXECUTE_READWRITE, &old)) {
        ws_log("!! cannot write IDirectDraw vtable (%lu)", GetLastError());
        return;
    }
    vt[VTBL_INDEX_CREATE_SURFACE] = (void *)ws_create_surface;
    VirtualProtect(&vt[VTBL_INDEX_CREATE_SURFACE], sizeof(void *), old, &old);

    g_hooked_dd = 1;
    ws_log("disp : IDirectDraw::CreateSurface hooked (forcing RGB565)");
}

static HRESULT __stdcall ws_ddcreate(void *guid, void **dd, void *unk)
{
    HRESULT hr = g_ddcreate(guid, dd, unk);

    /* the game window exists by now (WinMain creates it before display
     * init), so make it borderless before DirectDraw is associated with it */
    if (!g_borderless_done) {
        g_borderless_done = 1;
        ws_make_borderless(*(HWND *)(g_base + RVA_MAIN_HWND));
    }
    if (SUCCEEDED(hr) && dd && *dd) ws_hook_dd_vtable(*dd);
    return hr;
}

/* ------------------------------------------------------------- patches */

int ws_patch_cd_check(void)
{
    static const BYTE expect[1] = { 0x55 };     /* push ebp */
    static const BYTE stub[3]   = { 0x33, 0xC0, 0xC3 };  /* xor eax,eax ; ret */

    if (!g_cfg.cd_check) {
        ws_log("cd   : check left in place");
        return 0;
    }
    if (!ws_verify_bytes(RVA_CD_CHECK, expect, 1)) {
        ws_log("!! CD probe mismatch, skipped");
        return 0;
    }
    if (!ws_write(RVA_CD_CHECK, stub, 3)) return 0;
    ws_log("cd   : volume label probe forced to success");
    return 1;
}

int ws_patch_windowed(void)
{
    static const BYTE win_flag[5] = { 0xB8, 0x01, 0x00, 0x00, 0x00 }; /* mov eax,1 */
    DWORD zero = 0;

    if (!g_cfg.windowed) {
        ws_log("disp : staying in exclusive fullscreen");
        return 0;
    }
    if (!ws_verify_bytes(RVA_WINDOW_FLAG_IMM - 1, win_flag, 5)) {
        ws_log("!! window flag site mismatch, display patch skipped");
        return 0;
    }
    if (!ws_write(RVA_WINDOW_FLAG_IMM, &zero, 4)) return 0;   /* mov eax, 0 */

    /* "To run in windowed mode, screenmode must be 16-bit!" -> never taken */
    if (ws_verify_bytes(RVA_WINED_16BPP_JNZ, "\x0F\x85", 2) &&
        ws_nop(RVA_WINED_16BPP_JNZ, 6))
        ws_log("disp : windowed path enabled (no SetDisplayMode, 16bpp check removed)");
    else
        ws_log("!! windowed 16bpp guard mismatch (continuing)");

    /* sub_4B1230 stores its first argument into dword_C41904, which selects
     * the SetDisplayMode branch of sub_4B0C70.  The game re-enters mode setup
     * later with a non-zero argument and would try to switch the display mode
     * again; keep the flag pinned at 0 (windowed) instead. */
    if (ws_verify_bytes(RVA_FLAG_STORE, "\xA3", 1) &&
        ws_nop(RVA_FLAG_STORE, 5))
        ws_log("disp : dword_C41904 pinned to 0 (windowed is sticky)");
    else
        ws_log("!! fullscreen-flag store mismatch (continuing)");

    return 1;
}

int ws_patch_ddraw_hook(void)
{
    void *cur;
    void *mine = (void *)ws_ddcreate;

    if (!g_cfg.windowed) return 0;

    cur = *(void **)(g_base + RVA_DDCREATE_IAT);
    if (!cur) {
        ws_log("!! DirectDrawCreate IAT slot empty, skipped");
        return 0;
    }
    g_ddcreate = (ddcreate_t)cur;

    if (!ws_write(RVA_DDCREATE_IAT, &mine, sizeof(void *)))
        return 0;

    ws_log("disp : DirectDrawCreate intercepted");
    ws_set_565_format();
    return 1;
}
