/* stub_macho.S -- D4 stub Mach-O kernel.
 *
 * Assembled as 32-bit code inside a 64-bit Mach-O (clang -arch x86_64
 * assembles the .code32 body). The loader's 32-bit trampoline jumps to
 * _start with paging off, so this can just poke COM1 and halt.
 */
	.code32

	.globl	_start
_start:
	movw	$0x3F8, %dx	/* COM1 */
	movl	$msg, %esi
.Lloop:
	lodsb
	testb	%al, %al
	je	.Ldone
	outb	%al, %dx
	jmp	.Lloop
.Ldone:
	hlt
	jmp	.Ldone

	.section	__TEXT,__cstring,cstring_literals
msg:
	.asciz	"RAVYNOS-MACHO-OK\n"
