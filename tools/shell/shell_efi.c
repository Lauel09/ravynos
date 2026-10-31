/*
 * ravynOS boot shell -- /EFI/SHELL.EFI
 *
 * The smallest thing that satisfies "boot up and land in a shell": a
 * freestanding UEFI application that prints a prompt on the serial console
 * (and ConOut), reads lines from ConIn, runs built-ins, and returns to the
 * firmware on exit.  No kernel yet; the command set is deliberately tiny so
 * every line exercises firmware protocols proven by the loader step
 * (SimpleTextIn/WaitForKey, Serial I/O).
 *
 * Builtins: help, ver, echo <text>, exit.
 *
 * Milestone markers (Docs/Pipeline.md contract):
 *   RAVYNOS-SHELL-OK        printed once when the interactive loop starts
 *   RAVYNOS-SHELL-EXIT      printed when `exit` ends the loop
 *
 * Build: make (clang -target x86_64-unknown-windows + lld-link); same flags
 * as tools/bootloader -- see that Makefile's comments for why each matters.
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

#define EFIERR(a)                 (0x8000000000000000ULL | (a))
#define EFI_SUCCESS               0ULL
#define EFI_ERROR(Status)         (((Status) & 0x8000000000000000ULL) != 0)
#define EFIAPI                    __attribute__((ms_abi))

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

/* --- SimpleTextOut ------------------------------------------------------ */
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_OUTPUT *This, UINT8 Extended);
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT *This, CHAR16 *String);
    EFI_STATUS (EFIAPI *TestString)(EFI_SIMPLE_TEXT_OUTPUT *This, CHAR16 *String);
    EFI_STATUS (EFIAPI *QueryMode)(EFI_SIMPLE_TEXT_OUTPUT *This, UINTN Mode,
                                   UINTN *Cols, UINTN *Rows);
    EFI_STATUS (EFIAPI *SetMode)(EFI_SIMPLE_TEXT_OUTPUT *This, UINTN Mode);
    EFI_STATUS (EFIAPI *SetAttribute)(EFI_SIMPLE_TEXT_OUTPUT *This, UINTN Attr);
    EFI_STATUS (EFIAPI *ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT *This);
    EFI_STATUS (EFIAPI *SetCursorPosition)(EFI_SIMPLE_TEXT_OUTPUT *This,
                                           UINTN C, UINTN R);
    EFI_STATUS (EFIAPI *EnableCursor)(EFI_SIMPLE_TEXT_OUTPUT *This, UINT8 Visible);
    void *Mode;
};

/* --- SimpleTextIn -------------------------------------------------------- */
typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT;
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_INPUT *This, UINT8 Extended);
    EFI_STATUS (EFIAPI *ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT *This,
                                       EFI_INPUT_KEY *Key);
    EFI_EVENT WaitForKey;             /* firmware-signalled event */
};

/* --- Serial I/O ---------------------------------------------------------- */
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
                                       UINT32 Fifo, UINT32 Timeout,
                                       UINT32 Parity, UINT32 DataBits,
                                       UINT32 StopBits);
    EFI_STATUS (EFIAPI *SetControlBits)(EFI_SERIAL_IO *This, UINT32 Control);
    EFI_STATUS (EFIAPI *GetControlBits)(EFI_SERIAL_IO *This, UINT32 *Control);
    EFI_STATUS (EFIAPI *Write)(EFI_SERIAL_IO *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Read)(EFI_SERIAL_IO *This, UINTN *BufferSize, void *Buffer);
    EFI_SERIAL_IO_MODE *Mode;
};

/* --- Boot services (named fields exactly where we call; others opaque) --- */
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
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;

    EFI_STATUS (EFIAPI *RaiseTPL)(UINTN NewTPL);
    void (EFIAPI *RestoreTPL)(UINTN OldTPL);

    EFI_STATUS (EFIAPI *AllocatePages)(EFI_ALLOCATE_TYPE Type,
                                       EFI_MEMORY_TYPE MemoryType,
                                       UINTN Pages, UINT64 *Memory);
    EFI_STATUS (EFIAPI *FreePages)(UINT64 Memory, UINTN Pages);
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *MemoryMapSize,
                                      EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                      UINTN *MapKey, UINTN *DescriptorSize,
                                      UINT32 *DescriptorVersion);
    EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size,
                                      void **Buffer);
    EFI_STATUS (EFIAPI *FreePool)(void *Buffer);

    EFI_STATUS (EFIAPI *CreateEvent)(UINT32 Type, UINTN NotifyTpl,
                                     void *NotifyFunction, void *NotifyContext,
                                     EFI_EVENT *Event);
    EFI_STATUS (EFIAPI *SetTimer)(EFI_EVENT Event, UINT64 Type, UINT64 TriggerTime);
    EFI_STATUS (EFIAPI *WaitForEvent)(UINTN NumberOfEvents, EFI_EVENT *Event,
                                      UINTN *Index);
    EFI_STATUS (EFIAPI *SignalEvent)(EFI_EVENT Event);
    EFI_STATUS (EFIAPI *CloseEvent)(EFI_EVENT Event);
    EFI_STATUS (EFIAPI *CheckEvent)(EFI_EVENT Event);

    EFI_STATUS (EFIAPI *InstallProtocolInterface)(EFI_HANDLE *Handle,
                                                  EFI_GUID *Protocol,
                                                  UINTN InterfaceType,
                                                  void *Interface);
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol,
                                       void **Interface);
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;

    EFI_STATUS (EFIAPI *LoadImage)(UINT8 BootPolicy, EFI_HANDLE ParentImageHandle,
                                   void *DevicePath, void *SourceBuffer,
                                   UINTN SourceSize, EFI_HANDLE *ImageHandle);
    EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE ImageHandle, UINTN *ExitDataSize,
                                    CHAR16 **ExitData);
    void *Exit;
    void *UnloadImage;
    void *ExitBootServices;

    void *GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
    void *SetWatchdogTimer;

    void *ConnectController;
    void *DisconnectController;

    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;

    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *Protocol, void *Registration,
                                        void **Interface);
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
};

/* --- System table --------------------------------------------------------- */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    UINT32 Pad0;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT *ConIn;
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

static EFI_SYSTEM_TABLE *gST;
static EFI_SERIAL_IO *gSerial = 0;
static EFI_SIMPLE_TEXT_OUTPUT *gOut = 0;
static EFI_SIMPLE_TEXT_INPUT *gIn = 0;

/* ---------------------------------------------------------------------- */
/* Output                                                                  */
/* ---------------------------------------------------------------------- */
static void serial_putc(char c)
{
    UINTN size = 1;
    if (gSerial) {
        gSerial->Write(gSerial, &size, &c);
    }
}

static void out_str(const char *s)
{
    CHAR16 line[160];
    UINTN i = 0;
    const char *p = s;

    while (*p) {
        serial_putc(*p == '\n' ? '\r' : *p);
        p++;
    }
    if (!gOut) {
        return;
    }
    p = s;
    while (*p && i < 158) {
        if (*p == '\n') {
            line[i++] = '\r';
        }
        line[i++] = (CHAR16)(UINT8)*p++;
    }
    line[i++] = '\r';
    line[i++] = '\n';
    line[i] = 0;
    gOut->OutputString(gOut, line);
}

/* ---------------------------------------------------------------------- */
/* String helpers                                                          */
/* ---------------------------------------------------------------------- */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a++ != *b++) {
            return 0;
        }
    }
    return *a == *b;
}

static int starts_with(const char *s, const char *pre)
{
    while (*pre) {
        if (*s++ != *pre++) {
            return 0;
        }
    }
    return 1;
}

static void u64_to_dec(UINT64 v, char *buf)
{
    char tmp[24];
    int n = 0, i;

    if (v == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    while (v) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (i = 0; i < n; i++) {
        buf[i] = tmp[n - 1 - i];
    }
    buf[n] = 0;
}

/* ---------------------------------------------------------------------- */
/* Line editing over ConIn                                                 */
/* ---------------------------------------------------------------------- */
#define LINE_MAX 128
#define SCAN_ESC 0x17

static int read_line(char *line)
{
    UINTN len = 0;

    for (;;) {
        EFI_INPUT_KEY key;
        EFI_STATUS status;
        UINTN idx = 0;

        status = gST->BootServices->WaitForEvent(1, &gIn->WaitForKey, &idx);
        if (EFI_ERROR(status)) {
            gST->BootServices->Stall(10000);
            continue;
        }
        status = gIn->ReadKeyStroke(gIn, &key);
        if (EFI_ERROR(status)) {
            continue;                   /* spurious wake */
        }
        if (key.ScanCode == SCAN_ESC) {
            line[0] = 0;
            return -1;
        }
        if (key.UnicodeChar == L'\r') {
            out_str("\n");
            line[len] = 0;
            return (int)len;
        }
        if (key.UnicodeChar == L'\b' || key.UnicodeChar == 0x7F) {
            if (len > 0) {
                len--;
                out_str("\b \b");
            }
            continue;
        }
        if (key.UnicodeChar >= L' ' && key.UnicodeChar < 0x7F &&
            len + 1 < LINE_MAX) {
            line[len++] = (char)key.UnicodeChar;
            {
                char echo[2];
                echo[0] = (char)key.UnicodeChar;
                echo[1] = 0;
                out_str(echo);
            }
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Built-ins                                                               */
/* ---------------------------------------------------------------------- */
static void cmd_help(void)
{
    out_str("builtins: help ver echo <text> exit\n");
}

static void cmd_ver(void)
{
    char b[24];

    out_str("ravynOS shell v0.1 (uefi)\n");
    out_str("system table revision ");
    u64_to_dec(gST->Hdr.Revision, b);
    out_str(b);
    out_str("\n");
}

static int execute(char *line)
{
    while (*line == ' ') {
        line++;
    }
    if (!*line) {
        return 0;
    }
    if (str_eq(line, "help")) {
        cmd_help();
    } else if (str_eq(line, "ver")) {
        cmd_ver();
    } else if (starts_with(line, "echo ")) {
        out_str(line + 5);
        out_str("\n");
    } else if (str_eq(line, "exit")) {
        out_str("RAVYNOS-SHELL-EXIT\n");
        return 1;
    } else {
        out_str("shell: unknown command: ");
        out_str(line);
        out_str("  (try help)\n");
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Entry point                                                             */
/* ---------------------------------------------------------------------- */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    EFI_STATUS status;
    char line[LINE_MAX];

    (void)image_handle;
    gST = st;
    gOut = st->ConOut;
    gIn = st->ConIn;

    /* Serial console first: headless CI reads COM1, never the screen. */
    status = st->BootServices->LocateProtocol(&gSerialIoGuid, 0,
                                              (void **)&gSerial);
    if (EFI_ERROR(status)) {
        gSerial = 0;
    }

    out_str("ravynOS boot shell -- type 'help'\n");
    out_str("RAVYNOS-SHELL-OK\n");

    if (!gIn) {
        out_str("shell: no console input; idling\n");
        for (;;) {
            st->BootServices->Stall(1000000);
        }
    }

    for (;;) {
        out_str("ravyn> ");
        if (read_line(line) < 0) {
            out_str("RAVYNOS-SHELL-EXIT\n");   /* ESC also leaves the shell */
            break;
        }
        if (execute(line)) {
            break;
        }
    }

    out_str("shell: done, returning to firmware\n");
    return EFI_SUCCESS;
}
