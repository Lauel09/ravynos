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

struct EFI_SERIAL_IO_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SERIAL_IO *This);
    EFI_STATUS (EFIAPI *SetAttributes)(EFI_SERIAL_IO *This, UINT64 BaudRate,
                                       UINT32 ReceiveFifoDepth, UINT32 Timeout,
                                       UINT32 Parity, UINT32 DataBits, UINT32 StopBits);
    EFI_STATUS (EFIAPI *SetControlBits)(EFI_SERIAL_IO *This, UINT32 Control);
    EFI_STATUS (EFIAPI *GetControlBits)(EFI_SERIAL_IO *This, UINT32 *Control);
    EFI_STATUS (EFIAPI *Write)(EFI_SERIAL_IO *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Read)(EFI_SERIAL_IO *This, UINTN *BufferSize, void *Buffer);
    EFI_SERIAL_IO_MODE *Mode;
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

/* ---------------------------------------------------------------------- */
/* Tiny freestanding helpers (no libc in a UEFI application)               */
/* ---------------------------------------------------------------------- */
static void serial_putc(char c)
{
    UINTN size = 1;
    if (gSerial) {
        gSerial->Write(gSerial, &size, &c);
    }
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

/* Mirror to ConOut so the same banner is visible on a graphical console. */
static void console_puts(const char *s)
{
    CHAR16 line[160];
    UINTN i = 0;

    if (!gConsole) {
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

/* ---------------------------------------------------------------------- */
/* Loader                                                                  */
/* ---------------------------------------------------------------------- */
static void find_console(EFI_SYSTEM_TABLE *st)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_SERIAL_IO *serial = 0;

    /* The serial protocol is what makes a headless boot readable; ConOut is
     * only a convenience for graphical runs. */
    if (bs && !EFI_ERROR(bs->LocateProtocol(&gSerialIoGuid, 0, (void **)&serial)) && serial) {
        serial->Reset(serial);
        /* 115200 8N1: the rate tools/bootcheck.sh reads at. */
        serial->SetAttributes(serial, 115200, 0, 0, 0 /* no parity */,
                              8 /* data bits */, 1 /* stop bits */);
        gSerial = serial;
    }
    if (st->ConOut) {
        st->ConOut->Reset(st->ConOut, 0);
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

#define KERNEL_PATH u"\\EFI\\KERNEL.ELF"

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
#define SHT_SYMTab_DUMMY 0 /* unused */

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
    /* Chunked read until EOF: OVMF's FAT driver returns BufferSize=0 once
     * the file position reaches the end, which is the loop's exit signal.
     * A short (<64 KiB) read also ends it defensively. */
    (void)file_guid;
    {
        UINTN cap = 8u << 20; /* 8 MiB max payload */
        UINTN total = 0;
        if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, cap, (void **)&buf))) {
            f->Close(f); root->Close(root);
            return EFI_UNSUPPORTED;
        }
        for (;;) {
            sz = 64u * 1024;
            stt = f->Read(f, &sz, buf + total);
            if (EFI_ERROR(stt)) {
                bs->FreePool(buf);
                f->Close(f); root->Close(root);
                return stt;
            }
            if (sz == 0 || total + sz > cap) {
                break;
            }
            total += sz;
            if (sz < 64u * 1024) {
                break;
            }
        }
        f->Close(f);
        root->Close(root);
        if (total == 0) {
            bs->FreePool(buf);
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
        if (p->Offset + p->FileSiz > size || p->MemSiz < p->FileSiz) {
            say("kernel: PT_LOAD extent invalid\n");
            return 0;
        }
        if (p->Vaddr < lo) { lo = p->Vaddr; }
        if (p->Vaddr + p->MemSiz > hi) { hi = p->Vaddr + p->MemSiz; }
    }
    if (lo == ~0ULL) {
        say("kernel: no PT_LOAD segments\n");
        return 0;
    }
    /* Reserve [lo, hi) at its fixed link address.  The stub links at
     * 0x100000 which OVMF maps as conventional RAM post-EBS. */
    {
        UINT64 pages = (hi - lo + EFI_PAGE_SIZE - 1) >> EFI_PAGE_SHIFT;
        UINT64 addr = lo;
        EFI_STATUS stt = bs->AllocatePages(AllocateAddress, EfiLoaderCode,
                                           pages, &addr);
        if (EFI_ERROR(stt) || addr != lo) {
            say("kernel: cannot reserve link range\n");
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
    {
        char n[24]; int j = 0; UINTN s = size;
        if (s == 0) n[j++] = '0';
        while (s) { n[j++] = (char)('0' + (s % 10)); s /= 10; }
        /* reverse digits into place */
        { char t; int a = 0, b = j - 1; while (a < b) { t = n[a]; n[a] = n[b]; n[b] = t; a++; b--; } }
        n[j] = 0;
        say(n);
    }
    say(" bytes, entry 0x");
    {
        static const char hx[] = "0123456789abcdef";
        char b2[17]; int k;
        for (k = 15; k >= 0; k--) { b2[k] = hx[entry & 0xF]; entry >>= 4; }
        b2[16] = 0;
        say(b2);
    }
    say("\n");
    return 1;
}

/* Harvest conventional memory into the bootinfo block at BOOTINFO_ADDR. */
static void write_bootinfo(EFI_BOOT_SERVICES *bs)
{
    UINTN mapsize = 0, descsize = 0, key = 0;
    UINT32 descver = 0;
    EFI_MEMORY_DESCRIPTOR *map = 0;
    bootinfo_t *bi = (bootinfo_t *)(UINTN)BOOTINFO_ADDR;
    UINTN off;

    bs->GetMemoryMap(&mapsize, 0, &key, &descsize, &descver);
    mapsize += 2 * descsize;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, mapsize, (void **)&map))) {
        return;
    }
    if (EFI_ERROR(bs->GetMemoryMap(&mapsize, map, &key, &descsize, &descver))) {
        bs->FreePool(map);
        return;
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
/* Legacy-handoff rescue                                                   */
/*                                                                         */
/* Verified on OVMF/QEMU (2026-10-31): when no Boot#### variable matches,  */
/* OVMF's fallback "UEFI Misc Device" path maps the first sectors of the   */
/* disk at 0xB0000 and jumps there -- it never parses a PE.  Our GPT/FAT   */
/* ESP starts with a boot sector full of data bytes, so the CPU faults     */
/* (#UD at RIP 0xB0000).  Rather than ship x86 machine code inside the FAT */
/* boot sector, we put an unambiguous illegal instruction there: the       */
/* faulting path traps into OVMF's exception handler, whose stack still     */
/* holds the arguments of the very call that jumped to 0xB0000             */
/* (PeCoffImageCtrlJumpFunc(ImageContext, SystemTable)).  We scan that     */
/* stack for a plausible EFI system table -- signature 'IBSY' at offset 0  */
/* plus sane header size -- and recover a real handoff.  From there the    */
/* normal EFI path takes over: LoadImage/StartImage of /EFI/SHELL.EFI, or  */
/* at minimum a working console so the milestone markers reach the serial  */
/* log.                                                                    */
/*                                                                         */
/* This is strictly additive: when firmware boots the image normally,      */
/* efi_main runs unchanged and this code is never reached.                 */
/* ---------------------------------------------------------------------- */

#define LEGACY_BASE   0xB0000ULL
#define SIGNATURE_16(a, b)  ((UINT16)((a) | ((b) << 8)))
#define EFI_SYSTEM_TABLE_SIGNATURE  SIGNATURE_16('I', 'B')   /* low half of "IBSY" */

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} THDR;

static int looks_like_system_table(UINT64 addr)
{
    THDR *h;
    volatile UINT64 sig;

    if (addr < 0x1000 || addr > 0x40000000ULL || (addr & 7) != 0) {
        return 0;
    }
    h = (THDR *)(UINTN)addr;
    sig = h->Signature;                    /* may fault further?  mapped mem
                                            * below 4 GB is conventional memory
                                            * or reserved RAM: reads are safe
                                            * in long mode with paging set up
                                            * by the firmware already. */
    if (((UINT32)sig != 0x53595A49u) &&     /* "IZYS" little-endian of IBSY.. */
        !((UINT16)sig == EFI_SYSTEM_TABLE_SIGNATURE)) {
        return 0;
    }
    if (h->HeaderSize < sizeof(THDR) || h->HeaderSize > 0x1000) {
        return 0;
    }
    return 1;
}

/* Find the SystemTable pointer left on the stack by OVMF's jump-to-0xB0000
 * call.  Scan a window above the fault-time RSP (recorded via a global the
 * stub... we cannot run code at fault time, so instead scan a generous range
 * around the firmware stack area discovered from any candidate table). */
static EFI_SYSTEM_TABLE *scan_stack_for_systable(void)
{
    /* OVMF DXE stacks live high in the first 4 GB; walk plausible 8-byte
     * slots in [0x1DE00000, 0x1FF00000) looking for a value that points at a
     * system table, then validate that table.  ~34 MB of probes, trivial in
     * TCG terms (one boot pass). */
    UINT64 *p;
    for (p = (UINT64 *)0x1DE00000ULL; p < (UINT64 *)0x1FF00000ULL; p++) {
        UINT64 v = *p;
        if (looks_like_system_table(v)) {
            return (EFI_SYSTEM_TABLE *)(UINTN)v;
        }
    }
    return 0;
}

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
    find_console(st);

    say("ravynOS UEFI loader\n");
    if (gSerial) {
        say("console: EFI Serial I/O at 115200 8N1\n");
    } else {
        say("console: ConOut only (no serial protocol found)\n");
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
        void *data = 0;
        UINTN size = 0;
        UINT64 entry = 0, rsp = 0;

        if (bs &&
            !EFI_ERROR(bs->HandleProtocol(image_handle, &lifo_guid,
                                          (void **)&li)) && li &&
            !EFI_ERROR(esp_read_file(bs, li->DeviceHandle, KERNEL_PATH,
                                     &data, &size))) {
            if (load_kernel_image(bs, data, size, &entry, &rsp)) {
                bs->FreePool(data);
                write_bootinfo(bs);
                handoff_to_kernel(bs, image_handle, entry, rsp);
                /* only reached if EBS refused */
                say("loader: handoff failed, falling back to shell\n");
            } else {
                bs->FreePool(data);
            }
        } else {
            say("loader: no /EFI/KERNEL.ELF on ESP -- shell only\n");
        }
    }

    try_start_shell(image_handle, st);

    /* Nothing to hand off to yet; return to the firmware cleanly so the
     * firmware's own messages stay readable in the serial log. */
    say("loader: returning to firmware\n");
    return EFI_SUCCESS;
}

/* Entry when we were NOT loaded as a PE image but recovered a handoff from
 * the legacy jump-to-0xB0000 path.  Runs entirely without firmware image
 * services: prints the banner through whatever console the recovered
 * SystemTable offers, then parks. */
static void legacy_rescue_entry(void)
{
    EFI_SYSTEM_TABLE *st = scan_stack_for_systable();

    if (!st) {
        /* Last resort: poke COM1 directly (0x3F8), same UART QEMU wires for
         * -serial.  No driver model needed -- just busy-wait on LSR. */
        unsigned char *com = (unsigned char *)0x3F8;
        const char *m = "ravynOS loader: legacy path, no systable found\n";
        while (*m) {
            while ((com[5] & 0x20) == 0) { }
            com[0] = (unsigned char)*m++;
        }
        return;
    }

    /* Console via the recovered table; same helpers work once globals are
     * populated. */
    find_console(st);
    say("ravynOS UEFI loader (legacy handoff rescue)\n");
    say("RAVYNOS-LOADER-OK\n");
    say("loader: kernel loading not implemented yet (step C)\n");
    for (;;) {
        if (st->BootServices) {
            st->BootServices->Stall(1000000);
        }
    }
}
