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
    void *LoadAddress;
    UINT32 ImageSize;
    EFI_PHYSICAL_ADDRESS ImageBase;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

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
    u"\\KERNEL.ELF"
};
#define KERNEL_PATH_COUNT (sizeof(gKernelPaths) / sizeof(gKernelPaths[0]))

#define BOOTINFO_ADDR     0x90000ULL
#define BOOTINFO_MAGIC    0x52564E4Bu   /* 'RVNK' little-endian */
#define BOOTINFO_VERSION  1u
#define BOOTKSTACK_SIZE   0x10000ULL    /* 64 KiB */

#define SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x0964e5b2, 0x64da, 0x11d3, { 0x9a, 0x35, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0xfd } }
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

/* Where a kernel payload may be linked: above the firmware's low memory and
 * inside the identity-mapped first 4 GiB (Docs/KernelHandoff.md risk notes). */
#define KERNEL_MIN_ADDR 0x100000ULL
#define KERNEL_MAX_END  0x100000000ULL

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

/* Read the whole file at `path` (relative to volume root) into an
 * allocated pool buffer.  On EFI_SUCCESS, buf_out/size_out receive the
 * data pointer and byte count. */
static EFI_STATUS esp_read_file(EFI_BOOT_SERVICES *bs, EFI_HANDLE dev_handle,
                                const CHAR16 *path, void **out_buf,
                                UINTN *out_size)
{
    static EFI_GUID sfs_guid = SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    static EFI_GUID file_guid = EFI_FILE_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM *fs = 0;
    EFI_FILE *root = 0, *f = 0;
    EFI_STATUS stt;
    UINTN sz;
    UINT8 *buf = 0;
    void *iface = 0;

    *out_buf = 0;
    *out_size = 0;

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
    stt = root->Open(root, &f, (void *)path, 1 /*READ*/, 0);
    if (EFI_ERROR(stt)) {
        root->Close(root);
        return stt;
    }
    /* Chunked read until EOF.  EFI_FILE_PROTOCOL.Read shrinks *BufferSize to
     * the number of bytes actually transferred and returns 0 bytes at end of
     * file, which is the exit condition.  The buffer grows with the file
     * instead of being a fixed 8 MiB pool allocation: the old code silently
     * truncated anything larger and burned 8 MiB of pool for every probe. */
    (void)file_guid;
    {
        UINTN cap = 0;
        UINTN total = 0;
        for (;;) {
            if (total == cap) {
                UINTN newcap = cap ? cap * 2 : (16u << 10);
                UINT8 *grown;
                if (newcap > (8u << 20)) {
                    say("kernel: payload larger than 8 MiB, refusing\n");
                    if (buf) {
                        bs->FreePool(buf);
                    }
                    f->Close(f);
                    root->Close(root);
                    return EFI_UNSUPPORTED;
                }
                if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, newcap,
                                               (void **)&grown))) {
                    if (buf) {
                        bs->FreePool(buf);
                    }
                    f->Close(f);
                    root->Close(root);
                    return EFI_UNSUPPORTED;
                }
                if (buf) {
                    bs->CopyMem(grown, buf, total);
                    bs->FreePool(buf);
                }
                buf = grown;
                cap = newcap;
            }
            sz = cap - total;
            stt = f->Read(f, &sz, buf + total);
            if (EFI_ERROR(stt)) {
                bs->FreePool(buf);
                f->Close(f);
                root->Close(root);
                return stt;
            }
            if (sz == 0) {
                break;                       /* end of file */
            }
            total += sz;
        }
        f->Close(f);
        root->Close(root);
        if (total == 0) {
            if (buf) {
                bs->FreePool(buf);
            }
            return EFI_UNSUPPORTED;
        }
        *out_buf = buf;
        *out_size = total;
        gFs = fs;
        return EFI_SUCCESS;
    }
}

/* Parse + stage the kernel image.  On success returns entry point and
 * allocates a boot stack; rsp_out receives its top. */
static int load_kernel_image(EFI_BOOT_SERVICES *bs, void *file_data, UINTN size,
                             UINT64 *entry_out, UINT64 *rsp_out)
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
            for (k = 0; k < KERNEL_PATH_COUNT; k++) {
                void *data = 0;
                UINTN size = 0;
                UINT64 entry = 0, rsp = 0;

                if (EFI_ERROR(esp_read_file(bs, li->DeviceHandle,
                                            gKernelPaths[k], &data, &size))) {
                    continue;
                }
                tried = 1;
                say("loader: found ");
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
                say(" bytes)\n");
                if (load_kernel_image(bs, data, size, &entry, &rsp)) {
                    bs->FreePool(data);
                    if (!write_bootinfo(bs)) {
                        say("loader: no bootinfo block, not handing off\n");
                        break;
                    }
                    handoff_to_kernel(bs, image_handle, entry, rsp);
                    /* only reached if ExitBootServices refused */
                    say("loader: handoff failed, falling back to shell\n");
                } else {
                    bs->FreePool(data);
                    say("loader: kernel payload rejected, falling back to shell\n");
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
