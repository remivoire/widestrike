/*
 * WideStrike - logging
 */
#include "wstrike.h"
#include <stdarg.h>

static FILE *g_log_fp = NULL;

static void ws_path_dir(const char *dir, char *out, size_t n, const char *file)
{
    size_t i = 0;
    out[0] = 0;
    if (dir && dir[0]) {
        for (; dir[i] && i + 1 < n; i++) out[i] = dir[i];
        if (i && out[i - 1] != '\\' && out[i - 1] != '/' && i + 1 < n) out[i++] = '\\';
    }
    out[i] = 0;
    if (i + strlen(file) + 1 < n) strcat(out, file);
}

void ws_log_init(void)
{
    char path[MAX_PATH];
    char exe[MAX_PATH];
    if (!g_cfg.log) return;

    GetModuleFileNameA(NULL, exe, sizeof(exe));
    {
        size_t i = strlen(exe);
        while (i && exe[i - 1] != '\\' && exe[i - 1] != '/') exe[--i] = 0;
    }
    ws_path_dir(exe, path, sizeof(path), "wstrike.log");
    g_log_fp = fopen(path, "w");
    if (!g_log_fp) return;

    ws_log("%s %s - %s", WS_NAME, WS_VERSION, __DATE__);
    ws_log("host dir : %s", exe);
    ws_log("base     : 0x%08X", (unsigned)g_base);
}

void ws_log(const char *fmt, ...)
{
    va_list ap;
    if (!g_log_fp) return;
    va_start(ap, fmt);
    vfprintf(g_log_fp, fmt, ap);
    va_end(ap);
    fputc('\n', g_log_fp);
    fflush(g_log_fp);          /* keep the file usable if the game dies */
}

void ws_log_flush(void)
{
    if (g_log_fp) { fflush(g_log_fp); fclose(g_log_fp); g_log_fp = NULL; }
}
