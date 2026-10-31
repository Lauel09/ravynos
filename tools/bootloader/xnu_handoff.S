/* xnu_handoff.S -- drop from 64-bit UEFI long mode to 32-bit protected mode
 * and jump to the XNU kernel entry.  Globals are filled by the C side. */
	.text

	.globl	xnu_handoff

	.align	8
gdt:
	.quad	0
	/* 32-bit flat code segment, D=1, L=0 */
	.quad	0x00CF9A000000FFFF
	/* 32-bit flat data segment */
	.quad	0x00CF92000000FFFF
gdt_end:

gdt_ptr:
	.word	gdt_end - gdt - 1
	.quad	gdt

	.extern	gHandoffEntry
	.extern	gHandoffBootArgs
	.extern	gHandoffRsp

xnu_handoff:
	cli
	lgdt	gdt_ptr(%rip)

	/* far jump into 32-bit code segment */
	pushq	$0x08
	leaq	1f(%rip), %rax
	pushq	%rax
	lretq

1:
	.code32
	/* disable paging */
	movl	%cr0, %eax
	andl	$0x7FFFFFFF, %eax
	movl	%eax, %cr0

	/* clear EFER.LME */
	movl	$0xC0000080, %ecx
	rdmsr
	andl	$~0x00000100, %eax
	wrmsr

	/* data segments */
	movw	$0x10, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	%ax, %fs
	movw	%ax, %gs

	/* stack */
	movl	gHandoffRsp, %esp

	/* boot_args pointer in eax */
	movl	gHandoffBootArgs, %eax

	/* jump to entry */
	movl	gHandoffEntry, %edx
	jmp	*%edx
