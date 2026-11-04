/* xnu_handoff.S -- copy the kernel segments and jump to XNU _pstart.
 *
 * This tree's XNU enters from UEFI in 64-bit long mode, not 32-bit protected
 * mode.  osfmk/x86_64/start.s says so directly ("When entering from UEFI
 * (i.e. ravynOS) we are already in 64bit long mode"), has .code64 at
 * L_pstart_common, and its SWITCH_TO_64BIT_MODE macro is commented out.  The
 * old trampoline here dropped to a 32-bit compatibility segment before the
 * jump; _pstart's first .code64 block then executed `mov %ax,%ss` with a null
 * selector, which is legal in long mode but raises #GP in 32-bit mode (the
 * kernel entry + 0x39 fault seen in work/logs/qemu-d6-1.log).
 *
 * Keep the firmware's long mode and identity-mapped paging alive: _pstart
 * rebases its own BootPML4 and installs its own GDT, so the loader only has
 * to copy the segments and arrive with EAX = physical boot_args. */

	.text

	.globl	xnu_handoff

	.align	8
	.extern	gHandoffEntry
	.extern	gHandoffBootArgs
	.extern	gHandoffRsp
	.extern	xnu_copy_kernel

xnu_handoff:
	cli
	cld

	/* Copy the kernel segments on the high boot stack, still in 64-bit mode
	 * with the firmware identity map active. */
	movl	gHandoffRsp(%rip), %esp
	call	xnu_copy_kernel

	/* XNU _pstart contract: EAX = physical address of boot_args.  A 32-bit
	 * move zero-extends, and every address here is below 4 GiB.  The kernel
	 * immediately moves EAX to EDI and switches to its own stack. */
	movl	gHandoffBootArgs(%rip), %eax
	movl	gHandoffEntry(%rip), %edx
	jmp	*%rdx
