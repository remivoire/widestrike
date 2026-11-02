/*
 * WideStrike compatibility stub - winstr.dll (RAD streaming interface)
 *
 * IMPORTANT: these are cdecl and the game treats a NON-ZERO return from the
 * Init / Map / Stream calls as an error (see jsf.exe 0x4AC1F0):
 *
 *      call Streamer_InitMovie
 *      test eax,eax
 *      jnz  <error path>          ; non-zero = failure
 *
 * So every entry point returns 1 ("cannot initialise"), which makes the movie
 * player bail out cleanly instead of using a streamer that does not exist.
 * Returning 0 (which looks like "success") would send the game into a null
 * handle and crash.
 *
 * cdecl means the caller cleans the stack, so the exact argument lists do not
 * matter here.
 */
#include <windows.h>

int  __cdecl Streamer_InitVideo(void)          { return 1; }
int  __cdecl Streamer_InitMovie(void)          { return 1; }
int  __cdecl Streamer_InitSound(void)          { return 1; }
int  __cdecl Streamer_InitStreaming(void)      { return 1; }
int  __cdecl Streamer_Stream(void)             { return 1; }
int  __cdecl Streamer_SetSoundBuffer(void)     { return 0; }
int  __cdecl Streamer_SetSoundDecodeMode(void) { return 0; }
int  __cdecl Streamer_GetSoundDecodeMode(void) { return 0; }
int  __cdecl Streamer_SetPixelFormat(void)     { return 0; }
void *__cdecl Streamer_MapVideo(void)          { return 0; }
int  __cdecl Streamer_ShutDownVideo(void)      { return 0; }
int  __cdecl Streamer_ShutDownMovie(void)      { return 0; }
int  __cdecl Streamer_ShutDownSound(void)      { return 0; }
