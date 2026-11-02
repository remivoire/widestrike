/*
 * WideStrike compatibility stub - winplay.dll (RAD Smacker movie interface)
 *
 * jsf.exe links winplay.dll statically; this DLL only exists so the loader
 * can start the game on an install that is missing the original file.
 *
 * The getters are only reached after Streamer_InitMovie() succeeds, which it
 * never does with the stub winstr.dll, so the values are defensive defaults
 * rather than real movie properties.
 */
#include <windows.h>

int __cdecl Movie_GetXSize(void)        { return 640;   }
int __cdecl Movie_GetYSize(void)        { return 480;   }
int __cdecl Movie_GetFrameRate(void)    { return 15;    }
int __cdecl Movie_GetCurrentFrame(void) { return 0;     }
int __cdecl Movie_GetSoundChannels(void){ return 2;     }
int __cdecl Movie_GetSoundRate(void)    { return 22050; }
int __cdecl Movie_GetSoundPrecision(void){ return 16;   }
