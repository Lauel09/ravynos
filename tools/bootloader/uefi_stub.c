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
 * Build:  make            (clang -target x86_64-unknown-windows + lld-link)
 * See tools/bootloader/README.md.
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
typedef enum {
    EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData, EfiBootServicesCode,
    EfiBootServicesData, EfiRuntimeServicesCode, EfiRuntimeServicesData,
    EfiConventionalMemory, EfiUnusableMemory, EfiACPIReclaimMemory,
    EfiACPIMemoryNVS, EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace,
    EfiPalCode, EfiPersistentMemory, EfiMaxMemoryType
} EFI_MEMORY_TYPE;

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
static UINTN str_len(const char *s)
{
    UINTN n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

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

static const char *status_name(EFI_STATUS status)
{
    if (!EFI_ERROR(status)) {
        return "ok";
    }
    switch (status) {
    case EFI_UNSUPPORTED:          return "unsupported";
    case EFI_BUFFER_TOO_SMALL:     return "buffer-too-small";
    default:                       return "error";
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    (void)image_handle;

    find_console(st);

    say("ravynOS UEFI loader skeleton\n");
    if (gSerial) {
        say("console: EFI Serial I/O at 115200 8N1\n");
    } else {
        say("console: ConOut only (no serial protocol found)\n");
    }
    say("RAVYNOS-LOADER-OK\n");

    /*
     * Step C (Docs/BootLoader.md) continues here:
     *
     *   1. open the volume this image was started from
     *      (LoadedImage->DeviceHandle + SimpleFileSystem),
     *   2. read /BOOT/KERNEL and verify its checksum,
     *   3. only then print "RAVYNOS-KERNEL-OK".
     *
     * The marker must not be printed before the payload is actually in
     * memory and validated, or the verdict stops meaning anything.
     */
    say("loader: kernel loading not implemented yet (step C)\n");

    /* Nothing to hand off to yet; return to the firmware cleanly so the
     * firmware's own messages stay readable in the serial log. */
    say("loader: returning to firmware\n");
    (void)status_name;
    return EFI_SUCCESS;
}
