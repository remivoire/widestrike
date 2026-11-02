/*
 * WideStrike - safe code patching helpers
 */
#include "wstrike.h"

int ws_verify_bytes(uintptr_t rva, const void *expect, size_t len)
{
    const BYTE *p;
    if (!g_active || !g_base) return 0;
    p = (const BYTE *)(g_base + rva);
    return memcmp(p, expect, len) == 0;
}

static int ws_make_writable(void *addr, size_t len, DWORD *old)
{
    DWORD tmp;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, old ? old : &tmp))
        return 0;
    return 1;
}

int ws_write(uintptr_t rva, const void *data, size_t len)
{
    void  *p;
    DWORD  old = 0;

    if (!g_active || !g_base) return 0;
    p = (void *)(g_base + rva);
    if (!ws_make_writable(p, len, &old)) {
        ws_log("!! VirtualProtect failed at %08X (err %lu)", (unsigned)(g_base + rva), GetLastError());
        return 0;
    }
    memcpy(p, data, len);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    VirtualProtect(p, len, old, &old);
    return 1;
}

int ws_nop(uintptr_t rva, size_t len)
{
    BYTE buf[16];
    if (len > sizeof(buf)) return 0;
    memset(buf, 0x90, len);
    return ws_write(rva, buf, len);
}

int ws_screen_w(void)
{
    typedef int (*fn_t)(void);
    return ((fn_t)(g_base + RVA_GET_W))();
}

int ws_screen_h(void)
{
    typedef int (*fn_t)(void);
    return ((fn_t)(g_base + RVA_GET_H))();
}

/* the resolution WideStrike wants the engine to render at */
void ws_render_size(int *w, int *h)
{
    *w = g_cfg.render_w > 0 ? g_cfg.render_w : GetSystemMetrics(SM_CXSCREEN);
    *h = g_cfg.render_h > 0 ? g_cfg.render_h : GetSystemMetrics(SM_CYSCREEN);
}
