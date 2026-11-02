	.text

/* fastcall thunk for sub_4B18A0 (w in ecx, h in edx, result in eax).
 * Replaces the engine's (w,h) -> mode index lookup so it can never return
 * -1 (which makes the engine read dword_C41704[-2] and ask DirectDraw for a
 * 0x0 surface). */
	.globl "_ws_lookup_thunk"
"_ws_lookup_thunk":
	push %ebx
	push %esi
	push %ebp
	push %edx
	push %ecx
	call _ws_lookup_impl
	add $8, %esp
	pop %ebp
	pop %esi
	pop %ebx
	ret
