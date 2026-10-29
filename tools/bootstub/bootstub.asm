; RAVYNOS boot smoke-test stub (El Torito "no emulation" boot image).
; Proves the portable mkimage -> QEMU -> serial-verdict loop before the real
; bootloader exists. Prints a marker to COM1 (115200 8N1) and halts.
;
; Build: nasm -f bin -o bootstub.bin bootstub.asm
; Entry conditions differ between BIOSes (CS 0x0000 or 0x07C0), so force
; CS to 0 first; everything else is set up explicitly.

BITS 16
ORG 0x7C00

start:
    jmp 0x0000:abs_start

abs_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Init COM1: 115200 baud, 8N1
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

    mov si, msg
.next_char:
    lodsb
    or al, al
    jz .halt
    push ax
.wait_thr:                     ; wait for transmit holding register empty
    mov dx, 0x3FD              ; LSR
    in al, dx
    test al, 0x20
    jz .wait_thr
    pop ax
    mov dx, 0x3F8              ; THR
    out dx, al
    jmp .next_char

.halt:
    hlt
    jmp .halt

msg: db "RAVYNOS-BOOTCHECK-STUB-OK", 13, 10, 0

; El Torito no-emulation load size is 4 sectors (2048 bytes)
times 2048-($-$$) db 0
