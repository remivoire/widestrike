/*
 * WideStrike - entry point
 *
 * Shipped as glide2x.dll: jsf.exe imports that library statically, so the
 * Windows loader maps us into the process before any game code runs and
 * we can patch the image in place.
 */
#include "wstrike.h"

uintptr_t g_base   = 0;
int       g_active = 0;

static const char *ws_basename(const char *path)
{
    const char *p = path, *last = path;
    while (*p) { if (*p == '\\' || *p == '/') last = p + 1; p++; }
    return last;
}

static void ws_dir_of(const char *path, char *out, size_t n)
{
    size_t i = strlen(path);
    if (i >= n) i = n - 1;
    memcpy(out, path, i);
    out[i] = 0;
    while (i && out[i - 1] != '\\' && out[i - 1] != '/') out[--i] = 0;
    if (i) out[i - 1] = 0;
}

static int ws_image_ok(const char *exe_path)
{
    HANDLE        f;
    LARGE_INTEGER size;

    f = CreateFileA(exe_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        ws_log("!! cannot open %s", exe_path);
        return 0;
    }
    if (!GetFileSizeEx(f, &size)) {
        CloseHandle(f);
        ws_log("!! cannot size %s", exe_path);
        return 0;
    }
    CloseHandle(f);

    if (size.QuadPart != (LONGLONG)WS_TARGET_SIZE) {
        ws_log("!! unexpected jsf.exe size %I64d (expected %u)", size.QuadPart, WS_TARGET_SIZE);
        return 0;
    }
    return 1;
}

static void ws_bootstrap(void)
{
    char exe[MAX_PATH];
    char dir[MAX_PATH];

    GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (_stricmp(ws_basename(exe), WS_TARGET_EXE) != 0)
        return;                                  /* loaded by some other host */

    g_base = (uintptr_t)GetModuleHandleA(NULL);
    if (!g_base) return;

    ws_dir_of(exe, dir, sizeof(dir));
    ws_config_load(dir);
    ws_log_init();

    ws_log("target  : %s", exe);
    ws_log("%s %s", WS_NAME, WS_VERSION);

    if (!g_cfg.enabled) {
        ws_log("state  : disabled by wstrike.ini, nothing patched");
        return;
    }
    if (!ws_image_ok(exe)) return;

    g_active = 1;

    ws_patch_cd_check();
    ws_patch_windowed();
    ws_patch_ddraw_hook();
    ws_patch_force_software();
    ws_patch_modes();
    ws_patch_mode_lookup();
    ws_patch_mode_count();
    if (g_cfg.harden) ws_patch_span_guards(); else ws_log("harden: span guards disabled by config");
    ws_patch_clip_guards();
    ws_patch_screengrab();
    ws_patch_projection();

    ws_log("hud    : left untouched - HUD/menu art is drawn at native pixel");
    ws_log("         size from four pre-rendered bitmap fonts chosen by box");
    ws_log("         size (FONT18X2/12X1/8X9/6X8), so it stays pixel sharp.");
    ws_log("state  : active");
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        ws_bootstrap();
    } else if (reason == DLL_PROCESS_DETACH) {
        ws_log_flush();
    }
    return TRUE;
}

/* called from the first frame so the mode table is complete by then */
void ws_on_first_frame(void);
void ws_on_first_frame(void)
{
    static int done = 0;
    if (done || !g_active) return;
    done = 1;
    ws_log_mode_table();
}
