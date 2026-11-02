/*
 * WideStrike - projected coordinate guard fix + renderer lock
 *
 * Several projection routines validate a projected vertex with
 *
 *        t = v; if (t < 0) t = -t;
 *        if (t > 0x4000) vertex = invalid;
 *
 * where the comparison is a *signed* jump (jg).  The value comes from
 *
 *        fistp  -> int     after  v / depth
 *
 * so a vertex that lands on the camera plane produces -inf/+inf/nan,
 * fistp turns that into 0x80000000 (INT_MIN) and "neg" leaves INT_MIN
 * unchanged.  INT_MIN compares as a negative number, so the guard lets
 * it through and a garbage screen coordinate reaches the span filler.
 *
 * Switching those two instructions to an unsigned compare (jg -> ja)
 * rejects INT_MIN and keeps every legitimate value unchanged.
 */
#include "wstrike.h"

int ws_patch_clip_guards(void)
{
    static const uintptr_t sites[CLIP_GUARD_COUNT] = CLIP_GUARD_IMM_RVAS;
    int i, patched = 0;

    if (!g_cfg.clip_guard) {
        ws_log("clip : disabled by config");
        return 0;
    }

    for (i = 0; i < CLIP_GUARD_COUNT; i++) {
        uintptr_t rva = sites[i];
        const BYTE *p  = (const BYTE *)(g_base + rva);

        /* expect: 81 /7  imm32 0F 8F  -> imm32 starts at rva, jg at rva+4 */
        if (p[-2] != 0x81 || ((p[-1] >> 3) & 7) != 7 ||
            p[4] != 0x0F || p[5] != 0x8F || p[0] != 0x00 ||
            p[1] != 0x40 || p[2] != 0x00 || p[3] != 0x00) {
            ws_log("!! clip guard %08X pattern mismatch, skipped", (unsigned)(g_base + rva));
            continue;
        }
        {
            BYTE ja = 0x87;   /* jg -> ja (unsigned) */
            if (ws_write(rva + 5, &ja, 1)) patched++;
        }
    }
    ws_log("clip : %d/%d projected-coordinate guards hardened", patched, CLIP_GUARD_COUNT);
    return patched;
}

/*
 * The 3dfx path is useless with a stub glide2x.dll, and selecting it
 * makes the game exit(1) with "UNABLE TO OPEN 3DFX ...".  Turn the
 * render-mode branch into a nop so 3dfx values in an old SESSION.FLP
 * fall back to the software renderer instead of killing the game.
 */
int ws_patch_force_software(void)
{
    if (!g_cfg.force_software) return 0;

    if (!ws_verify_bytes(RVA_FORCE_SW_JNZ, "\x0F\x85", 2)) {
        ws_log("!! render-mode branch mismatch, force_software skipped");
        return 0;
    }
    if (!ws_nop(RVA_FORCE_SW_JNZ, 6)) return 0;

    ws_log("mode : 3dfx renderer disabled (software only)");
    return 1;
}

/*
 * sub_42BD80 - "restore the back buffer from the primary".
 *
 * It is called immediately after PresentFrame (0x430AB4, 0x430EF0, 0x4310FB,
 * 0x43194F) and copies 2*GetScreenWidth() bytes per row from the locked
 * primary into the locked back buffer, for GetScreenHeight() rows.
 *
 * That is an identity copy only while primary and back buffer are the same
 * surface size, i.e. in the original 640x480 fullscreen flipping mode, where
 * a Flip leaves the back buffer holding the old front buffer.
 *
 * WideStrike never flips: it presents with Blt from a 640x480 RGB565 offscreen
 * onto a desktop-sized primary.  The back buffer already holds the frame, and
 * the grab instead copies the top-left 640x480 of the stretched desktop image
 * back over it - which is a 3x magnified crop of the menu background.  The
 * first subsequent present then pushes that crop to the screen, which is the
 * "menu turns into a blue blocky mess when the mouse moves" bug.
 *
 * Nothing in the windowed path needs the grab, so return immediately.
 */
int ws_patch_screengrab(void)
{
    const BYTE *p = (const BYTE *)(g_base + RVA_SCREENGRAB);

    /* expect the standard frame-pointer prologue: 55 8B EC 83 EC 28 57 57 */
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        ws_log("!! screen-grab prologue mismatch, screengrab skipped");
        return 0;
    }
    if (!ws_verify_bytes(RVA_SCREENGRAB, "\x55", 1)) return 0;

    {
        BYTE ret = 0xC3;
        if (!ws_write(RVA_SCREENGRAB, &ret, 1)) return 0;
    }
    ws_log("disp : primary->backbuffer grab disabled (windowed present)");
    return 1;
}
