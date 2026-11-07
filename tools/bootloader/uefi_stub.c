/*
 * ravynOS UEFI loader skeleton -- Step B of Docs/BootLoader.md.
 *
 * A freestanding PE32+ UEFI application with no dependencies beyond the
 * compiler: the few firmware types and tables it needs are declared here, in
 * the order the UEFI specification lays them out.  It finds a console that a
 * headless CI run can read (the EFI Serial I/O protocol, falling back to the
 * console output protocol), prints the boot milestone markers, and returns.
 *
 * Nothing here loads a kernel yet -- that is Step C/D -- so this file's job is
 * to prove the plumbing: ISO/ESP construction, the El Torito UEFI catalog
 * entry, the firmware handoff, and a serial console that bootcheck.sh can read.
 *
 * Build:  ./build.sh      (clang + lld-link, or zig as a fallback)
 * See tools/bootloader/README.md.
 *
 * The entry symbol is ``EfiMain``, not ``efi_main``: lld-link's default entry
 * for a PE image is ``EfiMain``, and zig's bundled COFF linker -- the only
 * linker on hosts without LLVM -- accepts no /entry: override at all.  Using
 * the conventional name means one source file links with both.
 *
 * Milestone contract: Docs/Pipeline.md §4.  Do not rename the markers without
 * updating tools/bootcheck.sh and Docs/BootLoader.md in the same commit.
 */

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef unsigned long long UINTN;
typedef UINTN              EFI_STATUS;
typedef void              *EFI_HANDLE;
typedef void              *EFI_EVENT;
typedef UINT16             CHAR16;
typedef UINT64             EFI_PHYSICAL_ADDRESS;
typedef UINT64             EFI_VIRTUAL_ADDRESS;

typedef enum {
    EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData, EfiBootServicesCode,
    EfiBootServicesData, EfiRuntimeServicesCode, EfiRuntimeServicesData,
    EfiConventionalMemory, EfiUnusableMemory, EfiACPIReclaimMemory,
    EfiACPIMemoryNVS, EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace,
    EfiPalCode, EfiPersistentMemory, EfiMaxMemoryType
} EFI_MEMORY_TYPE;


#define EFIERR(a)                 (0x8000000000000000ULL | (a))
#define EFI_SUCCESS               0ULL
#define EFI_ERROR(Status)         (((Status) & 0x8000000000000000ULL) != 0)
#define EFI_BUFFER_TOO_SMALL      EFIERR(5)
#define EFI_UNSUPPORTED           EFIERR(3)
#define EFI_NOT_FOUND             EFIERR(14)
#define EFIAPI                    __attribute__((ms_abi))

#define EFI_PAGE_SHIFT            12
#define EFI_PAGE_SIZE             (1ULL << EFI_PAGE_SHIFT)

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

/* ---------------------------------------------------------------------- */
/* Device paths (only what LoadImage needs)                                */
/* ---------------------------------------------------------------------- */
#define MEDIA_DEVICE_PATH 0x04
#define MEDIA_HARDDRIVE_DP 0x01
#define END_ENTIRE_DEVICE_PATH_SUBTYPE 0xFF

#pragma pack(push, 1)
typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT16 Length;
} EFI_DEVICE_PATH_HEADER;

typedef struct {
    EFI_DEVICE_PATH_HEADER Hdr;
    UINT32 PartitionNumber;
    UINT64 StartingLBA;
    UINT64 EndingLBA;
    UINT16 PartTypeGUID[4];   /* signature only; zero = "don't care" */
    UINT16 Attributes;
} HARDDRIVE_DEVICE_PATH;

typedef struct {
    EFI_DEVICE_PATH_HEADER Hdr;
    CHAR16 FileName[1];       /* variable length, NUL-terminated */
} FILEPATH_DEVICE_PATH;
#pragma pack(pop)

/* ---------------------------------------------------------------------- */
/* Loaded image protocol                                                   */
/* ---------------------------------------------------------------------- */
typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    void *SystemTable;
    EFI_HANDLE DeviceHandle;
    EFI_DEVICE_PATH_HEADER *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
    void *LoadOptions;
    EFI_PHYSICAL_ADDRESS ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* UEFI 2.x: 4 + 4(pad) + 5 pointers, then Reserved/LoadOptionsSize/
 * LoadOptions before ImageBase/ImageSize.  The earlier declaration omitted
 * those three fields, so the log read LoadOptions as ImageBase and
 * LoadOptionsSize as ImageSize ("image base 0x0 size 0" on every boot); every
 * field after FilePath was shifted.  DeviceHandle/FilePath came before the gap
 * and stayed correct, which is why the kernel still loaded. */
_Static_assert(sizeof(EFI_LOADED_IMAGE_PROTOCOL) == 96,
               "EFI_LOADED_IMAGE_PROTOCOL must match the UEFI layout");

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

/* ---------------------------------------------------------------------- */
/* Console output                                                          */
/* ---------------------------------------------------------------------- */
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_OUTPUT *This, UINT8 ExtendedVerification);
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT *This, CHAR16 *String);
    EFI_STATUS (EFIAPI *TestString)(EFI_SIMPLE_TEXT_OUTPUT *This, CHAR16 *String);
    EFI_STATUS (EFIAPI *QueryMode)(EFI_SIMPLE_TEXT_OUTPUT *This, UINTN ModeNumber,
                                   UINTN *Columns, UINTN *Rows);
    EFI_STATUS (EFIAPI *SetMode)(EFI_SIMPLE_TEXT_OUTPUT *This, UINTN ModeNumber);
    EFI_STATUS (EFIAPI *SetAttribute)(EFI_SIMPLE_TEXT_OUTPUT *This, UINTN Attribute);
    EFI_STATUS (EFIAPI *ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT *This);
    EFI_STATUS (EFIAPI *SetCursorPosition)(EFI_SIMPLE_TEXT_OUTPUT *This, UINTN Column, UINTN Row);
    EFI_STATUS (EFIAPI *EnableCursor)(EFI_SIMPLE_TEXT_OUTPUT *This, UINT8 Visible);
    void *Mode;
};

/* ---------------------------------------------------------------------- */
/* Serial I/O                                                              */
/* ---------------------------------------------------------------------- */
typedef struct {
    UINT32 ControlMask;
    UINT32 Timeout;
    UINT64 BaudRate;
    UINT32 ReceiveFifoDepth;
    UINT32 DataBits;
    UINT32 Parity;
    UINT32 StopBits;
} EFI_SERIAL_IO_MODE;

typedef struct EFI_SERIAL_IO_PROTOCOL EFI_SERIAL_IO;

/* The UEFI specification puts a UINT32 Revision in front of every one of
 * these members (EFI_SERIAL_IO_PROTOCOL.Revision).  Leaving it out shifted
 * every function pointer by eight bytes, so this->Reset() called the address
 * 0x00010000 -- the Revision value read as a pointer -- which is one of the
 * ways an EFI application dies on an "invalid opcode" far outside its own
 * image.  Do not "simplify" this field away. */
struct EFI_SERIAL_IO_PROTOCOL {
    UINT32 Revision;
    UINT32 Pad;
    EFI_STATUS (EFIAPI *Reset)(EFI_SERIAL_IO *This);
    EFI_STATUS (EFIAPI *SetAttributes)(EFI_SERIAL_IO *This, UINT64 BaudRate,
                                       UINT32 ReceiveFifoDepth, UINT32 Timeout,
                                       UINT32 Parity, UINT32 DataBits, UINT32 StopBits);
    EFI_STATUS (EFIAPI *SetControlBits)(EFI_SERIAL_IO *This, UINT32 Control);
    EFI_STATUS (EFIAPI *GetControlBits)(EFI_SERIAL_IO *This, UINT32 *Control);
    EFI_STATUS (EFIAPI *Write)(EFI_SERIAL_IO *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Read)(EFI_SERIAL_IO *This, UINTN *BufferSize, void *Buffer);
    EFI_SERIAL_IO_MODE *Mode;
    const EFI_GUID *DeviceTypeGuid;   /* added in UEFI 2.8; unused, but sized */
};

/* ---------------------------------------------------------------------- */
/* Boot services                                                           */
/* ---------------------------------------------------------------------- */
typedef enum { AllocateAnyPages, AllocateMaxAddress, AllocateAddress } EFI_ALLOCATE_TYPE;

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define EFI_MEMORY_RUNTIME 0x8000000000000000ULL /* pexpert/i386/efi.h:232 */

#define EVT_TIMER 0x80000000
typedef void (*EFI_EVENT_NOTIFY)(EFI_EVENT Event, void *Context);

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;

    /* Task priority services */
    EFI_STATUS (EFIAPI *RaiseTPL)(UINTN NewTPL);
    void (EFIAPI *RestoreTPL)(UINTN OldTPL);

    /* Memory services */
    EFI_STATUS (EFIAPI *AllocatePages)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType,
                                       UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *MemoryMapSize,
                                      EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                      UINTN *MapKey, UINTN *DescriptorSize,
                                      UINT32 *DescriptorVersion);
    EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size, void **Buffer);
    EFI_STATUS (EFIAPI *FreePool)(void *Buffer);

    /* Event, timer and task priority services */
    EFI_STATUS (EFIAPI *CreateEvent)(UINT32 Type, UINTN NotifyTpl,
                                     EFI_EVENT_NOTIFY NotifyFunction,
                                     void *NotifyContext, EFI_EVENT *Event);
    EFI_STATUS (EFIAPI *SetTimer)(EFI_EVENT Event, UINT64 Type, UINT64 TriggerTime);
    EFI_STATUS (EFIAPI *WaitForEvent)(UINTN NumberOfEvents, EFI_EVENT *Event, UINTN *Index);
    EFI_STATUS (EFIAPI *SignalEvent)(EFI_EVENT Event);
    EFI_STATUS (EFIAPI *CloseEvent)(EFI_EVENT Event);
    EFI_STATUS (EFIAPI *CheckEvent)(EFI_EVENT Event);

    /* Protocol handler services */
    EFI_STATUS (EFIAPI *InstallProtocolInterface)(EFI_HANDLE *Handle, EFI_GUID *Protocol,
                                                  UINTN InterfaceType, void *Interface);
    EFI_STATUS (EFIAPI *ReinstallProtocolInterface)(EFI_HANDLE Handle, EFI_GUID *Protocol,
                                                    void *OldInterface, void *NewInterface);
    EFI_STATUS (EFIAPI *UninstallProtocolInterface)(EFI_HANDLE Handle, EFI_GUID *Protocol,
                                                    void *Interface);
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol,
                                        void **Interface);
    void *Reserved;
    EFI_STATUS (EFIAPI *RegisterProtocolNotify)(EFI_GUID *Protocol, EFI_EVENT Event,
                                                void **Registration);
    EFI_STATUS (EFIAPI *LocateHandle)(UINTN SearchType, EFI_GUID *Protocol,
                                      void *SearchKey, UINTN *BufferSize, EFI_HANDLE *Buffer);
    EFI_STATUS (EFIAPI *LocateDevicePath)(EFI_GUID *Protocol, void **DevicePath,
                                          EFI_HANDLE *Device);
    EFI_STATUS (EFIAPI *InstallConfigurationTable)(EFI_GUID *Guid, void *Table);

    /* Image services */
    EFI_STATUS (EFIAPI *LoadImage)(UINT8 BootPolicy, EFI_HANDLE ParentImageHandle,
                                   void *DevicePath, void *SourceBuffer,
                                   UINTN SourceSize, EFI_HANDLE *ImageHandle);
    EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE ImageHandle, UINTN *ExitDataSize,
                                    CHAR16 **ExitData);
    EFI_STATUS (EFIAPI *Exit)(EFI_HANDLE ImageHandle, EFI_STATUS ExitStatus,
                              UINTN ExitDataSize, CHAR16 *ExitData);
    EFI_STATUS (EFIAPI *UnloadImage)(EFI_HANDLE ImageHandle);
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);

    /* Miscellaneous services */
    EFI_STATUS (EFIAPI *GetNextMonotonicCount)(UINT64 *Count);
    EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN Timeout, UINT64 WatchdogCode,
                                          UINTN DataSize, CHAR16 *WatchdogData);

    /* Driver support services */
    EFI_STATUS (EFIAPI *ConnectController)(EFI_HANDLE ControllerHandle,
                                           EFI_HANDLE *DriverImageHandle,
                                           void *RemainingDevicePath, UINT8 Recursive);
    EFI_STATUS (EFIAPI *DisconnectController)(EFI_HANDLE ControllerHandle,
                                              EFI_HANDLE DriverImageHandle,
                                              EFI_HANDLE ChildHandle);

    /* Open and close protocol services */
    EFI_STATUS (EFIAPI *OpenProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol,
                                      void **Interface, EFI_HANDLE AgentHandle,
                                      EFI_HANDLE ControllerHandle, UINT32 Attributes);
    EFI_STATUS (EFIAPI *CloseProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol,
                                       EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle);
    EFI_STATUS (EFIAPI *OpenProtocolInformation)(EFI_HANDLE Handle, EFI_GUID *Protocol,
                                                 void **EntryBuffer, UINTN *EntryCount);

    /* Library services */
    EFI_STATUS (EFIAPI *ProtocolsPerHandle)(EFI_HANDLE Handle, EFI_GUID ***ProtocolBuffer,
                                            UINTN *ProtocolBufferCount);
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(UINTN SearchType, EFI_GUID *Protocol,
                                            void *SearchKey, UINTN *NoHandles,
                                            EFI_HANDLE **Buffer);
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *Protocol, void *Registration,
                                        void **Interface);
    EFI_STATUS (EFIAPI *InstallMultipleProtocolInterfaces)(EFI_HANDLE *Handle, ...);
    EFI_STATUS (EFIAPI *UninstallMultipleProtocolInterfaces)(EFI_HANDLE Handle, ...);
    EFI_STATUS (EFIAPI *CalculateCrc32)(void *Data, UINTN DataSize, UINT32 *Crc32);
    void (EFIAPI *CopyMem)(void *Destination, void *Source, UINTN Length);
    void (EFIAPI *SetMem)(void *Buffer, UINTN Size, UINT8 Value);
    EFI_STATUS (EFIAPI *CreateEventEx)(UINT32 Type, UINTN NotifyTpl,
                                       EFI_EVENT_NOTIFY NotifyFunction,
                                       const void *NotifyContext, const EFI_GUID *EventGroup,
                                       EFI_EVENT *Event);
};

/* ---------------------------------------------------------------------- */
/* System table                                                            */
/* ---------------------------------------------------------------------- */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    UINT32 Pad0;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;

_Static_assert(sizeof(EFI_CONFIGURATION_TABLE) == 24,
               "EFI_CONFIGURATION_TABLE must match the UEFI 64-bit layout");

/* UEFI 2.x §4.6: ACPI 2.0+ RSDP (XSDT) first, ACPI 1.0 fallback. */
#define EFI_ACPI_20_TABLE_GUID \
    { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }
#define EFI_ACPI_TABLE_GUID \
    { 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

/* BB25CF6F-F1D4-11D2-9A0C-0090273FC1FD */
static EFI_GUID gSerialIoGuid = {
    0xBB25CF6F, 0xF1D4, 0x11D2,
    { 0x9A, 0x0C, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0xFD }
};

static EFI_SERIAL_IO *gSerial = 0;
static EFI_SIMPLE_TEXT_OUTPUT *gConsole = 0;
static int gMirrorConOut = 1;     /* cleared when ConOut *is* the serial port */

/* ---------------------------------------------------------------------- */
/* Output: the 16550 UART first, firmware protocols second                 */
/*                                                                         */
/* This is the loader's most important diagnostic property.  In the first  */
/* CI runs nothing at all appeared in serial.log before OVMF's exception   */
/* dump, and there was no way to tell "the firmware never ran us" from     */
/* "we ran and crashed before printing".  Firmware is not obliged to       */
/* install the Serial I/O protocol (OVMF's own debug output goes to the    */
/* UART through DebugLib, not through that protocol), so a loader that     */
/* prints only through SerialIo can be silent on the very host that is     */
/* testing it.  Writing COM1 directly needs no protocol at all.            */
/*                                                                         */
/* ConOut is mirrored only when the firmware has *no* Serial I/O protocol: */
/* that is the graphical-console case, where the mirror is the only way a  */
/* user sees anything.  When SerialIo exists the firmware console is       */
/* already the serial port and mirroring would print every line twice.     */
/* ---------------------------------------------------------------------- */
#define COM1          0x3F8
#define COM1_LSR      (COM1 + 5)
#define LSR_RX_READY  0x01
#define LSR_THR_EMPTY 0x20

static void uart_outb(UINT16 port, UINT8 val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static UINT8 uart_inb(UINT16 port)
{
    UINT8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void uart_init(void)
{
    uart_outb(COM1 + 1, 0x00);   /* no interrupts               */
    uart_outb(COM1 + 3, 0x80);   /* DLAB on                     */
    uart_outb(COM1 + 0, 0x01);   /* divisor 1 => 115200 baud    */
    uart_outb(COM1 + 1, 0x00);
    uart_outb(COM1 + 3, 0x03);   /* DLAB off, 8n1               */
    uart_outb(COM1 + 2, 0xC7);   /* FIFO enable + clear         */
    uart_outb(COM1 + 4, 0x0B);   /* DTR/RTS/OUT2                */
}

static void uart_putc(char c)
{
    int spin = 200000;
    while ((uart_inb(COM1_LSR) & LSR_THR_EMPTY) == 0 && --spin)
        ;
    uart_outb(COM1, (UINT8)c);
}

static void serial_putc(char c)
{
    UINTN size = 1;
    uart_putc(c);
    /* The protocol write is a no-op duplicate of the UART write above when
     * the protocol drives the same port, so it is not used at all: the UART
     * is the channel, the protocol only tells us whether ConOut is serial. */
    (void)size;
}

static void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

/* Mirror to ConOut so a graphical console shows the same text. */
static void console_puts(const char *s)
{
    CHAR16 line[160];
    UINTN i = 0;

    if (!gConsole || !gMirrorConOut) {
        return;
    }
    while (*s && i < 158) {
        if (*s == '\n') {
            line[i++] = '\r';
        }
        line[i++] = (CHAR16)(UINT8)*s++;
    }
    line[i++] = '\r';
    line[i++] = '\n';
    line[i] = 0;
    gConsole->OutputString(gConsole, line);
}

static void say(const char *s)
{
    serial_puts(s);
    console_puts(s);
}

/* Unsigned decimal, for sizes and counts (no libc). */
static void say_dec(UINT64 v)
{
    char buf[21];
    int i = 20;

    buf[i] = 0;
    if (v == 0) {
        buf[--i] = '0';
    }
    while (v) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    say(&buf[i]);
}

static void say_hex(UINT64 v, int width)
{
    static const char digits[] = "0123456789abcdef";
    char buf[17];
    int i;

    if (width > 16) {
        width = 16;
    }
    for (i = width - 1; i >= 0; i--) {
        buf[i] = digits[v & 0xF];
        v >>= 4;
    }
    buf[width] = 0;
    say(buf);
}

/* ---------------------------------------------------------------------- */
/* Console discovery                                                       */
/* ---------------------------------------------------------------------- */
static void find_console(EFI_SYSTEM_TABLE *st)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_SERIAL_IO *serial = 0;

    uart_init();                 /* always: this is the primary channel */

    if (bs && !EFI_ERROR(bs->LocateProtocol(&gSerialIoGuid, 0,
                                            (void **)&serial)) && serial) {
        serial->Reset(serial);
        /* 115200 8N1: the rate tools/bootcheck.sh reads at. */
        serial->SetAttributes(serial, 115200, 0, 0, 0 /* no parity */,
                              8 /* data bits */, 1 /* stop bits */);
        gSerial = serial;
        gMirrorConOut = 0;       /* ConOut is this same serial terminal */
    }
    if (st->ConOut) {
        gConsole = st->ConOut;
    }
}

/* ---------------------------------------------------------------------- */
/* ELF64 payload support (K2/K3 of Docs/KernelHandoff.md)                  */
/*                                                                         */
/* The stub kernel is a raw ELF64 linked at 0x100000.  We read it off the  */
/* ESP through the firmware's SimpleFileSystem protocol (no FAT parsing    */
/* here -- same principle as the shell chain-load), copy every PT_LOAD     */
/* segment verbatim, harvest the memory map into a bootinfo block at       */
/* 0x90000, call ExitBootServices() and jump to e_entry in long mode with  */
/* paging still on (firmware page tables).  rdi = &bootinfo.               */
/* ---------------------------------------------------------------------- */

/* Where a kernel may live on the boot volume, in the order they are tried.
 * The image pipeline stages it as /EFI/KERNEL.ELF (readable by the shell's
 * `kernel` command too); /BOOT/KERNEL is what mkimage --kernel writes, and
 * the other two are the obvious hand-made variants.  Every candidate is
 * tried and the winner is reported, so "not found" is never ambiguous. */
static const CHAR16 *const gKernelPaths[] = {
    u"\\EFI\\KERNEL.ELF",
    u"\\BOOT\\KERNEL",
    u"\\EFI\\KERNEL",
    u"\\KERNEL.ELF",
    u"\\System\\Library\\Kernels\\kernel"
};
#define KERNEL_PATH_COUNT (sizeof(gKernelPaths) / sizeof(gKernelPaths[0]))

#define BOOTINFO_ADDR     0x90000ULL
#define BOOTINFO_MAGIC    0x52564E4Bu   /* 'RVNK' little-endian */
#define BOOTINFO_VERSION  1u
#define BOOTKSTACK_SIZE   0x10000ULL    /* 64 KiB */

#define SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_FILE_PROTOCOL_GUID \
    { 0x0964e5b2, 0x64da, 0x11d3, { 0x9a, 0x35, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM;
typedef struct EFI_FILE_PROTOCOL               EFI_FILE;

struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE *This, EFI_FILE **OutFile, void *FileName,
                              UINT64 OpenMode, UINT64 Attributes);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE *This);
    EFI_STATUS (EFIAPI *Delete)(EFI_FILE *This);
    EFI_STATUS (EFIAPI *Read)(EFI_FILE *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Write)(EFI_FILE *This, UINTN *BufferSize, const void *Buffer);
    /* GetPosition/SetPosition/GetInfo/SetInfo/Flush follow; unused here. */
};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(EFI_SIMPLE_FILE_SYSTEM *This, EFI_FILE **Root);
};

typedef struct {
    UINT16 Year; UINT8 Month; UINT8 Day; UINT8 Hour; UINT8 Minute;
    UINT8 Second; UINT8 Pad1; UINT32 Nanosecond; UINT16 TimeZone;
    UINT8 Daylight; UINT8 Pad2;
} EFI_TIME;

_Static_assert(sizeof(EFI_TIME) == 16, "EFI_TIME must be 16 bytes");

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

_Static_assert(__builtin_offsetof(EFI_FILE_INFO, FileName) == 80,
               "EFI_FILE_INFO.FileName offset must match UEFI spec");

#define EFI_FILE_DIRECTORY 0x0000000000000010ULL

#pragma pack(push, 1)
typedef struct { UINT8  Ident[16]; UINT16 Type, Machine; UINT32 Version;
                 UINT64 Entry; UINT64 PhOff; UINT64 ShOff; UINT64 Flags;
                 UINT16 EhSize, PhEntSize, PhNum, ShEntSize, ShNum, ShStrIdx;
               } Elf64_Ehdr;
typedef struct { UINT32 Type; UINT32 Pad; UINT64 Offset; UINT64 Vaddr;
                 UINT64 Paddr; UINT64 FileSiz; UINT64 MemSiz; UINT64 Flags;
                 UINT64 Align; } Elf64_Phdr;
#pragma pack(pop)

#define ET_EXEC      2
#define EM_X86_64    62
#define PT_LOAD      1

/* ---------------------------------------------------------------------- */
/* Mach-O 64 payload support (D4)                                          */
/* ---------------------------------------------------------------------- */
#define MH_MAGIC_64       0xFEEDFACFu
#define MH_EXECUTE        2
#define MH_FILESET        12
#define LC_SEGMENT_64     0x19u
#define LC_UNIXTHREAD     0x05u
#define LC_MAIN           0x80000028u
#define CPU_TYPE_X86_64   0x01000007u

#pragma pack(push, 1)
typedef struct {
    UINT32 magic;
    UINT32 cputype;
    UINT32 cpusubtype;
    UINT32 filetype;
    UINT32 ncmds;
    UINT32 sizeofcmds;
    UINT32 flags;
    UINT32 reserved;
} mach_header_64_t;

typedef struct {
    UINT32 cmd;
    UINT32 cmdsize;
} load_command_t;

typedef struct {
    UINT32 cmd;
    UINT32 cmdsize;
    char   segname[16];
    UINT64 vmaddr;
    UINT64 vmsize;
    UINT64 fileoff;
    UINT64 filesize;
    UINT32 maxprot;
    UINT32 initprot;
    UINT32 nsects;
    UINT32 flags;
} segment_command_64_t;
#pragma pack(pop)

#define VM_MIN_KERNEL_ADDRESS 0xFFFFFF8000000000ULL

/* Where a kernel payload may be linked: above the firmware's low memory and
 * inside the identity-mapped first 4 GiB (Docs/KernelHandoff.md risk notes). */
#define KERNEL_MIN_ADDR 0x100000ULL
#define KERNEL_MAX_END  0x100000000ULL

/* XNU's pmap_lowmem_finalize() (osfmk/x86_64/pmap.c:1143) calls
 * ml_static_mfree(eHIB, stext - eHIB) on [0x19e000, 0x200000), so boot data
 * cannot live in that 2M-alignment gap.  Place a 256 KiB boot-data window
 * immediately above the highest Mach-O segment (round_page(kphys_base +
 * kphys_size)), stage it in high EfiLoaderData while boot services are alive,
 * copy it into place after ExitBootServices() alongside the kernel segments,
 * and include it in boot_args->ksize so Idle_PTs_init() / i386_vm_init()
 * (osfmk/i386/i386_init.c:369,603; i386_vm_init.c:588-605) keep it mapped and
 * reserved below first_avail. */
#define XNU_BOOT_DATA_SIZE     0x40000ULL /* 256 KiB total */
#define XNU_BD_DT_OFF          0x00000ULL /* 64 KiB device tree */
#define XNU_BD_DT_CAP          0x10000ULL
#define XNU_BD_BA_OFF          0x10000ULL /* 4 KiB boot_args_t */
#define XNU_BD_EFISTUB_OFF     0x11000ULL /* 4 KiB zeroed EFI_SYSTEM_TABLE_64 */
#define XNU_BD_MEMMAP_OFF      0x12000ULL /* 184 KiB EFI memory map */
#define XNU_BD_MEMMAP_CAP      (XNU_BOOT_DATA_SIZE - XNU_BD_MEMMAP_OFF)
#define XNU_BOOT_DATA_MAX_END  0x3C000000ULL /* < NKPT*2M (1000M, i386_init.c:369) */

typedef struct { UINT64 base; UINT64 pages; } kern_memdesc_t;
typedef struct {
    UINT32 magic;
    UINT32 version;
    UINT64 memmap_size;
    UINT32 entry_count;
    UINT32 reserved;
    kern_memdesc_t entries[64];
} bootinfo_t;

static EFI_SIMPLE_FILE_SYSTEM *gFs = 0;
UINT32 gDTPhys = 0;
UINT32 gDTSize = 0;
UINT32 gHandoffEntry = 0;
UINT32 gHandoffBootArgs = 0;
UINT32 gHandoffRsp = 0;
static UINT8 *gBootDataStage = 0;
static UINT64 gBootDataPhys = 0;
static UINT64 gPostKernelEnd = 0;
static UINT64 gLoaderImageBase = 0;
static UINT64 gLoaderImageSize = 0;

/* /chosen/memory-map "Driver-<hex>" value layout
 * (Kernel/xnu/libsa/bootstrap.cpp:582-589, 663-695). */
typedef struct {
    UINT32 paddr;
    UINT32 length;
} dt_memmap_entry_t;

_Static_assert(sizeof(dt_memmap_entry_t) == 8,
               "dt_memmap_entry_t must match _DeviceTreeBuffer");

/* Header at the start of each booter-staged kext buffer
 * (Kernel/xnu/libkern/c++/OSKext.cpp:1759-1766). */
typedef struct {
    UINT32 infoDictPhysAddr;
    UINT32 infoDictLength;
    UINT32 executablePhysAddr;
    UINT32 executableLength;
    UINT32 bundlePathPhysAddr;
    UINT32 bundlePathLength;
} booter_kext_file_info_t;

_Static_assert(sizeof(booter_kext_file_info_t) == 24,
               "booter_kext_file_info_t must match _BooterKextFileInfo");

#define MAX_BOOTER_KEXTS 64u
typedef struct {
    UINT8 *stage;
    UINTN  stage_pages;
    UINT64 dest_phys;
    UINT32 blob_size;
    UINT32 plist_off;
    UINT32 plist_len;
    UINT32 path_off;
    UINT32 path_len;
    UINT32 exec_off;
    UINT32 exec_len;
} staged_kext_t;

static staged_kext_t gStagedKexts[MAX_BOOTER_KEXTS];
static UINTN gStagedKextCount = 0;

/* Mach-O segments are recorded, not copied, while boot services are alive:
 * part of the kernel's physical span is firmware EfiBootServicesCode, which
 * cannot be reserved until ExitBootServices.  xnu_copy_kernel() replays these
 * from the payload on the boot stack after boot services are gone. */
#define MAX_KERNEL_SEGS 32
typedef struct {
    UINT64 phys;
    UINT64 fileoff;
    UINT64 filesize;
    UINT64 vmsize;
} kernel_seg_t;
static kernel_seg_t gKernelSegs[MAX_KERNEL_SEGS];
static UINTN gKernelSegCount;
static const UINT8 *gKernelPayload;
static UINTN gKernelPayloadSize;
static UINT64 gBootStackBase;

/* The real XNU kernel is ~20 MiB with its __LINKEDIT, so the reader must
 * accept more than the old 8 MiB cap while still refusing an absurd file
 * that would exhaust memory. */
#define MAX_KERNEL_PAYLOAD (64u << 20)

/* Stage the payload at the top of the firmware's identity-mapped range.
 * An ~20 MiB pool allocation lands at 2 MiB, which is exactly where the
 * kernel's __TEXT segment wants to be, so it cannot stay in low pool. */
#define KERNEL_PAYLOAD_MAX_ADDR 0xFFFFFFFFULL

/* Read the whole file at `rel_path` relative to `base_dir` into pages below
 * KERNEL_PAYLOAD_MAX_ADDR. */
static EFI_STATUS efi_read_rel_file(EFI_BOOT_SERVICES *bs, EFI_FILE *base_dir,
                                    const CHAR16 *rel_path, void **out_buf,
                                    UINTN *out_size, UINTN *out_pages)
{
    EFI_FILE *f = 0;
    EFI_STATUS stt;
    UINTN sz;
    UINT8 *buf = 0;
    UINTN cap = 0;
    UINTN total = 0;

    *out_buf = 0;
    *out_size = 0;
    *out_pages = 0;

    stt = base_dir->Open(base_dir, &f, (void *)rel_path, 1 /*READ*/, 0);
    if (EFI_ERROR(stt)) {
        return stt;
    }
    /* Chunked read until EOF.  EFI_FILE_PROTOCOL.Read shrinks *BufferSize to
     * the number of bytes actually transferred and returns 0 bytes at end of
     * file, which is the exit condition. */
    for (;;) {
        if (total == cap) {
            UINTN newcap = cap ? cap * 2 : (16u << 10);
            EFI_PHYSICAL_ADDRESS addr = KERNEL_PAYLOAD_MAX_ADDR;
            if (newcap > MAX_KERNEL_PAYLOAD) {
                say("kernel: payload larger than 64 MiB, refusing\n");
                if (buf) {
                    bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)buf,
                                  cap >> EFI_PAGE_SHIFT);
                }
                f->Close(f);
                return EFI_UNSUPPORTED;
            }
            if (EFI_ERROR(bs->AllocatePages(AllocateMaxAddress,
                                            EfiLoaderData,
                                            newcap >> EFI_PAGE_SHIFT,
                                            &addr))) {
                if (buf) {
                    bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)buf,
                                  cap >> EFI_PAGE_SHIFT);
                }
                f->Close(f);
                return EFI_UNSUPPORTED;
            }
            if (buf) {
                bs->CopyMem((void *)(UINTN)addr, buf, total);
                bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)buf,
                              cap >> EFI_PAGE_SHIFT);
            }
            buf = (UINT8 *)(UINTN)addr;
            cap = newcap;
        }
        sz = cap - total;
        stt = f->Read(f, &sz, buf + total);
        if (EFI_ERROR(stt)) {
            bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)buf,
                          cap >> EFI_PAGE_SHIFT);
            f->Close(f);
            return stt;
        }
        if (sz == 0) {
            break;                       /* end of file */
        }
        total += sz;
    }
    f->Close(f);
    if (total == 0) {
        if (buf) {
            bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)buf,
                          cap >> EFI_PAGE_SHIFT);
        }
        return EFI_UNSUPPORTED;
    }
    *out_buf = buf;
    *out_size = total;
    *out_pages = cap >> EFI_PAGE_SHIFT;
    return EFI_SUCCESS;
}

/* Read the whole file at `path` (relative to volume root) into pages below
 * KERNEL_PAYLOAD_MAX_ADDR.  On EFI_SUCCESS, buf_out/size_out/pages_out
 * receive the data pointer, byte count and allocated page count. */
static EFI_STATUS esp_read_file(EFI_BOOT_SERVICES *bs, EFI_HANDLE dev_handle,
                                const CHAR16 *path, void **out_buf,
                                UINTN *out_size, UINTN *out_pages)
{
    static EFI_GUID sfs_guid = SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    static EFI_GUID file_guid = EFI_FILE_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM *fs = 0;
    EFI_FILE *root = 0;
    EFI_STATUS stt;
    void *iface = 0;

    (void)file_guid;
    *out_buf = 0;
    *out_size = 0;
    *out_pages = 0;

    if (!dev_handle) {
        return EFI_UNSUPPORTED;
    }
    stt = bs->HandleProtocol(dev_handle, &sfs_guid, &iface);
    if (EFI_ERROR(stt)) {
        return stt;
    }
    fs = (EFI_SIMPLE_FILE_SYSTEM *)iface;
    stt = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(stt)) {
        return stt;
    }
    stt = efi_read_rel_file(bs, root, path, out_buf, out_size, out_pages);
    root->Close(root);
    if (!EFI_ERROR(stt)) {
        gFs = fs;
    }
    return stt;
}

static UINTN ascii_strlen(const char *s)
{
    UINTN n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static int ascii_match_at(const char *buf, UINTN len, UINTN pos,
                          const char *lit)
{
    UINTN i = 0;
    while (lit[i]) {
        if (pos + i >= len || buf[pos + i] != lit[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int is_xml_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Extract <key>CFBundleExecutable</key><string>...</string> from Info.plist,
 * ignoring XML comments.  Returns 1 if CFBundleExecutable is declared (storing
 * the trimmed name in `out`), or 0 for codeless kexts that omit it. */
static int plist_find_executable(const char *plist, UINTN len,
                                 char *out, UINTN out_cap)
{
    UINTN i = 0;
    const char *key_tag = "<key>CFBundleExecutable</key>";
    UINTN key_tag_len = ascii_strlen(key_tag);

    out[0] = 0;
    while (i < len) {
        if (ascii_match_at(plist, len, i, "<!--")) {
            i += 4;
            while (i < len && !ascii_match_at(plist, len, i, "-->")) {
                i++;
            }
            if (i < len) {
                i += 3;
            }
            continue;
        }
        if (ascii_match_at(plist, len, i, key_tag)) {
            UINTN s_start, s_end, n;
            i += key_tag_len;
            while (i < len) {
                if (is_xml_space(plist[i])) {
                    i++;
                } else if (ascii_match_at(plist, len, i, "<!--")) {
                    i += 4;
                    while (i < len && !ascii_match_at(plist, len, i, "-->")) {
                        i++;
                    }
                    if (i < len) {
                        i += 3;
                    }
                } else {
                    break;
                }
            }
            if (!ascii_match_at(plist, len, i, "<string>")) {
                return 0;
            }
            i += 8;
            s_start = i;
            while (i < len && !ascii_match_at(plist, len, i, "</string>")) {
                i++;
            }
            if (i >= len) {
                return 0;
            }
            s_end = i;
            while (s_start < s_end && is_xml_space(plist[s_start])) {
                s_start++;
            }
            while (s_end > s_start && is_xml_space(plist[s_end - 1])) {
                s_end--;
            }
            n = s_end - s_start;
            if (n == 0 || n + 1 > out_cap) {
                return 0;
            }
            for (i = 0; i < n; i++) {
                char c = plist[s_start + i];
                if (c == '/' || c == '\\' || c == '$') {
                    return 0;
                }
                out[i] = c;
            }
            out[n] = 0;
            return 1;
        }
        i++;
    }
    return 0;
}

static CHAR16 u16_tolower(CHAR16 c)
{
    if (c >= 'A' && c <= 'Z') {
        return (CHAR16)(c + ('a' - 'A'));
    }
    return c;
}

static int ends_with_kext_u16(const CHAR16 *name)
{
    UINTN n = 0;
    while (name[n]) {
        n++;
    }
    if (n < 6) {
        return 0;
    }
    return name[n - 5] == '.' &&
           u16_tolower(name[n - 4]) == 'k' &&
           u16_tolower(name[n - 3]) == 'e' &&
           u16_tolower(name[n - 2]) == 'x' &&
           u16_tolower(name[n - 1]) == 't';
}

static int build_u16_exec_path(CHAR16 *out, UINTN cap, const char *prefix,
                               const char *exec_name)
{
    UINTN i = 0, j = 0;
    while (prefix[j]) {
        if (i + 1 >= cap) {
            return 0;
        }
        out[i++] = (CHAR16)(UINT8)prefix[j++];
    }
    j = 0;
    while (exec_name[j]) {
        if (i + 1 >= cap) {
            return 0;
        }
        out[i++] = (CHAR16)(UINT8)exec_name[j++];
    }
    out[i] = 0;
    return 1;
}

static int join_posix_u16(char *out, UINTN cap, const char *dir,
                          const CHAR16 *leaf)
{
    UINTN i = 0, j = 0;
    while (dir[j]) {
        if (i + 1 >= cap) {
            return 0;
        }
        out[i++] = dir[j++];
    }
    if (i == 0 || out[i - 1] != '/') {
        if (i + 1 >= cap) {
            return 0;
        }
        out[i++] = '/';
    }
    j = 0;
    while (leaf[j]) {
        if (i + 1 >= cap || leaf[j] > 0x7Fu) {
            return 0;
        }
        out[i++] = (char)leaf[j++];
    }
    out[i] = 0;
    return 1;
}

static void free_staged_kexts(EFI_BOOT_SERVICES *bs)
{
    UINTN i;
    for (i = 0; i < gStagedKextCount; i++) {
        if (gStagedKexts[i].stage) {
            bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)gStagedKexts[i].stage,
                          gStagedKexts[i].stage_pages);
            gStagedKexts[i].stage = 0;
        }
    }
    gStagedKextCount = 0;
}

/* Stage a single .kext bundle (deep Contents/Info.plist or shallow Info.plist)
 * into its own 4 KiB-aligned EfiLoaderData buffer for OSKext::initWithBooterData
 * (Kernel/xnu/libkern/c++/OSKext.cpp:1768-1915). */
static int stage_one_kext(EFI_BOOT_SERVICES *bs, EFI_FILE *kext_dir,
                          const char *posix_path)
{
    void *plist_buf = 0, *exec_buf = 0;
    UINTN plist_sz = 0, plist_pages = 0;
    UINTN exec_sz = 0, exec_pages = 0;
    int deep = 1;
    char exec_name[64];
    int has_exec_key;
    UINT32 plist_off, plist_len, path_off, path_len, exec_off, exec_len;
    UINT32 raw_end, blob_size;
    UINTN stage_pages;
    EFI_PHYSICAL_ADDRESS stage_phys = KERNEL_PAYLOAD_MAX_ADDR;
    UINT8 *stage;
    staged_kext_t *slot;

    if (gStagedKextCount >= MAX_BOOTER_KEXTS) {
        return 0;
    }
    if (EFI_ERROR(efi_read_rel_file(bs, kext_dir, u"Contents\\Info.plist",
                                    &plist_buf, &plist_sz, &plist_pages))) {
        deep = 0;
        if (EFI_ERROR(efi_read_rel_file(bs, kext_dir, u"Info.plist",
                                        &plist_buf, &plist_sz, &plist_pages))) {
            return 0;
        }
    }

    has_exec_key = plist_find_executable((const char *)plist_buf, plist_sz,
                                         exec_name, sizeof(exec_name));
    if (has_exec_key) {
        CHAR16 rel1[160], rel2[160];
        const char *p1 = deep ? "Contents\\MacOS\\" : "";
        const char *p2 = deep ? "" : "Contents\\MacOS\\";
        EFI_STATUS est = EFI_NOT_FOUND;

        if (build_u16_exec_path(rel1, 160, p1, exec_name)) {
            est = efi_read_rel_file(bs, kext_dir, rel1, &exec_buf, &exec_sz,
                                    &exec_pages);
        }
        if (EFI_ERROR(est) && build_u16_exec_path(rel2, 160, p2, exec_name)) {
            est = efi_read_rel_file(bs, kext_dir, rel2, &exec_buf, &exec_sz,
                                    &exec_pages);
        }
        if (EFI_ERROR(est)) {
            /* e.g. System.kext/PlugIns/Kasan.kext on non-KASAN RELEASE builds
             * declares CFBundleExecutable=Kasan without installing a binary;
             * OSKext::loadExecutable (OSKext.cpp:5505) rejects declared-but-
             * missing executables, so skip incomplete bundles cleanly. */
            bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)plist_buf, plist_pages);
            return 0;
        }
    }

    plist_off = (UINT32)sizeof(booter_kext_file_info_t);
    plist_len = (UINT32)plist_sz + 1u; /* NUL-terminated for OSUnserializeXML */
    path_off  = plist_off + plist_len;
    path_len  = (UINT32)ascii_strlen(posix_path) + 1u;
    exec_off  = (path_off + path_len + 15u) & ~15u;
    exec_len  = (UINT32)exec_sz;
    raw_end   = exec_len ? (exec_off + exec_len) : (path_off + path_len);
    blob_size = (raw_end + (UINT32)(EFI_PAGE_SIZE - 1u)) &
                ~(UINT32)(EFI_PAGE_SIZE - 1u);
    stage_pages = (UINTN)(blob_size >> EFI_PAGE_SHIFT);

    if (EFI_ERROR(bs->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                    stage_pages, &stage_phys))) {
        if (exec_buf) {
            bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)exec_buf, exec_pages);
        }
        bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)plist_buf, plist_pages);
        return 0;
    }

    stage = (UINT8 *)(UINTN)stage_phys;
    bs->SetMem(stage, (UINTN)blob_size, 0);
    bs->CopyMem(stage + plist_off, plist_buf, plist_sz);
    bs->CopyMem(stage + path_off, (void *)posix_path, path_len - 1u);
    if (exec_buf && exec_sz) {
        bs->CopyMem(stage + exec_off, exec_buf, exec_sz);
        bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)exec_buf, exec_pages);
    }
    bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)plist_buf, plist_pages);

    slot = &gStagedKexts[gStagedKextCount++];
    slot->stage       = stage;
    slot->stage_pages = stage_pages;
    slot->dest_phys   = 0;
    slot->blob_size   = blob_size;
    slot->plist_off   = plist_off;
    slot->plist_len   = plist_len;
    slot->path_off    = path_off;
    slot->path_len    = path_len;
    slot->exec_off    = exec_off;
    slot->exec_len    = exec_len;
    return 1;
}

static void scan_kext_dir(EFI_BOOT_SERVICES *bs, EFI_FILE *dir,
                          const char *posix_dir, int depth)
{
    UINT8 info_stack[1024] __attribute__((aligned(8)));

    for (;;) {
        UINTN bsz = sizeof(info_stack);
        EFI_FILE_INFO *fi = (EFI_FILE_INFO *)(void *)info_stack;
        void *pool_info = 0;
        EFI_STATUS stt = dir->Read(dir, &bsz, fi);

        if (stt == EFI_BUFFER_TOO_SMALL) {
            if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, bsz, &pool_info))) {
                break;
            }
            fi = (EFI_FILE_INFO *)pool_info;
            stt = dir->Read(dir, &bsz, fi);
        }
        if (EFI_ERROR(stt) || bsz == 0) {
            if (pool_info) {
                bs->FreePool(pool_info);
            }
            break;
        }

        if ((fi->Attribute & EFI_FILE_DIRECTORY) != 0 &&
            !(fi->FileName[0] == '.' &&
              (fi->FileName[1] == 0 ||
               (fi->FileName[1] == '.' && fi->FileName[2] == 0))) &&
            ends_with_kext_u16(fi->FileName)) {
            EFI_FILE *sub = 0;
            char child_posix[256];
            if (join_posix_u16(child_posix, sizeof(child_posix), posix_dir,
                               fi->FileName) &&
                !EFI_ERROR(dir->Open(dir, &sub, fi->FileName, 1 /*READ*/, 0))) {
                (void)stage_one_kext(bs, sub, child_posix);
                if (depth == 0) {
                    EFI_FILE *plug = 0;
                    char plug_posix[256];
                    if (join_posix_u16(plug_posix, sizeof(plug_posix),
                                       child_posix, u"Contents/PlugIns") &&
                        !EFI_ERROR(sub->Open(sub, &plug, u"Contents\\PlugIns",
                                             1 /*READ*/, 0))) {
                        scan_kext_dir(bs, plug, plug_posix, 1);
                        plug->Close(plug);
                    }
                    if (join_posix_u16(plug_posix, sizeof(plug_posix),
                                       child_posix, u"PlugIns") &&
                        !EFI_ERROR(sub->Open(sub, &plug, u"PlugIns",
                                             1 /*READ*/, 0))) {
                        scan_kext_dir(bs, plug, plug_posix, 1);
                        plug->Close(plug);
                    }
                }
                sub->Close(sub);
            }
        }
        if (pool_info) {
            bs->FreePool(pool_info);
        }
    }
}

static void stage_booter_kexts(EFI_BOOT_SERVICES *bs, EFI_HANDLE dev_handle)
{
    static EFI_GUID sfs_guid = SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM *fs = 0;
    EFI_FILE *root = 0, *ext_dir = 0;
    void *iface = 0;
    UINT64 total_bytes = 0;
    UINTN i;

    gStagedKextCount = 0;
    if (!dev_handle ||
        EFI_ERROR(bs->HandleProtocol(dev_handle, &sfs_guid, &iface))) {
        return;
    }
    fs = (EFI_SIMPLE_FILE_SYSTEM *)iface;
    if (EFI_ERROR(fs->OpenVolume(fs, &root))) {
        return;
    }
    if (EFI_ERROR(root->Open(root, &ext_dir,
                             u"\\System\\Library\\Extensions", 1 /*READ*/, 0))) {
        root->Close(root);
        say("loader: no \\System\\Library\\Extensions (0 kexts staged)\n");
        return;
    }
    scan_kext_dir(bs, ext_dir, "/System/Library/Extensions", 0);
    ext_dir->Close(ext_dir);
    root->Close(root);

    for (i = 0; i < gStagedKextCount; i++) {
        total_bytes += gStagedKexts[i].blob_size;
    }
    say("loader: staged ");
    say_dec(gStagedKextCount);
    say(" kexts (");
    say_dec(total_bytes);
    say(" bytes)\n");
}

/* ---------------------------------------------------------------------- */
/* boot_args (D3) -- byte-for-byte copy of Kernel/xnu/pexpert/pexpert/i386/boot.h */
/* ---------------------------------------------------------------------- */
#define BOOT_LINE_LENGTH 1024
#define kBootArgsRevision 0
#define kBootArgsVersion 2
#define kBootArgsEfiMode64 64

typedef struct { UINT32 v_display, v_rowBytes, v_width, v_height, v_depth;
                 UINT8 v_rotate; UINT8 v_resv_byte[3]; UINT32 v_resv[6];
                 UINT64 v_baseAddr; } Boot_Video;
typedef struct { UINT32 v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth; } Boot_VideoV1;

typedef struct boot_args {
    UINT16 Revision;
    UINT16 Version;
    UINT8  efiMode;
    UINT8  debugMode;
    UINT16 flags;
    char   CommandLine[BOOT_LINE_LENGTH];
    UINT32 MemoryMap;
    UINT32 MemoryMapSize;
    UINT32 MemoryMapDescriptorSize;
    UINT32 MemoryMapDescriptorVersion;
    Boot_VideoV1 VideoV1;
    UINT32 deviceTreeP;
    UINT32 deviceTreeLength;
    UINT32 kaddr;
    UINT32 ksize;
    UINT32 efiRuntimeServicesPageStart;
    UINT32 efiRuntimeServicesPageCount;
    UINT64 efiRuntimeServicesVirtualPageStart;
    UINT32 efiSystemTable;
    UINT32 kslide;
    UINT32 performanceDataStart;
    UINT32 performanceDataSize;
    UINT32 keyStoreDataStart;
    UINT32 keyStoreDataSize;
    UINT64 bootMemStart;
    UINT64 bootMemSize;
    UINT64 PhysicalMemorySize;
    UINT64 FSBFrequency;
    UINT64 pciConfigSpaceBaseAddress;
    UINT32 pciConfigSpaceStartBusNumber;
    UINT32 pciConfigSpaceEndBusNumber;
    UINT32 csrActiveConfig;
    UINT32 csrCapabilities;
    UINT32 boot_SMC_plimit;
    UINT16 bootProgressMeterStart;
    UINT16 bootProgressMeterEnd;
    Boot_Video Video;
    UINT32 apfsDataStart;
    UINT32 apfsDataSize;
    UINT32 __reserved4[710];
} boot_args_t;

_Static_assert(sizeof(boot_args_t) == 4096, "boot_args size mismatch");

/* Extra boot-args loaded from the ESP \boot-args file (ASCII, one argument
 * word per whitespace token, e.g. "-s" for single-user).  Mirrors how
 * com.apple.Boot.plist boot-args are appended to the default kernel flags on
 * macOS.  Empty by default; build_boot_args() appends it verbatim. */
static char gBootArgsFile[BOOT_LINE_LENGTH];
static UINTN gBootArgsFileLen = 0;

/* On a failed fixed-address reservation, print every firmware memory-map
 * descriptor that overlaps [addr, addr+len) so the occupying type is visible
 * in the serial log instead of just the address. */
static void say_memmap_covering(EFI_BOOT_SERVICES *bs, UINT64 addr, UINT64 len)
{
    UINTN mapsize = 0, descsize = 0, key = 0, off;
    UINT32 descver = 0;
    EFI_MEMORY_DESCRIPTOR *map = 0;
    UINT64 end = addr + len;

    (void)bs->GetMemoryMap(&mapsize, 0, &key, &descsize, &descver);
    mapsize += 2 * descsize;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, mapsize, (void **)&map))) {
        return;
    }
    if (EFI_ERROR(bs->GetMemoryMap(&mapsize, map, &key, &descsize,
                                   &descver))) {
        bs->FreePool(map);
        return;
    }
    for (off = 0; off < mapsize; off += descsize) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + off);
        UINT64 dend = d->PhysicalStart + d->NumberOfPages * EFI_PAGE_SIZE;
        if (d->PhysicalStart < end && addr < dend) {
            say("kernel: memmap type ");
            say_dec(d->Type);
            say(" at 0x");
            say_hex(d->PhysicalStart, 16);
            say("-0x");
            say_hex(dend, 16);
            say("\n");
        }
    }
    bs->FreePool(map);
}

/* Parse + stage the kernel image.  On success returns entry point and
 * allocates a boot stack; rsp_out receives its top. */
static int load_kernel_image(EFI_BOOT_SERVICES *bs, void *file_data, UINTN size,
                             UINT64 *entry_out, UINT64 *rsp_out,
                             UINT64 *kphys_base_out, UINT64 *kphys_size_out)
{
    Elf64_Ehdr *eh = (Elf64_Ehdr *)file_data;
    Elf64_Phdr *ph;
    UINTN i;
    UINT64 entry = 0;
    UINT64 lo = ~0ULL, hi = 0;

    if (size < sizeof(Elf64_Ehdr) ||
        eh->Ident[0] != 0x7F || eh->Ident[1] != 'E' ||
        eh->Ident[2] != 'L'  || eh->Ident[3] != 'F' ||
        eh->Machine != EM_X86_64 || eh->Type != ET_EXEC) {
        say("kernel: not a x86-64 ET_EXEC ELF64\n");
        return 0;
    }
    if (eh->PhEntSize < sizeof(Elf64_Phdr) ||
        eh->PhOff + (UINT64)eh->PhNum * eh->PhEntSize > size) {
        say("kernel: ELF program headers out of bounds\n");
        return 0;
    }
    ph = (Elf64_Phdr *)((UINT8 *)file_data + eh->PhOff);
    for (i = 0; i < eh->PhNum; i++) {
        Elf64_Phdr *p = (Elf64_Phdr *)((UINT8 *)ph + i * eh->PhEntSize);
        if (p->Type != PT_LOAD) {
            continue;
        }
        if (p->FileSiz > size || p->Offset > size - p->FileSiz) {
            say("kernel: PT_LOAD file extent past the end of the payload\n");
            return 0;
        }
        if (p->MemSiz < p->FileSiz) {
            say("kernel: PT_LOAD has MemSiz < FileSiz\n");
            return 0;
        }
        /* The loader copies to the link address with the firmware's page
         * tables still active, so the image has to live in the identity-
         * mapped, firmware-usable window below 4 GiB and above the low
         * 1 MiB that the firmware itself uses.  A payload linked anywhere
         * else is a silent triple fault, so it is refused here. */
        if (p->Vaddr < KERNEL_MIN_ADDR) {
            say("kernel: PT_LOAD below 1 MiB (0x");
            say_hex(p->Vaddr, 16);
            say("), refusing\n");
            return 0;
        }
        if (p->Vaddr + p->MemSiz > KERNEL_MAX_END || p->Vaddr + p->MemSiz < p->Vaddr) {
            say("kernel: PT_LOAD above the 4 GiB identity map, refusing\n");
            return 0;
        }
        {
            UINTN j;
            for (j = 0; j < i; j++) {
                Elf64_Phdr *q = (Elf64_Phdr *)((UINT8 *)ph + j * eh->PhEntSize);
                if (q->Type != PT_LOAD) {
                    continue;
                }
                if (p->Vaddr < q->Vaddr + q->MemSiz &&
                    q->Vaddr < p->Vaddr + p->MemSiz) {
                    say("kernel: overlapping PT_LOAD segments, refusing\n");
                    return 0;
                }
            }
        }
        if (p->Vaddr < lo) { lo = p->Vaddr; }
        if (p->Vaddr + p->MemSiz > hi) { hi = p->Vaddr + p->MemSiz; }
    }
    if (lo == ~0ULL) {
        say("kernel: no PT_LOAD segments\n");
        return 0;
    }
    /* Jumping to an entry point outside the copied image is the failure mode
     * that presents as an invalid-opcode exception somewhere unrelated. */
    {
        int entry_ok = 0;
        for (i = 0; i < eh->PhNum; i++) {
            Elf64_Phdr *p = (Elf64_Phdr *)((UINT8 *)ph + i * eh->PhEntSize);
            if (p->Type == PT_LOAD && eh->Entry >= p->Vaddr &&
                eh->Entry < p->Vaddr + p->MemSiz) {
                entry_ok = 1;
                break;
            }
        }
        if (!entry_ok) {
            say("kernel: e_entry 0x");
            say_hex(eh->Entry, 16);
            say(" is outside every PT_LOAD, refusing\n");
            return 0;
        }
    }
    /* Reserve [lo, hi) at its fixed link address.  The stub links at
     * 0x100000 which OVMF maps as conventional RAM post-EBS.
     *
     * The range is rounded *down* to a page first.  AllocatePages with
     * AllocateAddress requires a page-aligned address and returns
     * EFI_INVALID_PARAMETER otherwise, and a kernel whose first PT_LOAD
     * starts at 0x1000b0 (the ELF headers sit in front of .text) is not
     * aligned -- which made every handoff fail with "cannot reserve link
     * range" while looking like a memory-map problem. */
    {
        UINT64 lo_page = lo & ~(EFI_PAGE_SIZE - 1);
        UINT64 hi_page = (hi + EFI_PAGE_SIZE - 1) & ~(EFI_PAGE_SIZE - 1);
        UINT64 pages = (hi_page - lo_page) >> EFI_PAGE_SHIFT;
        UINT64 addr = lo_page;
        EFI_STATUS stt = bs->AllocatePages(AllocateAddress, EfiLoaderCode,
                                           (UINTN)pages, &addr);
        if (EFI_ERROR(stt) || addr != lo_page) {
            say("kernel: cannot reserve link range 0x");
            say_hex(lo_page, 16);
            say(" (");
            say_dec(pages);
            say(" pages)\n");
            say_memmap_covering(bs, lo_page, hi_page - lo_page);
            return 0;
        }
    }
    for (i = 0; i < eh->PhNum; i++) {
        Elf64_Phdr *p = (Elf64_Phdr *)((UINT8 *)ph + i * eh->PhEntSize);
        if (p->Type != PT_LOAD) {
            continue;
        }
        bs->CopyMem((void *)(UINTN)p->Vaddr,
                    (UINT8 *)file_data + p->Offset, p->FileSiz);
        if (p->MemSiz > p->FileSiz) {
            bs->SetMem((void *)(UINTN)(p->Vaddr + p->FileSiz),
                      (UINTN)(p->MemSiz - p->FileSiz), 0);
        }
    }
    /* Boot stack: allocate ANY 16 pages, pass top in rsp. */
    {
        UINT64 stk = 0;
        if (EFI_ERROR(bs->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                        BOOTKSTACK_SIZE / EFI_PAGE_SIZE, &stk))) {
            say("kernel: boot stack allocation failed\n");
            return 0;
        }
        *rsp_out = stk + BOOTKSTACK_SIZE;
    }
    entry = eh->Entry;
    *entry_out = entry;
    if (kphys_base_out) { *kphys_base_out = lo; }
    if (kphys_size_out) { *kphys_size_out = hi - lo; }
    say("RAVYNOS-KERNEL-LOAD ");
    say_dec(size);
    say(" bytes, image 0x");
    say_hex(lo, 16);
    say("-0x");
    say_hex(hi, 16);
    say(", entry 0x");
    say_hex(entry, 16);
    say("\n");
    return 1;
}

/* Parse a Mach-O 64 kernel (XNU) and record its segments.  The actual copy
 * into the kernel's fixed physical addresses is deferred to
 * xnu_copy_kernel() after ExitBootServices, because OVMF keeps its own
 * boot-services code inside the kernel's span.  On success returns the
 * physical entry point and allocates a boot stack; also reports the physical
 * base and span of the image for boot_args. */
static int load_macho_kernel_image(EFI_BOOT_SERVICES *bs, void *file_data, UINTN size,
                                   UINT64 *entry_out, UINT64 *rsp_out,
                                   UINT64 *kphys_base_out, UINT64 *kphys_size_out)
{
    mach_header_64_t *mh = (mach_header_64_t *)file_data;
    UINT8 *base = (UINT8 *)file_data;
    UINT8 *lc = base + sizeof(mach_header_64_t);
    UINTN i;
    UINT64 entry_vm = 0;
    UINT64 have_entry = 0;
    UINT64 min_phys = ~0ULL, max_phys = 0;
    UINT64 ncmds = 0;

    gKernelSegCount = 0;
    gKernelPayload = 0;
    gKernelPayloadSize = 0;
    gBootStackBase = 0;
    gBootDataStage = 0;
    gBootDataPhys = 0;
    gPostKernelEnd = 0;

    if (size < sizeof(mach_header_64_t) || mh->magic != MH_MAGIC_64 ||
        mh->cputype != CPU_TYPE_X86_64) {
        say("kernel: not a Mach-O 64 x86_64 image\n");
        return 0;
    }
    if (mh->filetype != MH_EXECUTE && mh->filetype != MH_FILESET) {
        say("kernel: Mach-O filetype not executable/fileset\n");
        return 0;
    }
    ncmds = mh->ncmds;
    if (sizeof(mach_header_64_t) + mh->sizeofcmds > size) {
        say("kernel: Mach-O load commands out of bounds\n");
        return 0;
    }

    /* First pass: load every LC_SEGMENT_64. */
    for (i = 0; i < ncmds; i++) {
        load_command_t *cmd = (load_command_t *)lc;
        if (lc + sizeof(load_command_t) > base + size) {
            break;
        }
        if (cmd->cmd == LC_SEGMENT_64) {
            segment_command_64_t *seg = (segment_command_64_t *)lc;
            UINT64 vmaddr, vmsize, fileoff, filesize;
            UINT64 phys;

            if (lc + sizeof(segment_command_64_t) > base + size) {
                say("kernel: truncated LC_SEGMENT_64\n");
                return 0;
            }
            vmaddr = seg->vmaddr;
            vmsize = seg->vmsize;
            fileoff = seg->fileoff;
            filesize = seg->filesize;

            if (vmsize == 0) {
                lc += cmd->cmdsize;
                continue;
            }
            if (vmaddr < VM_MIN_KERNEL_ADDRESS) {
                say("kernel: Mach-O segment below kernel base, refusing\n");
                return 0;
            }
            if (fileoff > size || filesize > size - fileoff) {
                say("kernel: Mach-O segment file extent past EOF\n");
                return 0;
            }
            if (filesize > vmsize) {
                say("kernel: Mach-O segment FileSiz > MemSiz\n");
                return 0;
            }

            phys = vmaddr - VM_MIN_KERNEL_ADDRESS;
            if (phys + vmsize < phys) {
                say("kernel: Mach-O segment overflow\n");
                return 0;
            }
            if (gKernelSegCount >= MAX_KERNEL_SEGS) {
                say("kernel: too many Mach-O segments\n");
                return 0;
            }
            gKernelSegs[gKernelSegCount].phys = phys;
            gKernelSegs[gKernelSegCount].fileoff = fileoff;
            gKernelSegs[gKernelSegCount].filesize = filesize;
            gKernelSegs[gKernelSegCount].vmsize = vmsize;
            gKernelSegCount++;

            if (phys < min_phys) { min_phys = phys; }
            if (phys + vmsize > max_phys) { max_phys = phys + vmsize; }
        }
        if (cmd->cmdsize == 0) {
            break;
        }
        lc += cmd->cmdsize;
    }

    /* Second pass: entry point. */
    lc = base + sizeof(mach_header_64_t);
    for (i = 0; i < ncmds; i++) {
        load_command_t *cmd = (load_command_t *)lc;
        if (cmd->cmdsize == 0) {
            break;
        }
        if (cmd->cmd == LC_UNIXTHREAD) {
            /* flavor, count, then x86_thread_state64_t; rip is field 16. */
            UINT32 *p = (UINT32 *)(lc + 8);
            UINT32 flavor = p[0];
            UINT32 count = p[1];
            UINT64 *state = (UINT64 *)(lc + 16);
            (void)flavor;
            (void)count;
            if (count >= 17 * 2) {
                entry_vm = state[16];
                have_entry = 1;
            }
        } else if (cmd->cmd == LC_MAIN) {
            /* entryoff is first field after cmd/cmdsize. */
            UINT64 *off = (UINT64 *)(lc + 8);
            entry_vm = *off + (min_phys + VM_MIN_KERNEL_ADDRESS); /* approx */
            /* LC_MAIN entryoff is relative to __TEXT; better not use. */
            have_entry = 0; /* prefer UNIXTHREAD */
        }
        lc += cmd->cmdsize;
    }

    if (!have_entry) {
        /* Fall back: try any LC_MAIN entryoff as absolute vm? Skip. */
        say("kernel: no LC_UNIXTHREAD entry found\n");
        return 0;
    }
    if (entry_vm < VM_MIN_KERNEL_ADDRESS) {
        say("kernel: Mach-O entry below kernel base\n");
        return 0;
    }

    /* Boot stack: high, because the copy after ExitBootServices must not run
     * on firmware stack memory that lies inside the kernel's span. */
    {
        EFI_PHYSICAL_ADDRESS stk = KERNEL_PAYLOAD_MAX_ADDR;
        if (EFI_ERROR(bs->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                        BOOTKSTACK_SIZE / EFI_PAGE_SIZE, &stk))) {
            say("kernel: boot stack allocation failed\n");
            return 0;
        }
        gBootStackBase = (UINT64)stk;
        *rsp_out = (UINT64)stk + BOOTKSTACK_SIZE;
    }

    gKernelPayload = base;
    gKernelPayloadSize = size;
    *entry_out = entry_vm - VM_MIN_KERNEL_ADDRESS;
    if (kphys_base_out) { *kphys_base_out = min_phys; }
    if (kphys_size_out) { *kphys_size_out = max_phys - min_phys; }

    say("RAVYNOS-KERNEL-LOAD (macho) entry 0x");
    say_hex(*entry_out, 16);
    say(" (");
    say_dec(gKernelSegCount);
    say(" segments deferred)\n");
    return 1;
}

/* Called from xnu_handoff.S after ExitBootServices, already on the high boot
 * stack.  Boot services are gone, so this must use plain memory operations
 * only -- no CopyMem/SetMem. */
void xnu_copy_kernel(void)
{
    UINTN i;

    serial_puts("handoff: copying kernel segments\n");
    for (i = 0; i < gKernelSegCount; i++) {
        volatile UINT8 *dst = (volatile UINT8 *)(UINTN)gKernelSegs[i].phys;
        const volatile UINT8 *src =
            (const volatile UINT8 *)(gKernelPayload + gKernelSegs[i].fileoff);
        UINTN n = (UINTN)gKernelSegs[i].filesize;

        while (n--) {
            *dst++ = *src++;
        }
        n = (UINTN)(gKernelSegs[i].vmsize - gKernelSegs[i].filesize);
        while (n--) {
            *dst++ = 0;
        }
    }
    if (gBootDataStage) {
        volatile UINT64 *dst = (volatile UINT64 *)(UINTN)gBootDataPhys;
        const volatile UINT64 *src = (const volatile UINT64 *)(const void *)gBootDataStage;
        UINTN n = (UINTN)(XNU_BOOT_DATA_SIZE / sizeof(UINT64));
        while (n--) {
            *dst++ = *src++;
        }
    }
    for (i = 0; i < gStagedKextCount; i++) {
        volatile UINT64 *dst =
            (volatile UINT64 *)(UINTN)gStagedKexts[i].dest_phys;
        const volatile UINT64 *src =
            (const volatile UINT64 *)(const void *)gStagedKexts[i].stage;
        UINTN n = (UINTN)(gStagedKexts[i].blob_size / sizeof(UINT64));
        while (n--) {
            *dst++ = *src++;
        }
    }
    serial_puts("handoff: kernel segments copied\n");
}

/* Harvest conventional memory into the bootinfo block at BOOTINFO_ADDR.
 *
 * The block lives at a fixed physical address because the kernel hands over
 * before it has a heap: writing to 0x90000 without reserving it is a write to
 * firmware-owned memory, and the firmware is free to reuse that page (or to
 * have it mapped read-only) until ExitBootServices.  Returns 0 when the block
 * could not be reserved, so the caller can refuse the handoff instead of
 * jumping to a kernel with a bootinfo pointer to somebody else's memory. */
static int write_bootinfo(EFI_BOOT_SERVICES *bs)
{
    UINTN mapsize = 0, descsize = 0, key = 0;
    UINT32 descver = 0;
    EFI_MEMORY_DESCRIPTOR *map = 0;
    bootinfo_t *bi = (bootinfo_t *)(UINTN)BOOTINFO_ADDR;
    UINTN off;
    UINT64 bi_addr = BOOTINFO_ADDR;

    if (EFI_ERROR(bs->AllocatePages(AllocateAddress, EfiLoaderData, 1, &bi_addr)) ||
        bi_addr != BOOTINFO_ADDR) {
        say("bootinfo: cannot reserve page 0x");
        say_hex(BOOTINFO_ADDR, 16);
        say("\n");
        return 0;
    }

    bs->GetMemoryMap(&mapsize, 0, &key, &descsize, &descver);
    mapsize += 2 * descsize;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, mapsize, (void **)&map))) {
        return 0;
    }
    if (EFI_ERROR(bs->GetMemoryMap(&mapsize, map, &key, &descsize, &descver))) {
        bs->FreePool(map);
        return 0;
    }
    bs->SetMem(bi, sizeof(*bi), 0);
    bi->magic = BOOTINFO_MAGIC;
    bi->version = BOOTINFO_VERSION;
    bi->memmap_size = descsize;
    for (off = 0; off < mapsize; off += descsize) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + off);
        if (d->Type == EfiConventionalMemory && d->NumberOfPages &&
            bi->entry_count < 64) {
            bi->entries[bi->entry_count].base = d->PhysicalStart;
            bi->entries[bi->entry_count].pages = d->NumberOfPages;
            bi->entry_count++;
        }
    }
    bs->FreePool(map);
    (void)key; /* caller re-fetches key right before ExitBootServices */
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Minimal flattened device tree (D5)                                       */
/* ---------------------------------------------------------------------- */
#define XNU_RANDOM_SEED_SIZE 64u

/* Populate 64 bytes for /chosen/random-seed (Kernel/xnu/osfmk/prng/prng_random.c:56-65,
 * Kernel/xnu/pexpert/gen/pe_gen.c:141-172).  Uses RDRAND when CPUID.1:ECX[30]
 * advertises it, with a SplitMix64-mixed RDTSC fallback for CPUs without RDRAND
 * (such as QEMU -cpu Penryn). */
static void fill_random_seed(UINT8 out[XNU_RANDOM_SEED_SIZE])
{
    UINT32 eax = 1, ebx = 0, ecx = 0, edx = 0;
    int have_rdrand;
    UINT64 state;
    UINT32 lo = 0, hi = 0;
    UINTN i;
    UINT8 nonzero = 0;

    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    have_rdrand = (ecx & (1u << 30)) != 0;

    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    state = (((UINT64)hi << 32) | lo) ^ 0xA5A55A5AF0F00F0FULL;

    for (i = 0; i < XNU_RANDOM_SEED_SIZE / sizeof(UINT64); i++) {
        UINT64 v = 0;
        UINT8 ok = 0;
        if (have_rdrand) {
            UINTN tries;
            for (tries = 0; tries < 16 && !ok; tries++) {
                /* rdrand %rax */
                __asm__ volatile(".byte 0x48, 0x0f, 0xc7, 0xf0; setc %1"
                                 : "=a"(v), "=qm"(ok)
                                 :
                                 : "cc");
            }
        }
        if (!ok) {
            __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
            state ^= ((UINT64)hi << 32) | lo;
            state += 0x9E3779B97F4A7C15ULL;
            v = state;
            v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
            v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
            v ^= (v >> 31);
        }
        ((UINT64 *)(void *)out)[i] = v;
    }

    for (i = 0; i < XNU_RANDOM_SEED_SIZE; i++) {
        nonzero |= out[i];
    }
    if (!nonzero) {
        out[0] = 1;
    }
}

static int ranges_overlap(UINT64 a0, UINT64 alen, UINT64 b0, UINT64 blen)
{
    return (a0 < b0 + blen) && (b0 < a0 + alen);
}

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b)
{
    UINTN i;
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        if (a->Data4[i] != b->Data4[i]) {
            return 0;
        }
    }
    return 1;
}

/* Locate ACPI RSDP in EFI_SYSTEM_TABLE.ConfigurationTable, preferring
 * EFI_ACPI_20_TABLE_GUID so ACPIPlatformExpert::parseACPI()
 * (Kernel/Extensions/IOACPIFamily/ACPIPlatformExpert.cpp:259) sees a non-zero
 * rsdp->xsdtAddress. */
static UINT64 find_acpi_rsdp(EFI_SYSTEM_TABLE *st)
{
    static const EFI_GUID acpi20_guid = EFI_ACPI_20_TABLE_GUID;
    static const EFI_GUID acpi10_guid = EFI_ACPI_TABLE_GUID;
    const EFI_CONFIGURATION_TABLE *ct;
    UINT64 fallback = 0;
    UINTN i;

    if (!st || !st->ConfigurationTable || !st->NumberOfTableEntries) {
        return 0;
    }
    ct = (const EFI_CONFIGURATION_TABLE *)st->ConfigurationTable;
    for (i = 0; i < st->NumberOfTableEntries; i++) {
        if (guid_eq(&ct[i].VendorGuid, &acpi20_guid) && ct[i].VendorTable) {
            return (UINT64)(UINTN)ct[i].VendorTable;
        }
        if (!fallback && guid_eq(&ct[i].VendorGuid, &acpi10_guid) &&
            ct[i].VendorTable) {
            fallback = (UINT64)(UINTN)ct[i].VendorTable;
        }
    }
    return fallback;
}

static void format_driver_prop_name(char out[16], UINT32 phys)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    out[0] = 'D'; out[1] = 'r'; out[2] = 'i'; out[3] = 'v';
    out[4] = 'e'; out[5] = 'r'; out[6] = '-';
    for (i = 7; i >= 0; i--) {
        out[7 + i] = hex[phys & 0xFu];
        phys >>= 4;
    }
    out[15] = 0;
}

/* Allocate the 256 KiB staging buffer in high EfiLoaderData and choose a
 * reclaimable contiguous physical destination immediately above the kernel's
 * Mach-O span for both the 256 KiB boot-data window and all staged kexts. */
static int stage_boot_data_window(EFI_BOOT_SERVICES *bs, UINT64 kphys_base,
                                  UINT64 kphys_size)
{
    EFI_PHYSICAL_ADDRESS stage_phys = KERNEL_PAYLOAD_MAX_ADDR;
    UINTN stage_pages = (UINTN)(XNU_BOOT_DATA_SIZE >> EFI_PAGE_SHIFT);
    UINTN mapsize = 0, descsize = 0, key = 0;
    UINT32 descver = 0;
    EFI_MEMORY_DESCRIPTOR *map = 0;
    UINT64 cand = (kphys_base + kphys_size + (EFI_PAGE_SIZE - 1)) &
                  ~(UINT64)(EFI_PAGE_SIZE - 1);
    UINT64 payload_base = (UINT64)(UINTN)gKernelPayload;
    UINT64 total_window = XNU_BOOT_DATA_SIZE;
    UINTN i;

    for (i = 0; i < gStagedKextCount; i++) {
        total_window += gStagedKexts[i].blob_size;
    }

    if (EFI_ERROR(bs->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                    stage_pages, &stage_phys))) {
        say("kernel: cannot stage boot-data buffer\n");
        return 0;
    }
    if (ranges_overlap((UINT64)stage_phys, XNU_BOOT_DATA_SIZE,
                       kphys_base, kphys_size)) {
        bs->FreePages(stage_phys, stage_pages);
        say("kernel: boot-data stage overlaps kernel span\n");
        return 0;
    }
    bs->SetMem((void *)(UINTN)stage_phys, (UINTN)XNU_BOOT_DATA_SIZE, 0);

    bs->GetMemoryMap(&mapsize, 0, &key, &descsize, &descver);
    mapsize += 2 * descsize;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, mapsize, (void **)&map)) ||
        EFI_ERROR(bs->GetMemoryMap(&mapsize, map, &key, &descsize, &descver))) {
        if (map) { bs->FreePool(map); }
        bs->FreePages(stage_phys, stage_pages);
        say("kernel: cannot inspect memory map for boot-data placement\n");
        return 0;
    }

    while (cand + total_window <= XNU_BOOT_DATA_MAX_END) {
        UINT64 next_cand = 0;
        UINT64 pa, win_end;
        int ok = 1;
        int bumped = 0;

        if (ranges_overlap(cand, total_window,
                           (UINT64)stage_phys, XNU_BOOT_DATA_SIZE)) {
            cand = ((UINT64)stage_phys + XNU_BOOT_DATA_SIZE +
                    (EFI_PAGE_SIZE - 1)) & ~(UINT64)(EFI_PAGE_SIZE - 1);
            continue;
        }
        if (gKernelPayloadSize &&
            ranges_overlap(cand, total_window,
                           payload_base, (UINT64)gKernelPayloadSize)) {
            cand = (payload_base + (UINT64)gKernelPayloadSize +
                    (EFI_PAGE_SIZE - 1)) & ~(UINT64)(EFI_PAGE_SIZE - 1);
            continue;
        }
        if (gBootStackBase &&
            ranges_overlap(cand, total_window,
                           gBootStackBase, BOOTKSTACK_SIZE)) {
            cand = (gBootStackBase + BOOTKSTACK_SIZE +
                    (EFI_PAGE_SIZE - 1)) & ~(UINT64)(EFI_PAGE_SIZE - 1);
            continue;
        }
        if (gLoaderImageSize &&
            ranges_overlap(cand, total_window,
                           gLoaderImageBase, gLoaderImageSize)) {
            cand = (gLoaderImageBase + gLoaderImageSize +
                    (EFI_PAGE_SIZE - 1)) & ~(UINT64)(EFI_PAGE_SIZE - 1);
            continue;
        }
        for (i = 0; i < gStagedKextCount; i++) {
            UINT64 ks = (UINT64)(UINTN)gStagedKexts[i].stage;
            UINT64 kl = gStagedKexts[i].blob_size;
            if (ranges_overlap(cand, total_window, ks, kl)) {
                cand = (ks + kl + (EFI_PAGE_SIZE - 1)) &
                       ~(UINT64)(EFI_PAGE_SIZE - 1);
                bumped = 1;
                break;
            }
        }
        if (bumped) {
            continue;
        }

        pa = cand;
        win_end = cand + total_window;
        while (pa < win_end) {
            UINTN off;
            int covered = 0;

            for (off = 0; off < mapsize; off += descsize) {
                EFI_MEMORY_DESCRIPTOR *d =
                    (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + off);
                UINT64 dend = d->PhysicalStart +
                              d->NumberOfPages * EFI_PAGE_SIZE;
                if (pa >= d->PhysicalStart && pa < dend) {
                    if ((d->Attribute & EFI_MEMORY_RUNTIME) == 0 &&
                        (d->Type == EfiConventionalMemory ||
                         d->Type == EfiBootServicesCode ||
                         d->Type == EfiBootServicesData ||
                         d->Type == EfiLoaderCode ||
                         d->Type == EfiLoaderData)) {
                        pa = (dend < win_end) ? dend : win_end;
                        covered = 1;
                    } else {
                        next_cand = (dend + (EFI_PAGE_SIZE - 1)) &
                                    ~(UINT64)(EFI_PAGE_SIZE - 1);
                    }
                    break;
                }
            }
            if (!covered) {
                ok = 0;
                if (next_cand <= cand) {
                    next_cand = pa + EFI_PAGE_SIZE;
                }
                break;
            }
        }
        if (ok) {
            UINT64 kcur = cand + XNU_BOOT_DATA_SIZE;
            bs->FreePool(map);
            gBootDataStage = (UINT8 *)(UINTN)stage_phys;
            gBootDataPhys = cand;
            for (i = 0; i < gStagedKextCount; i++) {
                booter_kext_file_info_t *hdr =
                    (booter_kext_file_info_t *)(void *)gStagedKexts[i].stage;
                gStagedKexts[i].dest_phys = kcur;
                hdr->infoDictPhysAddr =
                    (UINT32)(kcur + gStagedKexts[i].plist_off);
                hdr->infoDictLength = gStagedKexts[i].plist_len;
                hdr->executablePhysAddr = gStagedKexts[i].exec_len
                    ? (UINT32)(kcur + gStagedKexts[i].exec_off) : 0u;
                hdr->executableLength = gStagedKexts[i].exec_len;
                hdr->bundlePathPhysAddr =
                    (UINT32)(kcur + gStagedKexts[i].path_off);
                hdr->bundlePathLength = gStagedKexts[i].path_len;
                kcur += gStagedKexts[i].blob_size;
            }
            gPostKernelEnd = kcur;
            return 1;
        }
        cand = next_cand;
    }

    bs->FreePool(map);
    bs->FreePages(stage_phys, stage_pages);
    say("kernel: no reclaimable RAM window above kernel for boot data\n");
    return 0;
}

static UINT32 build_device_tree(EFI_BOOT_SERVICES *bs, EFI_SYSTEM_TABLE *st,
                                UINT64 kphys_base, UINT64 kphys_size)
{
    UINT8 *buf, *p;
    UINT8 seed[XNU_RANDOM_SEED_SIZE];
    UINT64 rsdp_phys = find_acpi_rsdp(st);
    UINTN i;

    if (rsdp_phys) {
        say("loader: ACPI RSDP 0x");
        say_hex(rsdp_phys, 16);
        say("\n");
    } else {
        say("loader: warning: no ACPI RSDP in EFI ConfigurationTable\n");
    }

    if (!stage_boot_data_window(bs, kphys_base, kphys_size)) {
        return 0;
    }
    buf = gBootDataStage + XNU_BD_DT_OFF;
    bs->SetMem(buf, (UINTN)XNU_BD_DT_CAP, 0);
    fill_random_seed(seed);
    p = buf;

#define WU32(x) do { *(UINT32 *)p = (x); p += 4; } while (0)
#define WNAME(nm) do {         bs->SetMem(p, 32, 0);         { UINTN _i = 0; while ((nm)[_i] && _i < 31) { p[_i] = (UINT8)(nm)[_i]; _i++; } }         p += 32;     } while (0)
#define WPROP_NAME(nm) do { WNAME(nm); } while (0)
#define WPROP_STR(pname, sval) do {         WNAME(pname);         *(UINT32 *)p = (UINT32)(sizeof(sval)); p += 4;         bs->CopyMem(p, (sval), sizeof(sval));         p += ((sizeof(sval) + 3) & ~3u);     } while (0)
#define WPROP_U64(pname, val) do {         WNAME(pname);         *(UINT32 *)p = 8; p += 4;         *(UINT64 *)p = (val); p += 8;     } while (0)
#define WPROP_BYTES(pname, ptr, len) do {         WNAME(pname);         *(UINT32 *)p = (UINT32)(len); p += 4;         bs->CopyMem(p, (ptr), (len));         p += (((UINTN)(len) + 3u) & ~3u);     } while (0)

    /* root: 2 props (name, compatible="ACPI" for IODeviceTreeSupport.cpp:194
     * and IOACPIFamily/Info.plist:34), 3 children (chosen, efi, ACPI). */
    WU32(2); WU32(3);
    WPROP_STR("name", "");
    WPROP_STR("compatible", "ACPI");

    /* chosen: PE_get_random_seed (pexpert/gen/pe_gen.c:147-170) reads
     * /chosen/random-seed and zeroes its bytes in place. */
    WU32(2); WU32(1);
    WPROP_STR("name", "chosen");
    WPROP_BYTES("random-seed", seed, sizeof(seed));
    /* memory-map: KLDBootstrap::readBooterExtensions (libsa/bootstrap.cpp:607)
     * reads "Driver-<hex>" _DeviceTreeBuffer entries here. */
    WU32(1u + (UINT32)gStagedKextCount); WU32(0);
    WPROP_STR("name", "memory-map");
    for (i = 0; i < gStagedKextCount; i++) {
        char drv_name[16];
        dt_memmap_entry_t dte;
        dte.paddr = (UINT32)gStagedKexts[i].dest_phys;
        dte.length = gStagedKexts[i].blob_size;
        format_driver_prop_name(drv_name, dte.paddr);
        WPROP_BYTES(drv_name, &dte, sizeof(dte));
    }

    /* efi */
    WU32(1); WU32(1);
    WPROP_STR("name", "efi");
    /* platform */
    WU32(2); WU32(0);
    WPROP_STR("name", "platform");
    WPROP_U64("FSBFrequency", 100000000ULL);

    /* ACPI: ACPIPlatformExpert::parseACPI (ACPIPlatformExpert.cpp:227-269)
     * resolves /ACPI in gIODTPlane and reads an 8-byte RSDP physical address. */
    WU32(rsdp_phys ? 2u : 1u); WU32(0);
    WPROP_STR("name", "ACPI");
    if (rsdp_phys) {
        WPROP_U64("RSDP", rsdp_phys);
    }

#undef WU32
#undef WNAME
#undef WPROP_STR
#undef WPROP_U64
#undef WPROP_BYTES

    gDTPhys = (UINT32)(gBootDataPhys + XNU_BD_DT_OFF);
    gDTSize = (UINT32)(p - buf);
    return gDTPhys;
}

static UINT32 build_boot_args(EFI_BOOT_SERVICES *bs, EFI_SYSTEM_TABLE *st,
                              UINT64 kphys_base, UINT64 kphys_size)
{
    UINT64 ba_phys = gBootDataPhys + XNU_BD_BA_OFF;
    UINT64 efistub_phys = gBootDataPhys + XNU_BD_EFISTUB_OFF;
    UINT64 map_phys = gBootDataPhys + XNU_BD_MEMMAP_OFF;
    boot_args_t *ba;
    UINTN mapsize = 0, descsize = 0, key = 0;
    UINT32 descver = 0;
    EFI_MEMORY_DESCRIPTOR *map = 0;
    UINT64 total_mem = 0;
    UINTN off;

    (void)st;
    (void)kphys_size;

    if (!gBootDataStage || !gBootDataPhys) {
        return 0;
    }

    ba = (boot_args_t *)(void *)(gBootDataStage + XNU_BD_BA_OFF);
    bs->SetMem(ba, sizeof(*ba), 0);
    /* Zeroed EFI_SYSTEM_TABLE_64 stub inside [kaddr, kaddr+ksize): efi_init()
     * (osfmk/i386/AT386/model_dep.c:376-379,554) dereferences
     * ml_static_ptovirt(args->efiSystemTable), sees Hdr.Signature == 0, and
     * leaves gPEEFISystemTable/gPEEFIRuntimeServices NULL cleanly
     * (osfmk/i386/pal_routines.c:171, IOACPIFamily/EFINVRAM.cpp:126). */
    bs->SetMem(gBootDataStage + XNU_BD_EFISTUB_OFF, EFI_PAGE_SIZE, 0);

    map = (EFI_MEMORY_DESCRIPTOR *)(void *)(gBootDataStage + XNU_BD_MEMMAP_OFF);
    mapsize = (UINTN)XNU_BD_MEMMAP_CAP;
    if (EFI_ERROR(bs->GetMemoryMap(&mapsize, map, &key, &descsize, &descver))) {
        say("kernel: EFI memory map does not fit the boot-data window\n");
        return 0;
    }

    ba->Revision = kBootArgsRevision;
    ba->Version = kBootArgsVersion;
    ba->efiMode = kBootArgsEfiMode64;
    /* CommandLine: built-in default plus any extra args staged from the
     * ESP \boot-args file (Apple's NVRAM boot-args analogue). */
    {
        const char *cmd = "-v debug=0x8 serial=3 keepsyms=1";
        UINTN i = 0;
        while (cmd[i] && i < BOOT_LINE_LENGTH - 1) {
            ba->CommandLine[i] = cmd[i];
            i++;
        }
        if (gBootArgsFileLen) {
            UINTN j = 0;
            if (i && ba->CommandLine[i - 1] != ' ' && i < BOOT_LINE_LENGTH - 1) {
                ba->CommandLine[i++] = ' ';
            }
            while (j < gBootArgsFileLen && i < BOOT_LINE_LENGTH - 1) {
                ba->CommandLine[i++] = gBootArgsFile[j++];
            }
        }
        ba->CommandLine[i] = 0;
    }
    ba->MemoryMap = (UINT32)map_phys;
    ba->MemoryMapSize = (UINT32)mapsize;
    ba->MemoryMapDescriptorSize = (UINT32)descsize;
    ba->MemoryMapDescriptorVersion = descver;
    ba->deviceTreeP = gDTPhys;
    ba->deviceTreeLength = gDTSize;
    ba->kaddr = (UINT32)kphys_base;
    ba->ksize = (UINT32)(gPostKernelEnd - kphys_base);
    ba->efiSystemTable = (UINT32)efistub_phys;
    ba->PhysicalMemorySize = 0; /* filled below */
    ba->FSBFrequency = 100000000ULL;

    for (off = 0; off < mapsize; off += descsize) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + off);
        if (d->Type == EfiConventionalMemory &&
            (d->Attribute & EFI_MEMORY_RUNTIME) == 0) {
            total_mem += d->NumberOfPages * EFI_PAGE_SIZE;
        }
        if (d->Attribute & EFI_MEMORY_RUNTIME) {
            /* Without SetVirtualAddressMap(), VirtualStart == 0 makes
             * efi_init() (model_dep.c:529-545) map runtime pages over
             * VM_MIN_KERNEL_ADDRESS (0xffffff8000000000), clobbering __HIB
             * and the boot stack.  Preserve non-conventional memory types so
             * i386_vm_init() (i386_vm_init.c:460-470,529-532) still excludes
             * firmware runtime physical pages from usable RAM. */
            if (d->Type == EfiLoaderCode || d->Type == EfiLoaderData ||
                d->Type == EfiBootServicesCode ||
                d->Type == EfiBootServicesData ||
                d->Type == EfiConventionalMemory) {
                d->Type = EfiReservedMemoryType;
            }
            d->Attribute &= ~EFI_MEMORY_RUNTIME;
        }
    }
    ba->PhysicalMemorySize = total_mem;

    (void)key;
    return (UINT32)ba_phys;
}

/* Final handoff: fresh map key -> ExitBootServices -> jump.  Never returns
 * on success. */
static void handoff_to_kernel(EFI_BOOT_SERVICES *bs, EFI_HANDLE image_handle,
                              UINT64 entry, UINT64 rsp)
{
    UINTN mapsize = 0, descsize = 0, key = 0;
    UINT32 descver = 0;
    EFI_MEMORY_DESCRIPTOR *map = 0;
    EFI_STATUS stt;

    /* Spec rule: the LAST GetMemoryMap before EBS supplies the valid key. */
    bs->GetMemoryMap(&mapsize, 0, &key, &descsize, &descver);
    mapsize += 2 * descsize;
    bs->AllocatePool(EfiLoaderData, mapsize, (void **)&map);
    stt = bs->GetMemoryMap(&mapsize, map, &key, &descsize, &descver);
    if (EFI_ERROR(stt)) {
        say("handoff: final GetMemoryMap failed\n");
        return;
    }
    say("handoff: ExitBootServices...\n");
    stt = bs->ExitBootServices(image_handle, key);
    if (EFI_ERROR(stt)) {
        say("handoff: ExitBootServices refused (stale key?)\n");
        return;
    }
    /* No boot services may be touched from here on. */
    __asm__ volatile("cli");
    __asm__ volatile(
        "mov %0, %%rsp\n\t"
        "mov %1, %%rdi\n\t"
        "jmp *%2\n"
        :
        : "r"(rsp), "r"((UINT64)BOOTINFO_ADDR), "r"(entry)
        : "memory");
    for (;;) { __asm__ volatile("hlt"); }
}

/* ---------------------------------------------------------------------- */
/* A note on the legacy "jump to 0xB0000" path -- removed                  */
/*                                                                         */
/* An earlier revision carried a rescue entry here that assumed OVMF's     */
/* UEFI Misc Device fallback maps the first disk sectors at 0xB0000 and    */
/* jumps to them, and tried to recover an EFI_SYSTEM_TABLE by scanning the */
/* firmware stack after the resulting #UD.  It could never have run: the   */
/* firmware's exception handler reports the fault and parks the CPU, so no */
/* code of ours is entered on that path -- and the CI logs show the        */
/* firmware never loading this image at all.  The block was unreachable    */
/* code that made the boot log harder to interpret, so it is gone.  The    */
/* real diagnosis is in PROGRESS.md (rev 17): the boot option BDS starts   */
/* has no file node, i.e. firmware never mounted the volume, which is an   */
/* image-layout problem on our side, not a payload problem.                */
/* ---------------------------------------------------------------------- */

/* ---------------------------------------------------------------------- */
/* Chain-load /EFI/SHELL.EFI from the volume this image was started from   */
/*                                                                         */
/* We rebuild the file device path from our own LoadedImage protocol:      */
/*   [parent path up to (but excluding) any FilePath node] +               */
/*   MEDIA_HARDDRIVE(partition info copied from our own path, when we were */
/*   loaded from a disk partition) or nothing (loaded from a CD-ROM/volume */
/*   already named by the remaining nodes) +                               */
/*   FILEPATH("\\EFI\\SHELL.EFI") + END                                     */
/* Then BootServices->LoadImage(BootPolicy=TRUE, parent=us, that path) and  */
/* StartImage.  No filesystem parsing here: the firmware's FAT driver does  */
/* all of it -- that is exactly what this step is meant to prove.           */
/* ---------------------------------------------------------------------- */

#define SHELL_PATH u"\\EFI\\SHELL.EFI"

static UINTN dp_text_len(const CHAR16 *s)
{
    UINTN n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

/* Copy our harddrive-node partition info into the shell path, if present. */
static int find_harddrive_node(EFI_DEVICE_PATH_HEADER *dp,
                               HARDDRIVE_DEVICE_PATH **out)
{
    EFI_DEVICE_PATH_HEADER *h = dp;
    while (h && !(h->Type == 127 && h->SubType == 0xFF)) {
        if (h->Type == MEDIA_DEVICE_PATH && h->SubType == MEDIA_HARDDRIVE_DP) {
            *out = (HARDDRIVE_DEVICE_PATH *)h;
            return 1;
        }
        h = (EFI_DEVICE_PATH_HEADER *)((UINT8 *)h +
                                       ((UINT32)h->Length));
    }
    return 0;
}

static EFI_STATUS build_shell_path(EFI_BOOT_SERVICES *bs,
                                   EFI_LOADED_IMAGE_PROTOCOL *li,
                                   EFI_DEVICE_PATH_HEADER **result)
{
    EFI_DEVICE_PATH_HEADER *h;
    HARDDRIVE_DEVICE_PATH *hd = 0;
    const CHAR16 *name = SHELL_PATH;
    UINTN name_chars = dp_text_len(name);
    UINTN tail_len;
    UINTN head_len = 0;
    UINT8 *buf;
    UINTN off = 0;

    /* Tail = optional harddrive node + file path node + end node. */
    tail_len = 4 + (name_chars + 1) * 2;
    if (find_harddrive_node(li->FilePath, &hd)) {
        tail_len += sizeof(HARDDRIVE_DEVICE_PATH);
    } else {
        hd = 0;   /* find_harddrive_node may have written a stale pointer */
    }

    /* Head = everything before any harddrive/file path node of our own path
     * (the controller/pci/cdrom part).  For the fallback path (\EFI\BOOT\
     * BOOTX64.EFI under a volume node) the head already names the volume,
     * so appending just the file node is correct. */
    for (h = li->FilePath;
         h && !(h->Type == 127 && h->SubType == 0xFF);
         h = (EFI_DEVICE_PATH_HEADER *)((UINT8 *)h + h->Length)) {
        if (h->Type == MEDIA_DEVICE_PATH &&
            (h->SubType == MEDIA_HARDDRIVE_DP || h->SubType == 0x04 /*FILE*/)) {
            break;
        }
        head_len += h->Length;
    }

    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, head_len + tail_len + 4,
                                   (void **)&buf))) {
        return EFI_UNSUPPORTED;
    }

    /* copy head */
    {
        EFI_DEVICE_PATH_HEADER *s = li->FilePath;
        UINTN copied = 0;
        while (copied < head_len) {
            bs->CopyMem(buf + copied, s, s->Length);
            copied += s->Length;
            s = (EFI_DEVICE_PATH_HEADER *)((UINT8 *)s + s->Length);
        }
    }
    off = head_len;

    /* harddrive node, if we booted from a GPT partition */
    if (hd) {
        HARDDRIVE_DEVICE_PATH *n = (HARDDRIVE_DEVICE_PATH *)(buf + off);
        bs->CopyMem(n, hd, sizeof(*hd));
        off += sizeof(*hd);
    }

    /* file path node */
    {
        FILEPATH_DEVICE_PATH *fp = (FILEPATH_DEVICE_PATH *)(buf + off);
        UINTN i;
        fp->Hdr.Type = MEDIA_DEVICE_PATH;
        fp->Hdr.SubType = 0x04;                 /* MEDIA_FILEPATH_DP */
        fp->Hdr.Length = (UINT16)(4 + (name_chars + 1) * 2);
        for (i = 0; i <= name_chars; i++) {
            fp->FileName[i] = name[i];
        }
        off += fp->Hdr.Length;
    }

    /* end node */
    buf[off + 0] = 0xFF;
    buf[off + 1] = 0xFF;
    buf[off + 2] = 0;
    buf[off + 3] = 0;

    *result = (EFI_DEVICE_PATH_HEADER *)buf;
    return EFI_SUCCESS;
}

static void try_start_shell(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    static EFI_GUID lifo_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    EFI_DEVICE_PATH_HEADER *path = 0;
    EFI_HANDLE shell_handle = 0;
    EFI_STATUS status;

    if (!bs) {
        say("shell: no boot services (legacy rescue), staying in loader\n");
        return;
    }
    status = bs->HandleProtocol(image_handle, &lifo_guid, (void **)&li);
    if (EFI_ERROR(status) || !li) {
        say("shell: no LoadedImage protocol, cannot locate volume\n");
        return;
    }
    status = build_shell_path(bs, li, &path);
    if (EFI_ERROR(status)) {
        say("shell: device-path build failed\n");
        return;
    }
    status = bs->LoadImage(1 /*BootPolicy*/, image_handle, path, 0, 0,
                           &shell_handle);
    if (EFI_ERROR(status)) {
        say("shell: LoadImage(/EFI/SHELL.EFI) failed -- not on ESP?\n");
        return;
    }
    say("RAVYNOS-SHELL-LOADED\n");
    status = bs->StartImage(shell_handle, 0, 0);
    if (EFI_ERROR(status)) {
        say("shell: StartImage failed\n");
    } else {
        say("shell: exited cleanly\n");
    }
}

extern void xnu_handoff(void);

static void handoff_to_xnu(EFI_BOOT_SERVICES *bs, EFI_HANDLE image_handle,
                           UINT64 entry, UINT32 boot_args_phys, UINT64 rsp)
{
    UINTN mapsize = 0, descsize = 0, key = 0;
    UINT32 descver = 0;
    EFI_MEMORY_DESCRIPTOR *map = 0;
    EFI_STATUS stt;

    bs->GetMemoryMap(&mapsize, 0, &key, &descsize, &descver);
    mapsize += 2 * descsize;
    bs->AllocatePool(EfiLoaderData, mapsize, (void **)&map);
    stt = bs->GetMemoryMap(&mapsize, map, &key, &descsize, &descver);
    if (EFI_ERROR(stt)) {
        say("handoff: final GetMemoryMap failed\n");
        return;
    }
    say("handoff: ExitBootServices... (");
    say_dec(gKernelSegCount);
    say(" kernel segments copied after it)\n");
    stt = bs->ExitBootServices(image_handle, key);
    if (EFI_ERROR(stt)) {
        say("handoff: ExitBootServices refused (stale key?)\n");
        return;
    }
    __asm__ volatile("cli");
    gHandoffEntry = (UINT32)entry;
    gHandoffBootArgs = boot_args_phys;
    gHandoffRsp = (UINT32)rsp;
    xnu_handoff();
    for (;;) { __asm__ volatile("hlt"); }
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    /*
     * Proof of execution, first, with nothing between the entry point and the
     * UART: no firmware call, no protocol lookup, no shared state.  Every
     * earlier version of this file discovered the console first and printed
     * afterwards, so a crash in that discovery was indistinguishable from the
     * firmware never running the image at all -- which is exactly the
     * ambiguity that cost several CI runs.  Keep this first.
     */
    uart_init();
    serial_puts("RAVYNOS-ENTRY\n");

    find_console(st);

    /* From here on the UART is driven directly, so these lines appear even
     * when the firmware installs no Serial I/O protocol. */
    say("ravynOS UEFI loader\n");
    if (gSerial) {
        say("console: COM1 115200 8N1 (EFI Serial I/O present; ConOut is the "
            "same terminal)\n");
    } else {
        say("console: COM1 115200 8N1 (no EFI Serial I/O; ConOut mirrored)\n");
    }
    say("RAVYNOS-LOADER-OK\n");

    /*
     * K2/K3 (Docs/KernelHandoff.md): try to load /EFI/KERNEL.ELF off the
     * ESP via SimpleFileSystem.  On success we never return: bootinfo is
     * written, ExitBootServices() runs and control jumps to the ELF entry.
     * On any failure we drop back to the shell chain-load (design decision
     * 4: "no kernel" -> ravyn> prompt), keeping SHELL-OK behavior intact.
     */
    {
        EFI_BOOT_SERVICES *bs = st->BootServices;
        static EFI_GUID lifo_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
        EFI_LOADED_IMAGE_PROTOCOL *li = 0;
        int tried = 0;

        if (bs &&
            !EFI_ERROR(bs->HandleProtocol(image_handle, &lifo_guid,
                                          (void **)&li)) && li) {
            UINTN k;
            gLoaderImageBase = li->ImageBase;
            gLoaderImageSize = li->ImageSize;
            say("loader: image base 0x");
            say_hex(li->ImageBase, 16);
            say(" size ");
            say_dec(li->ImageSize);
            say("\n");
            for (k = 0; k < KERNEL_PATH_COUNT; k++) {
                void *data = 0;
                UINTN size = 0, data_pages = 0;
                UINT64 entry = 0, rsp = 0;

                if (EFI_ERROR(esp_read_file(bs, li->DeviceHandle,
                                            gKernelPaths[k], &data, &size,
                                            &data_pages))) {
                    continue;
                }
                tried = 1;
                say("loader: found ");
                /* Load the optional \boot-args file once the ESP volume is
                 * known; appended to the default command line below. */
                if (gBootArgsFileLen == 0) {
                    void *args_buf = 0;
                    UINTN args_size = 0, args_pages = 0;
                    if (!EFI_ERROR(esp_read_file(bs, li->DeviceHandle,
                                                 L"boot-args", &args_buf,
                                                 &args_size, &args_pages)) &&
                        args_buf && args_size) {
                        UINTN n = args_size < sizeof(gBootArgsFile) - 1 ?
                                  args_size : sizeof(gBootArgsFile) - 1;
                        UINTN i;
                        for (i = 0; i < n; i++) {
                            char c = ((char *)args_buf)[i];
                            if (c == '\n' || c == '\r') {
                                c = ' '; /* allow newline-terminated files */
                            }
                            gBootArgsFile[i] = c;
                        }
                        while (n > 0 && gBootArgsFile[n - 1] == ' ') {
                            n--;
                        }
                        gBootArgsFile[n] = 0;
                        gBootArgsFileLen = n;
                        if (n) {
                            say("loader: ESP boot-args: ");
                            say(gBootArgsFile);
                            say("\n");
                        }
                    } else {
                        gBootArgsFileLen = 0;
                    }
                    if (args_buf && args_pages) {
                        bs->FreePages(
                            (EFI_PHYSICAL_ADDRESS)(UINTN)args_buf, args_pages);
                    }
                }
                {
                    char name[64];
                    UINTN n = 0;
                    while (gKernelPaths[k][n] && n + 1 < sizeof(name)) {
                        name[n] = (char)gKernelPaths[k][n];
                        n++;
                    }
                    name[n] = 0;
                    say(name);
                }
                say(" (");
                say_dec(size);
                say(" bytes at 0x");
                say_hex((UINT64)(UINTN)data, 16);
                say(")\n");
                {
                    UINT64 kphys_base = 0, kphys_size = 0;
                    int is_macho = (size >= 4 && *(UINT32 *)data == MH_MAGIC_64);
                    int ok = 0;
                    if (is_macho) {
                        ok = load_macho_kernel_image(bs, data, size, &entry, &rsp,
                                                     &kphys_base, &kphys_size);
                    } else {
                        ok = load_kernel_image(bs, data, size, &entry, &rsp,
                                              &kphys_base, &kphys_size);
                    }
                    if (ok) {
                        if (is_macho) {
                            /* Stage \System\Library\Extensions\*.kext (plus
                             * PlugIns) and the boot-data window so the post-EBS
                             * copy in xnu_handoff replays all of them into
                             * [kaddr, kaddr+ksize). */
                            stage_booter_kexts(bs, li->DeviceHandle);
                            if (!build_device_tree(bs, st, kphys_base, kphys_size)) {
                                say("loader: no device tree, not handing off\n");
                                free_staged_kexts(bs);
                                bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)data,
                                              data_pages);
                                break;
                            }
                            UINT32 bargs = build_boot_args(bs, st, kphys_base, kphys_size);
                            if (!bargs) {
                                say("loader: no boot_args, not handing off\n");
                                if (gBootDataStage) {
                                    bs->FreePages(
                                        (EFI_PHYSICAL_ADDRESS)(UINTN)gBootDataStage,
                                        (UINTN)(XNU_BOOT_DATA_SIZE >> EFI_PAGE_SHIFT));
                                    gBootDataStage = 0;
                                }
                                free_staged_kexts(bs);
                                bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)data,
                                              data_pages);
                                break;
                            }
                            handoff_to_xnu(bs, image_handle, entry, bargs, rsp);
                            if (gBootDataStage) {
                                bs->FreePages(
                                    (EFI_PHYSICAL_ADDRESS)(UINTN)gBootDataStage,
                                    (UINTN)(XNU_BOOT_DATA_SIZE >> EFI_PAGE_SHIFT));
                                gBootDataStage = 0;
                            }
                            free_staged_kexts(bs);
                            bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)data,
                                          data_pages);
                        } else {
                            bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)data,
                                          data_pages);
                            if (!write_bootinfo(bs)) {
                                say("loader: no bootinfo block, not handing off\n");
                                break;
                            }
                            handoff_to_kernel(bs, image_handle, entry, rsp);
                        }
                        /* only reached if ExitBootServices refused */
                        say("loader: handoff failed, falling back to shell\n");
                    } else {
                        bs->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)data,
                                      data_pages);
                        say("loader: kernel payload rejected, falling back to shell\n");
                    }
                }
                break;
            }
        }
        if (!tried) {
            say("loader: no kernel payload on this volume -- shell only\n");
        }
    }

    try_start_shell(image_handle, st);

    /* Nothing to hand off to yet; return to the firmware cleanly so the
     * firmware's own messages stay readable in the serial log. */
    say("loader: returning to firmware\n");
    return EFI_SUCCESS;
}
