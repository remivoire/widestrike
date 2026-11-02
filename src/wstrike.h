/*
 * WideStrike - widescreen + stability patch for Joint Strike Fighter (1997)
 * src/wstrike.h
 */
#ifndef WSTRIKE_H
#define WSTRIKE_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WS_NAME        "WideStrike"
#define WS_VERSION     "1.0.1"
#define WS_TARGET_EXE  "jsf.exe"
#define WS_TARGET_SIZE 1072128u
#define WS_IMAGE_BASE  0x400000u

/* ------------------------------------------------------------------ */
/* Image offsets (RVA = address - 0x400000, all verified against      */
/* jsf.exe SHA-256 c05392396b6b5b2fc83e0cbda4a71becdcd6c5689fb682b469756b845065a4e0) */
/* ------------------------------------------------------------------ */
#define RVA_GET_W            0xB1CF0u   /* int  view_setup_get_width(void)     */
#define RVA_GET_H            0xB1D40u   /* int  view_setup_get_height(void)    */
#define RVA_GET_BUF          0xB1CA0u   /* void *backbuffer data pointer       */
#define RVA_GET_PITCH        0xB1CC0u   /* int  backbuffer pitch in pixels     */
#define RVA_ROWPTR           0xD77E30u  /* void *dword_1177E30[2048]           */

#define RVA_CALL_PROJECTION  0x1BFE2u   /* call sub_41B8A0 @ 0x41BFE2          */
#define RVA_FN_PROJECTION    0x1B8A0u   /* sub_41B8A0 - perspective setup      */

#define RVA_ENUM_CB_IMM      0xB16DDu   /* mov eax, offset sub_4B0B50 (+1)     */
#define RVA_FN_ENUM_CB       0xB0B50u   /* sub_4B0B50 - DDraw mode callback    */

#define RVA_FORCE_SW_JNZ     0x19C08u   /* jnz 3dfx-mode @ 0x419C08            */
#define RVA_SCREENGRAB       0x2BD80u   /* sub_42BD80 - primary->back grab     */

/* --- modern display path (src/present.c) --------------------------- */
#define RVA_CD_CHECK         0x17490u   /* sub_417490 - CD volume probe        */
#define RVA_WINDOW_FLAG_IMM  0xAA5EAu   /* mov eax,1 -> dword_C3D704           */
#define RVA_WINED_16BPP_JNZ  0xB0F2Cu   /* "screenmode must be 16-bit"         */
#define RVA_DDCREATE_IAT     0xDD343Cu  /* IAT slot of DirectDrawCreate        */
#define RVA_FLAG_STORE       0xB126Eu   /* mov dword_C41904, eax (fullscreen)  */
#define RVA_MAIN_HWND        0x823648u  /* dword_C23648 - the game's window    */
#define RVA_FMT_MASKS        0x841928u  /* dword_C41928 - r/g/b bits + r shift */
#define RVA_FMT_SHIFTS       0x84192Cu  /* dword_C4192C - g shift, b shift     */

#define RVA_CAM_PTR          0xE86A0u   /* pointer to camera/view struct       */
#define CAM_FSCALE_X         0x38
#define CAM_FSCALE_Y         0x3C

#define RVA_MODE_COUNT       0x841700u  /* int  dword_C41700[0]                */
#define RVA_MODE_TABLE       0x841704u  /* {int w,h} dword_C41704[]            */
#define RVA_MODE_LOOKUP      0xB18A0u   /* sub_4B18A0 (w,h) -> index, -1 if none */
#define RVA_MODE_COUNT_GET   0xB1890u   /* sub_4B1890 -> dword_C41700          */
#define MODE_TABLE_MAX       64         /* 0x200 bytes / 8                     */
#define RVA_MODE_INDEX       0x84190Cu  /* int  nCurrentScreenMode             */

#define RVA_DBL_HTAN         0xD77DA0u  /* double tan(hfov/2)  (v8)            */
#define RVA_DBL_VTAN         0xD77DA8u  /* double tan(vfov/2)  (v9)            */
#define RVA_DBL_XSCALE       0xD77DB0u  /* double halfw / htan                 */
#define RVA_DBL_YSCALE       0xD77DB8u  /* double halfh / vtan                 */
#define RVA_FLT_HTAN         0xD77D98u  /* float  -" -                          */
#define RVA_FLT_VTAN         0xD77D9Cu
#define RVA_FLT_XSCALE       0xD77D90u
#define RVA_FLT_YSCALE       0xD77D94u
#define RVA_HALF_W           0xD77D80u  /* int centre x (= half viewport width)*/
#define RVA_HALF_H           0xD77D84u  /* int centre y (= half viewport height)*/

/* projected coordinate sanity guards:  cmp r32,4000h ; jg reject
 * the sign of the compared value is "abs(v)" produced by a preceding
 * test/neg pair, so the check must be unsigned or INT_MIN slips through */
#define CLIP_GUARD_COUNT     6
#define CLIP_GUARD_IMM_RVAS  { 0x4889Cu, 0x488B2u, 0x5032Du, \
                               0x50345u, 0x50E7Fu, 0x50E99u }

/* ------------------------------------------------------------------ */
/* config                                                              */
/* ------------------------------------------------------------------ */
typedef struct ws_config {
    int enabled;
    int log;
    int fov;              /* 0 = off, 1 = hor+ (default), 2 = vert- */
    int modes;            /* 1 = inject extra display modes        */
    int max_width;        /* 0 = use desktop width                 */
    int max_height;       /* 0 = use desktop height                */
    int clip_guard;       /* 1 = patch signed->unsigned guards     */
    int force_software;   /* 1 = never allow the 3dfx renderer     */
    int test_16bpp;       /* 1 = CDS_TEST each injected mode       */
    int cd_check;         /* 1 = bypass the CD volume label probe  */
    int windowed;         /* 1 = borderless 16bpp->32bpp present   */
    int force_desktop;    /* 1 = render at desktop resolution      */
    int harden;           /* 1 = row table margin (crash fix)      */
    int present;          /* 1 = scaler blit, 0 = engine BltFast   */
    int fmt565;           /* 1 = install RGB565 format globals     */
    int aspect;           /* 1 = stretch to window, 2 = pillarbox  */
    int render_w;         /* 0 = desktop, else forced render width */
    int render_h;         /* 0 = desktop, else forced render height*/
    char custom_modes[512];
} ws_config;

extern ws_config  g_cfg;
extern uintptr_t  g_base;          /* runtime base of jsf.exe */
extern int        g_active;        /* 1 when we are inside jsf.exe */

/* logging / config */
void ws_log_init(void);
void ws_log(const char *fmt, ...);
void ws_log_flush(void);
void ws_config_load(const char *dir);

/* helper accessors (game functions) */
int  ws_screen_w(void);
int  ws_screen_h(void);

/* memory patching */
int  ws_verify_bytes(uintptr_t rva, const void *expect, size_t len);
int  ws_write(uintptr_t rva, const void *data, size_t len);
int  ws_nop(uintptr_t rva, size_t len);

/* first rendered frame (mode table is complete by then) */
void ws_on_first_frame(void);

/* hook modules */
int  ws_patch_projection(void);
int  ws_patch_modes(void);
int  ws_patch_clip_guards(void);
int  ws_patch_screengrab(void);
int  ws_patch_force_software(void);
int  ws_patch_cd_check(void);
int  ws_patch_windowed(void);
int  ws_patch_ddraw_hook(void);
int  ws_patch_mode_lookup(void);
int  ws_patch_mode_count(void);
int  ws_patch_span_guards(void);
void ws_harden_row_table(void);
void ws_inject_modes_public(void);
void ws_render_size(int *w, int *h);
void ws_log_mode_table(void);

#endif /* WSTRIKE_H */
