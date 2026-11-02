/*
 * WideStrike - widescreen projection fix
 *
 * The engine computes, once per frame in sub_41B8A0 (0x41B8A0):
 *
 *     htan   = fov * pi/4096          // horizontal "tangent"  (v8)
 *     vtan   = 0.75 * htan            // 4:3 hard-coded ratio  (v9)
 *     xscale = halfw / htan           // stored at CAM+0x38
 *     yscale = halfh / vtan           // stored at CAM+0x3C
 *
 * 0.75 is 3/4, so at 4:3 both scales are identical.  At any other aspect
 * the horizontal scale is off by (r / (4/3)), which stretches the image.
 *
 * Fix: keep the vertical field of view and derive the horizontal tangent
 * from the real aspect ratio.  At 4:3 the result is identical to stock,
 * so the patch is a no-op on the original target resolution.
 */
#include "wstrike.h"

static int g_logged_once = 0;

static void ws_apply_aspect(void)
{
    uintptr_t b = g_base;
    int     buf_w, buf_h, out_w, out_h;
    double  ht, vt;
    int    *cam = *(int **)(b + RVA_CAM_PTR);
    int     xscale, yscale;

    if (g_cfg.fov == 0 || !cam) return;

    buf_w = ws_screen_w();
    buf_h = ws_screen_h();
    ht    = *(double *)(b + RVA_DBL_HTAN);
    vt    = *(double *)(b + RVA_DBL_VTAN);

    if (buf_w <= 0 || buf_h <= 0) return;
    if (ht <= 0.0 || vt <= 0.0 || ht > 100.0 || vt > 100.0) return;

    xscale = cam[CAM_FSCALE_X / 4];
    yscale = cam[CAM_FSCALE_Y / 4];
    if (xscale <= 0 || yscale <= 0) return;

    /* Aspect that matters for the geometry is the *output* aspect, because
     * the present blits the render buffer onto the whole window. */
    {
        HWND hwnd = *(HWND *)(b + RVA_MAIN_HWND);
        RECT cr;
        if (hwnd && GetClientRect(hwnd, &cr) && cr.right > 0 && cr.bottom > 0) {
            out_w = cr.right;
            out_h = cr.bottom;
        } else {
            out_w = GetSystemMetrics(SM_CXSCREEN);
            out_h = GetSystemMetrics(SM_CYSCREEN);
        }
    }

    /* stock 4:3 output -> leave everything alone (bit-identical behaviour) */
    if ((__int64)out_w * 3 == (__int64)out_h * 4) return;

    {
        /* The engine's projection is hard-wired to 4:3: it derives
         * vtan = 0.75 * htan, i.e. it assumes halfw/halfh == 4/3.  Whatever
         * the render buffer size, the correction that produces square pixels
         * on the final output is always (4/3) / output_aspect. */
        const double four_thirds = 4.0 / 3.0;
        double out_aspect = (double)out_w / (double)out_h;

        if (g_cfg.fov == 1) {
            double k   = four_thirds / out_aspect;      /* 0.75 at 16:9 */
            int    nxs = (int)((double)xscale * k);
            double nht = ht / k;                        /* wider hfov    */

            if (nxs <= 0 || nht <= 0.0 || nht > 8.0) return;

            cam[CAM_FSCALE_X / 4] = nxs;
            *(double *)(b + RVA_DBL_XSCALE) = (double)nxs;
            *(float  *)(b + RVA_FLT_XSCALE) = (float)nxs;
            *(double *)(b + RVA_DBL_HTAN) = nht;
            *(float  *)(b + RVA_FLT_HTAN) = (float)nht;

            if (!g_logged_once)
                ws_log("fov  : hor+ buf %dx%d out %dx%d  xscale %d->%d  htan %.5f->%.5f",
                       buf_w, buf_h, out_w, out_h, xscale, nxs, ht, nht);
        } else if (g_cfg.fov == 2) {
            double k   = out_aspect / four_thirds;
            int    nys = (int)((double)yscale * k);
            double nvt = vt / k;

            if (nys <= 0 || nvt <= 0.0 || nvt > 8.0) return;

            cam[CAM_FSCALE_Y / 4] = nys;
            *(double *)(b + RVA_DBL_YSCALE) = (double)nys;
            *(float  *)(b + RVA_FLT_YSCALE) = (float)nys;
            *(double *)(b + RVA_DBL_VTAN) = nvt;
            *(float  *)(b + RVA_FLT_VTAN) = (float)nvt;

            if (!g_logged_once)
                ws_log("fov  : vert- buf %dx%d out %dx%d  yscale %d->%d  vtan %.5f->%.5f",
                       buf_w, buf_h, out_w, out_h, yscale, nys, vt, nvt);
        }
    }

    g_logged_once = 1;
}

static int __cdecl ws_projection_hook(void)
{
    typedef int (*fn_t)(void);
    int r;

    if (!g_active) return 0;

    r = ((fn_t)(g_base + RVA_FN_PROJECTION))();
    ws_apply_aspect();
    ws_harden_row_table();
    ws_on_first_frame();
    return r;
}

int ws_patch_projection(void)
{
    static const BYTE expect[5] = { 0xE8, 0x49, 0xEF, 0xFF, 0xFF }; /* call 41B8A0 */
    DWORD rel;
    BYTE  patch[5];

    if (!ws_verify_bytes(RVA_CALL_PROJECTION, expect, sizeof(expect))) {
        ws_log("!! projection call site mismatch, fov patch skipped");
        return 0;
    }

    rel = (DWORD)((uintptr_t)&ws_projection_hook - (g_base + RVA_CALL_PROJECTION + 5));
    patch[0] = 0xE8;
    memcpy(patch + 1, &rel, 4);

    if (!ws_write(RVA_CALL_PROJECTION, patch, 5)) return 0;

    /* installed even with fov=0: the per-frame work (row-table hardening)
     * must not depend on the projection option */
    ws_log("fov  : per-frame hook installed (aspect mode %d)", g_cfg.fov);
    return 1;
}
