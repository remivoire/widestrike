/* WideStrike compatibility stub - IFORCE2.dll (Immersion force feedback)
 *
 * stdcall exports, so the labels carry one extra underscore: ld strips a
 * single leading '_' when it writes the export table and jsf.exe imports the
 * decorated names verbatim ("_IFLoadProjectFile@8").
 *
 * All return 0 (failure): the game then reports "Could not load IFORCE
 * project!" once and continues without force feedback.
 */
	.text

	.globl "__IFCreateEffectStructs@12"
"__IFCreateEffectStructs@12":
	xor %eax,%eax
	ret $12

	.globl "__IFReleaseProject@4"
"__IFReleaseProject@4":
	xor %eax,%eax
	ret $4

	.globl "__IFLoadProjectFile@8"
"__IFLoadProjectFile@8":
	xor %eax,%eax
	ret $8
