/*
 * ravynOS stub kernel (K1 of Docs/KernelHandoff.md).
 *
 * Freestanding x86-64 ELF, linked at 0x100000, entered by the UEFI loader
 * AFTER ExitBootServices() with rdi = &bootinfo (see Docs/KernelHandoff.md
 * design decision 3). No EFI boot services may be used from here on; the
 * only output channel is COM1 (16550 UART) which QEMU captures to the
 * serial log that bootcheck.sh greps.
 *
 * This stub deliberately does the smallest verifiable thing:
 *   1. validate the bootinfo magic/version passed in rdi,
 *   2. print RAVYNOS-KERNEL-OK plus a small dump of the memory map,
 *   3. halt in a hlt loop.
 * Growing it toward XNU-compatible handoff happens in later phases.
 */

#include <stdint.h>

#define BOOTINFO_MAGIC   0x52564E4Bu /* 'RVNK' little-endian */
#define BOOTINFO_VERSION 1u

typedef struct { uint64_t base; uint64_t pages; } kern_memdesc_t;

typedef struct {
    uint32_t magic;             /* BOOTINFO_MAGIC */
    uint32_t version;           /* BOOTINFO_VERSION */
    uint64_t memmap_size;       /* bytes of one descriptor (informational) */
    uint32_t entry_count;       /* number of entries below */
    uint32_t reserved;
    kern_memdesc_t entries[64]; /* conventional-memory regions, physical */
} bootinfo_t;

/* ---- COM1 16550 -------------------------------------------------------- */

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void com1_init(void)
{
    outb(COM1 + 1, 0x00); /* disable interrupts                */
    outb(COM1 + 3, 0x80); /* DLAB on                           */
    outb(COM1 + 0, 0x03); /* divisor 3 => 38400 baud           */
    outb(COM1 + 1, 0x00); /*                                   */
    outb(COM1 + 3, 0x03); /* DLAB off, 8n1                     */
    outb(COM1 + 2, 0xC7); /* enable FIFO, clear, 14-byte level */
    outb(COM1 + 4, 0x0B); /* DTR/RTS/OUT2                      */
}

static void putc_(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0)
        ;
    outb(COM1, (uint8_t)c);
}

static void puts_(const char *s)
{
    for (; *s; s++)
        putc_(*s);
}

static void puthex(uint64_t v, int width)
{
    static const char digits[] = "0123456789abcdef";
    char buf[17];
    for (int i = width - 1; i >= 0; i--) {
        buf[i] = digits[v & 0xF];
        v >>= 4;
    }
    buf[width] = '\0';
    puts_(buf);
}

/* ---- entry -------------------------------------------------------------- */

/*
 * Post-ExitBootServices there is no OS stack: the loader jumps to _start with
 * rsp = BOOTKSTACK_TOP, a 64 KiB boot stack it allocates and identity-maps
 * (Docs/KernelHandoff.md K1 contract; the loader reads the BOOTKSTACK_TOP
 * symbol from the ELF symtab in K2/K3). The buffer lives in .bss, which the
 * PT_LOAD MemSiz covers with zero-fill.
 */
#define BOOTKSTACK_BYTES 65536
static uint8_t boot_kstack[BOOTKSTACK_BYTES]; /* .bss */
uint8_t *const volatile BOOTKSTACK_TOP = boot_kstack + BOOTKSTACK_BYTES;

void _start(const bootinfo_t *bi)
{
    com1_init();

    puts_("\nRAVYNOS-KERNEL-OK\n");

    if ((uintptr_t)bi >= 0x1000 && bi->magic == BOOTINFO_MAGIC &&
        bi->version == BOOTINFO_VERSION) {
        puts_("bootinfo: magic RVNK v");
        putc_('0' + (char)(bi->version % 10));
        puts_(" memregions ");
        puthex(bi->entry_count, 4);
        puts_("\n");
        for (uint32_t i = 0; i < bi->entry_count && i < 64; i++) {
            puts_("  mem[");
            puthex(i, 2);
            puts_("] ");
            puthex(bi->entries[i].base, 16);
            puts_("+");
            puthex(bi->entries[i].pages * 4096, 16);
            puts_("\n");
        }
    } else {
        puts_("bootinfo: invalid or absent (rdi=");
        puthex((uint64_t)(uintptr_t)bi, 16);
        puts_(")\n");
    }

    puts_("halting.\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}
