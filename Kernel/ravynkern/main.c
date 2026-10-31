/*
 * ravynOS stub kernel (K1/K5 of Docs/KernelHandoff.md).
 *
 * Freestanding x86-64 ELF, linked at 1 MiB, entered by the UEFI loader AFTER
 * ExitBootServices() with rdi = &bootinfo (see Docs/KernelHandoff.md design
 * decision 3).  No EFI boot services may be used from here on; the only
 * channels available are the COM1 16550 UART (both directions) and the CPU.
 *
 * What it does, in order:
 *   1. validate the bootinfo magic/version passed in rdi,
 *   2. print RAVYNOS-KERNEL-OK and a memory-map summary,
 *   3. enter the kernel command console (a tiny shell) and read commands from
 *      COM1 -- this is the "boot to a prompt and run commands" milestone at
 *      the kernel level, before any userland exists.
 *
 * Markers (contract with tools/bootcheck.sh, Docs/Pipeline.md §4):
 *   RAVYNOS-KERNEL-OK    the kernel is running and can drive the UART
 *   RAVYNOS-KSHELL-OK    the command console is up and reading input
 *
 * Input arrives on COM1, so an interactive session needs a bidirectional
 * serial device: `tools/bootcheck.sh --send cmds.txt` wires QEMU's serial port
 * to a socket and types the file's lines into this console.
 */

#include <stdint.h>

#define BOOTINFO_MAGIC   0x52564E4Bu /* 'RVNK' little-endian */
#define BOOTINFO_VERSION 1u
#define BOOTINFO_MAX_MEMDESC 64

typedef struct { uint64_t base; uint64_t pages; } kern_memdesc_t;

typedef struct {
    uint32_t magic;             /* BOOTINFO_MAGIC */
    uint32_t version;           /* BOOTINFO_VERSION */
    uint64_t memmap_size;       /* bytes of one descriptor (informational) */
    uint32_t entry_count;       /* number of entries below */
    uint32_t reserved;
    kern_memdesc_t entries[BOOTINFO_MAX_MEMDESC]; /* usable regions, physical */
} bootinfo_t;

/* ---- COM1 16550 -------------------------------------------------------- */

#define COM1        0x3F8
#define COM1_DATA   (COM1 + 0)
#define COM1_IER    (COM1 + 1)
#define COM1_FCR    (COM1 + 2)
#define COM1_LCR    (COM1 + 3)
#define COM1_MCR    (COM1 + 4)
#define COM1_LSR    (COM1 + 5)

#define LSR_DATA_READY 0x01
#define LSR_THR_EMPTY  0x20

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
    outb(COM1_IER, 0x00);  /* disable interrupts                */
    outb(COM1_LCR, 0x80);  /* DLAB on                           */
    outb(COM1_DATA, 0x01); /* divisor 1 => 115200 baud          */
    outb(COM1_IER, 0x00);
    outb(COM1_LCR, 0x03);  /* DLAB off, 8n1                     */
    outb(COM1_FCR, 0xC7);  /* enable + clear FIFO, 14-byte level*/
    outb(COM1_MCR, 0x0B);  /* DTR/RTS/OUT2                      */
}

/* ---- console output ---------------------------------------------------- */

static void putc_(char c)
{
    while ((inb(COM1_LSR) & LSR_THR_EMPTY) == 0)
        ;
    outb(COM1_DATA, (uint8_t)c);
    if (c == '\n')
        putc_('\r');
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

static void putdec(uint64_t v)
{
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0)
        buf[--i] = '0';
    while (v) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    puts_(&buf[i]);
}

/* ---- console input ----------------------------------------------------- */

static int have_char(void)
{
    return (inb(COM1_LSR) & LSR_DATA_READY) != 0;
}

static char getc_(void)
{
    while (!have_char())
        __asm__ volatile("pause");
    return (char)inb(COM1_DATA);
}

/* ---- string helpers (no libc) ------------------------------------------ */

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++)
            return 0;
    }
    return 1;
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* ---- hardware reset ---------------------------------------------------- */

static void reboot_now(void)
{
    /* Pulse the 8042 reset line, then fall back to a triple fault. */
    for (int i = 0; i < 100; i++) {
        if ((inb(0x64) & 0x02) == 0)
            break;
    }
    outb(0x64, 0xFE);
    __asm__ volatile("cli");
    for (;;)
        __asm__ volatile("hlt");
}

/* ---- command console --------------------------------------------------- */

#define LINE_MAX 128

static const bootinfo_t *g_bi;

static void cmd_help(void)
{
    puts_("commands:\n"
          "  help          this list\n"
          "  ver           kernel banner and build identity\n"
          "  mem           bootinfo memory regions and totals\n"
          "  echo <text>   print text\n"
          "  cpus          CPU vendor/brand strings (cpuid)\n"
          "  reboot        reset the machine\n"
          "  halt          stop the CPU (the console stays connected)\n");
}

static void cmd_ver(void)
{
    puts_("ravynOS kernel (ravynkern) -- stub bring-up build\n");
    puts_("  arch    x86_64, ring 0, no boot services\n");
    puts_("  console COM1 16550 @115200 8n1\n");
    puts_("  bootinfo magic ");
    puthex(g_bi ? g_bi->magic : 0, 8);
    puts_(" version ");
    putdec(g_bi ? g_bi->version : 0);
    puts_("\n");
}

static void cmd_mem(void)
{
    uint64_t total = 0;
    uint32_t n = g_bi ? g_bi->entry_count : 0;

    if (!g_bi) {
        puts_("mem: no bootinfo\n");
        return;
    }
    puts_("mem: usable regions from the firmware map\n");
    for (uint32_t i = 0; i < n; i++) {
        uint64_t bytes = g_bi->entries[i].pages * 4096u;
        total += bytes;
        puts_("  [");
        putdec(i);
        puts_("] 0x");
        puthex(g_bi->entries[i].base, 12);
        puts_(" .. 0x");
        puthex(g_bi->entries[i].base + bytes, 12);
        puts_("  ");
        putdec(bytes >> 20);
        puts_(" MiB\n");
    }
    puts_("  total ");
    putdec(total >> 20);
    puts_(" MiB in ");
    putdec(n);
    puts_(" region(s)\n");
}

static void cmd_cpus(void)
{
    uint32_t a, b, c, d;
    char vendor[13];

    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    *(uint32_t *)&vendor[0] = b;
    *(uint32_t *)&vendor[4] = d;
    *(uint32_t *)&vendor[8] = c;
    vendor[12] = '\0';
    puts_("cpus: vendor ");
    puts_(vendor);
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(1));
    puts_("  signature 0x");
    puthex(a, 8);
    puts_("  logical/bsp count ");
    putdec((b >> 16) & 0xFF);
    puts_("\n");
}

static void cmd_echo(const char *args)
{
    puts_(args);
    puts_("\n");
}

/* Returns 1 when the console should stop (halt), 0 to keep prompting. */
static int execute(char *line)
{
    char *p = (char *)skip_spaces(line);

    if (!*p)
        return 0;
    if (str_eq(p, "help") || str_eq(p, "?")) {
        cmd_help();
    } else if (str_eq(p, "ver")) {
        cmd_ver();
    } else if (str_eq(p, "mem")) {
        cmd_mem();
    } else if (str_eq(p, "cpus")) {
        cmd_cpus();
    } else if (starts_with(p, "echo ")) {
        cmd_echo(skip_spaces(p + 5));
    } else if (str_eq(p, "reboot")) {
        puts_("rebooting\n");
        puts_("RAVYNOS-KSHELL-EXIT\n");
        reboot_now();
    } else if (str_eq(p, "halt")) {
        return 1;
    } else {
        puts_("unknown command: ");
        puts_(p);
        puts_("  (try help)\n");
    }
    return 0;
}

static void kshell(void)
{
    char line[LINE_MAX];
    int len = 0;

    puts_("RAVYNOS-KSHELL-OK\n");
    for (;;) {
        int stop;

        puts_("ravyn# ");
        len = 0;
        for (;;) {
            char c = getc_();
            if (c == '\r' || c == '\n') {
                putc_('\n');
                line[len] = '\0';
                break;
            }
            if (c == 0x7F || c == 0x08) {          /* backspace */
                if (len > 0) {
                    len--;
                    puts_("\b \b");
                }
                continue;
            }
            if (c == 0x03) {                        /* ^C: drop the line */
                len = 0;
                puts_("\n");
                break;
            }
            if (c == 0x1B) {                        /* ESC: leave the shell */
                puts_("\nRAVYNOS-KSHELL-EXIT\n");
                return;
            }
            if (len < LINE_MAX - 1 && c >= 0x20 && c < 0x7F) {
                line[len++] = c;
                putc_(c);
            }
        }
        if (len == 0)
            continue;
        stop = execute(line);
        if (stop)
            break;
    }
    puts_("halted.\n");
}

/* ---- entry -------------------------------------------------------------- */

/*
 * The loader owns the boot stack: it allocates 64 KiB after ExitBootServices
 * and jumps here with rsp pointing at its top (Docs/KernelHandoff.md K3).  The
 * static stack below is only a fallback for a loader that passes its own;
 * both are inside the zero-filled tail of the single PT_LOAD segment.
 */
#define BOOTKSTACK_BYTES 65536
static uint8_t boot_kstack[BOOTKSTACK_BYTES]; /* .bss */
uint8_t *const volatile BOOTKSTACK_TOP = boot_kstack + BOOTKSTACK_BYTES;

void _start(const bootinfo_t *bi)
{
    com1_init();

    puts_("\nRAVYNOS-KERNEL-OK\n");
    puts_("ravynkern: entered in long mode with paging on\n");

    if ((uintptr_t)bi >= 0x1000 && bi->magic == BOOTINFO_MAGIC &&
        bi->version == BOOTINFO_VERSION) {
        g_bi = bi;
        puts_("bootinfo: magic RVNK v");
        putdec(bi->version);
        puts_(", ");
        putdec(bi->entry_count);
        puts_(" usable region(s), descriptor ");
        putdec(bi->memmap_size);
        puts_(" bytes\n");
    } else {
        g_bi = 0;
        puts_("bootinfo: invalid or absent (rdi=");
        puthex((uint64_t)(uintptr_t)bi, 16);
        puts_(")\n");
    }

    puts_("type 'help' for commands\n");
    kshell();

    for (;;)
        __asm__ volatile("cli; hlt");
}
