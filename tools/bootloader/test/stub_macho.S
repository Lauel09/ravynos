/* stub_macho.S -- D4 stub Mach-O kernel.
 *
 * Assembled as 32-bit code inside a 64-bit Mach-O (clang -arch x86_64
 * assembles the .code32 body). The loader's 32-bit trampoline jumps to
 * _start with paging off.
 *
 * Addresses must be position-independent: a 64-bit Mach-O links
 * _start/msg at 0xffffff80........, and ld64 refuses 32-bit absolute
 * fixups there ("32-bit pointer overflow"). call/pop + a constant
 * delta keeps every field a pure rel32 or section-local constant.
 */
	.text
	.code32

	.globl	_start
_start:
	call	1f
1:	popl	%esi		/* %esi = runtime address of 1b */
	addl	$(msg - 1b), %esi	/* constant delta -> &msg */
	movw	$0x3F8, %dx	/* COM1 */
.Lloop:
	lodsb
	testb	%al, %al
	je	.Ldone
	outb	%al, %dx
	jmp	.Lloop
.Ldone:
	hlt
	jmp	.Ldone

msg:
	.asciz	"RAVYNOS-MACHO-OK\n"
