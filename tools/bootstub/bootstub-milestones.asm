; RAVYNOS boot milestone stub (El Torito "no emulation" boot image).
;
; The multi-marker counterpart of bootstub.asm: it prints the full boot
; milestone chain that tools/bootcheck.sh --milestones expects, with a busy
; wait between markers so that a poller can observe intermediate states rather
; than everything arriving in one burst.
;
; This is still a *stub*: there is no operating system behind it.  Its purpose
; is to prove the verdict machinery -- that the pipeline's markers occur in the
; right order, that the serial path survives a full boot, and that
; `bootcheck.sh --milestones` fails when a stage is missing.
;
; Build:  nasm -f bin -o bootstub-milestones.bin bootstub-milestones.asm
; (tools/mkimage.sh does this automatically for --stub-milestones when nasm is
; installed; otherwise it uses the equivalent python generator.)

BITS 16
ORG 0x7C00

; Print a NUL-terminated string at %%msg followed by CRLF, wait a while, and
; continue.  The jmp over the inline data is what makes this macro safe to
; expand repeatedly.
%macro MARKER 1
    mov si, %%msg
    call print_string
    call delay
    jmp %%after
%%msg:
    db %1, 13, 10, 0
%%after:
%endmacro

start:
    jmp 0x0000:abs_start

abs_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; COM1: 115200 baud, 8N1 (same setup as the single-marker stub)
    mov dx, 0x3FB              ; LCR: enable DLAB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8              ; DLL: divisor low = 1
    mov al, 0x01
    out dx, al
    mov dx, 0x3F9              ; DLM: divisor high = 0
    xor al, al
    out dx, al
    mov dx, 0x3FB              ; LCR: 8 data bits, no parity, 1 stop
    mov al, 0x03
    out dx, al

    MARKER "RAVYNOS-LOADER-OK"
    MARKER "RAVYNOS-KERNEL-OK"
    MARKER "RAVYNOS-ROOT-MOUNT-OK"
    MARKER "RAVYNOS-USERSPACE-OK"

.halt:
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; print_string: SI -> NUL-terminated string.  Clobbers AX, DX.
; ---------------------------------------------------------------------------
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, al
.wait_thr:
    mov dx, 0x3FD              ; LSR
    in al, dx
    test al, 0x20              ; transmit holding register empty
    jz .wait_thr
    mov al, ah
    mov dx, 0x3F8              ; THR
    out dx, al
    jmp print_string
.done:
    ret

; ---------------------------------------------------------------------------
; delay: a few hundred milliseconds of busy waiting on any modern CPU.
; Clobbers AX, CX.
; ---------------------------------------------------------------------------
delay:
    mov cx, 0x0010
.outer:
    push cx
    mov cx, 0xFFFF
.inner:
    loop .inner
    pop cx
    loop .outer
    ret

; El Torito no-emulation load size is 4 sectors (2048 bytes).
times 2048-($-$$) db 0
