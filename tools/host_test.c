/*
 * host_test.c - offline validation harness for glide2x.dll (WideStrike).
 *
 *   host_test  <path to jsf.exe>  <path to glide2x.dll>
 *
 * 1. loads the DLL in a foreign host (DllMain must stay inert!!)
 * 2. resolves the decorated Glide exports jsf.exe imports
 * 3. maps jsf.exe with DONT_RESOLVE_DLL_REFERENCES (nothing executes
 *    no imports are resolved) and lets WideStrike patch that image
 * 4. checks that every patch site really changed
 */
#include <windows.h>
#include <stdio.h>

#define IMAGE_BASE 0x400000

static const char *names[] = {
    "_grGlideInit@0", "_grSstQueryBoards@4", "_grSstQueryHardware@4",
    "_grSstWinOpen@28", "_grSstWinClose@0", "_grGlideShutdown@0",
    "_grBufferSwap@4", "_grTexSource@16", "_guDrawTriangleWithClip@12",
    NULL
};

typedef int (*fn_apply_t)(void *);

static int fails = 0;

static void check(BYTE *img, unsigned rva, const char *what)
{
    BYTE *p = img + rva;
    printf("  %-34s @%08X -> %02X %02X %02X %02X %02X\n",
           what, IMAGE_BASE + rva, p[0], p[1], p[2], p[3], p[4]);
}

int main(int argc, char **argv)
{
    const char *exe  = argc > 1 ? argv[1] : "..\\jsf.exe";
    const char *dll  = argc > 2 ? argv[2] : "build\\glide2x.dll";
    HMODULE     hdll, himg;
    fn_apply_t  apply;
    int         i, n;

    hdll = LoadLibraryA(dll);
    if (!hdll) { printf("LoadLibrary(%s) failed %lu\n", dll, GetLastError()); return 1; }
    printf("dll      : %s @ %p\n", dll, (void *)hdll);

    for (i = 0; names[i]; i++)
        if (!GetProcAddress(hdll, names[i])) { printf("missing export %s\n", names[i]); fails++; }
    if (!fails) printf("exports  : all glide entry points resolvable\n");

    himg = LoadLibraryExA(exe, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!himg) { printf("cannot map %s (%lu)\n", exe, GetLastError()); return 1; }
    printf("image    : %s mapped at %p\n", exe, (void *)himg);

    if ((uintptr_t)himg != IMAGE_BASE)
        printf("  note: image relocated to %p (fine)\n", (void *)himg);

    apply = (fn_apply_t)GetProcAddress(hdll, "ws_test_apply");
    if (!apply) { printf("ws_test_apply not exported\n"); return 1; }

    n = apply(himg);
    printf("patched  : bitmask 0x%X (expect 0x3FF, all 10 groups)\n", n);

    {
        BYTE *img = (BYTE *)himg;
        printf("\nbytes after patching (rva = image + 0x%X):\n", IMAGE_BASE);
        check(img, 0x1BFE2, "projection call (e8 .. .. .. ..)");
        check(img, 0xB16DD, "mode callback immediate");
        check(img, 0x19C08, "renderer branch (nop x6)");
        check(img, 0x5032D, "clip guard imm 0x4000");
        check(img, 0x50332, "clip guard jump 0f 87 -> ja");
        check(img, 0x17490, "cd probe (33 c0 c3)");
        check(img, 0xAA5E9, "window flag (b8 00 00 00 00)");
        check(img, 0xB0F2C, "16bpp guard (nop x5)");

        if (img[0x1BFE2] != 0xE8)                        { printf("  !! call not redirected\n"); fails++; }
        if (img[0xB16DD] == 0x50 && img[0xB16DE] == 0x0B &&
            img[0xB16DF] == 0x4B && img[0xB16E0] == 0x00) {
            printf("  !! callback not replaced\n"); fails++;
        }
        if (img[0x19C08] != 0x90)                        { printf("  !! branch not nop'ed\n"); fails++; }
        if (img[0x50332] != 0x87)                        { printf("  !! guard not switched to ja\n"); fails++; }
        if (img[0x17490] != 0x33 || img[0x17492] != 0xC3) { printf("  !! cd probe not stubbed\n"); fails++; }
        if (img[0xAA5EA] != 0x00)                          { printf("  !! window flag not cleared\n"); fails++; }
        if (img[0xB0F2C] != 0x90)                         { printf("  !! 16bpp guard not removed\n"); fails++; }
        if (n != 0x3FF)                                    { printf("  !! not every patch group applied\n"); fails++; }
    }

    printf("\n%s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
