#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_main(SB), 1, $(3*4+NPRIVATES*4)
	MOVL	$a6base(SB), A6

	/* _tos = arg */
	MOVL	R0, _tos(SB)		/* return value of sys exec!! */
	LEA	private+8(SP), _privates(SB)
	MOVL	$NPRIVATES, _nprivates(SB)

	PEA	inargv+0(FP)
	MOVL	inargc-4(FP), TOS
	BSR	_envsetup(SB)
	BSR	main(SB)
	MOVL	R0,TOS
	BSR	exit(SB)
	RTS
