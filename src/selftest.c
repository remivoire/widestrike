/*
 * WideStrike - offline self test hook
 *
 * Exported for tools/host_test.c only: applies every patch to a given
 * image base so the whole patch path can be exercised against a mapped
 * copy of jsf.exe without ever running the game.
 */
#include "wstrike.h"

int ws_test_apply(void *image_base)
{
    uintptr_t save_base   = g_base;
    int       save_active = g_active;
    int       applied     = 0;

    if (!image_base) return -1;

    g_base   = (uintptr_t)image_base;
    g_active = 1;
    g_cfg.enabled = 1;
    g_cfg.log     = 1;
    g_cfg.fov     = 1;
    g_cfg.modes   = 1;
    g_cfg.clip_guard = 1;
    g_cfg.force_software = 1;

    ws_log_init();

    if (ws_patch_force_software()) applied |= 1;
    if (ws_patch_modes())          applied |= 2;
    if (ws_patch_mode_lookup())    applied |= 0x80;
    if (ws_patch_mode_count())     applied |= 0x100;
    if (ws_patch_span_guards())    applied |= 0x200;
    if (ws_patch_clip_guards() == CLIP_GUARD_COUNT) applied |= 4;
    if (ws_patch_projection())     applied |= 8;
    if (ws_patch_cd_check())       applied |= 0x10;
    if (ws_patch_windowed())       applied |= 0x20;
    if (ws_patch_ddraw_hook())     applied |= 0x40;
    if (ws_patch_screengrab())     applied |= 0x400;
    g_base   = save_base;
    g_active = save_active;
    return applied;
}
