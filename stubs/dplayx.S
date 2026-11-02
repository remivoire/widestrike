/* WideStrike compatibility stub - DPLAYX.dll (DirectPlay lobby)
 *
 * jsf.exe imports this one by ORDINAL:
 *
 *      OriginalFirstThunk -> 0x80000004        (ordinal 4)
 *      004C057E  jmp ds:__imp_DirectPlayLobbyCreateA
 *
 * so the export must exist at ordinal 4; a name is not enough (Windows
 * reports "the ordinal 4 could not be located").  dplayx.def pins it there.
 *
 * HRESULT WINAPI DirectPlayLobbyCreateA(GUID*, LPDIRECTPLAYLOBBYA*,
 *                                       IUnknown*, LPVOID, DWORD)
 *      -> 5 dwords = 20 bytes, stdcall
 */
	.text

	.globl "_dplayx_ord4"
"_dplayx_ord4":
	movl $0x80004005, %eax          /* E_FAIL */
	ret $20
