/*
 * ravynOS boot shell -- /EFI/SHELL.EFI
 *
 * A freestanding UEFI application that gives the operator a prompt on the
 * serial console before any kernel work is involved: the loader chain-loads
 * this image when there is no kernel payload (or when the kernel handoff
 * fails), and it can also be started directly from a UEFI shell.
 *
 * Consoles (Docs/Shell.md):
 *   output  written to the 16550 UART at 0x3F8 directly, so a headless
 *           `-serial file:` log always contains it.  Mirrored to ConOut only
 *           when the firmware has *no* Serial I/O protocol (a graphical
 *           console), which is also the only case where the mirror is not a
 *           duplicate of the same text.
 *   input   COM1 polled directly, plus ConIn via CheckEvent/ReadKeyStroke, so
 *           a serial terminal and a physical keyboard both work.
 *
 * Builtins: help, ver, ls [dir], cat <file>, cd <dir|..>, pwd, kernel, memmap,
 *           echo <text>, reboot, exit.
 *
 * Milestone markers (Docs/Pipeline.md §4):
 *   RAVYNOS-SHELL-OK      the interactive loop is up
 *   RAVYNOS-SHELL-EXIT    `exit` (or ESC) ended the loop
 *
 * Build: ./build.sh (clang + lld-link, or zig as a fallback)
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

typedef enum { AllocateAnyPages, AllocateMaxAddress, AllocateAddress } EFI_ALLOCATE_TYPE;

#define EFIERR(a)             (0x8000000000000000ULL | (a))
#define EFI_SUCCESS           0ULL
#define EFI_ERROR(Status)     (((Status) & 0x8000000000000000ULL) != 0)
#define EFI_BUFFER_TOO_SMALL  EFIERR(5)
#define EFI_NOT_FOUND         EFIERR(14)
#define EFIAPI                __attribute__((ms_abi))

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

typedef struct {
    UINT16 Year; UINT8 Month; UINT8 Day; UINT8 Hour; UINT8 Minute;
    UINT8 Second; UINT8 Pad1; UINT32 Nanosecond; UINT16 TimeZone;
    UINT8 Daylight; UINT8 Pad2;
} EFI_TIME;

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
typedef struct {
    UINT32 Type;
    UINT32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
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
                                       UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
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

/* --- Loaded image -------------------------------------------------------- */
typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT16 Length;
} EFI_DEVICE_PATH;

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    EFI_DEVICE_PATH *FilePath;
    void *LoadAddress;
    UINT32 ImageSize;
    EFI_PHYSICAL_ADDRESS ImageBase;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* --- Simple file system -------------------------------------------------- */
typedef struct EFI_FILE_PROTOCOL EFI_FILE;
struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE *This, EFI_FILE **OutFile, CHAR16 *Name,
                              UINT64 OpenMode, UINT64 Attributes);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE *This);
    EFI_STATUS (EFIAPI *Delete)(EFI_FILE *This);
    EFI_STATUS (EFIAPI *Read)(EFI_FILE *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Write)(EFI_FILE *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE *This, UINT64 *Position);
    EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE *This, UINT64 Position);
    EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE *This, EFI_GUID *Type,
                                 UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *SetInfo)(EFI_FILE *This, EFI_GUID *Type,
                                 UINTN BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Flush)(EFI_FILE *This);
};

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM;
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(EFI_SIMPLE_FILE_SYSTEM *This, EFI_FILE **Root);
};

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

/* BB25CF6F-F1D4-11D2-9A0C-0090273FC1FD */
static EFI_GUID gSerialIoGuid = {
    0xBB25CF6F, 0xF1D4, 0x11D2, { 0x9A, 0x0C, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0xFD }
};
/* 5B1B31A1-9562-11D2-8E3F-00A0C969723B */
static EFI_GUID gLoadedImageGuid = {
    0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }
};
/* 964E5B22-6459-11D2-8E39-00A0C969723B */
static EFI_GUID gSimpleFsGuid = {
    0x964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }
};
static EFI_SYSTEM_TABLE *gST;
static EFI_SERIAL_IO *gSerial = 0;
static EFI_SIMPLE_TEXT_OUTPUT *gOut = 0;
static EFI_SIMPLE_TEXT_INPUT *gIn = 0;
static int gMirrorConOut = 1;      /* ConOut mirror unless it *is* the serial */
static EFI_FILE *gRoot = 0;        /* volume root, NULL if unavailable */
static char gCwd[128] = "";        /* "" = volume root, else "\\DIR\\DIR" */

/* ---------------------------------------------------------------------- */
/* 16550 UART at COM1                                                      */
/*                                                                         */
/* The shell must produce output even when the firmware has no Serial I/O  */
/* protocol -- that is exactly the configuration where a headless CI log    */
/* would otherwise be empty.  Writing the UART directly needs no protocols  */
/* at all, so it is the primary channel.                                   */
/* ---------------------------------------------------------------------- */
#define COM1        0x3F8
#define LSR_RX_READY 0x01
#define LSR_THR_EMPTY 0x20

static inline void outb(UINT16 port, UINT8 val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline UINT8 inb(UINT16 port)
{
    UINT8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void com1_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);   /* DLAB on  */
    outb(COM1 + 0, 0x01);   /* divisor 1: 115200 */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8n1 */
    outb(COM1 + 2, 0xC7);   /* FIFO enable + clear */
    outb(COM1 + 4, 0x0B);
}

static void com1_putc(char c)
{
    int spin = 100000;
    while ((inb(COM1 + 5) & LSR_THR_EMPTY) == 0 && --spin)
        ;
    outb(COM1, (UINT8)c);
}

static int com1_try_getc(void)
{
    if ((inb(COM1 + 5) & LSR_RX_READY) == 0) {
        return -1;
    }
    return (int)(UINT8)inb(COM1);
}

/* ---------------------------------------------------------------------- */
/* Output                                                                  */
/* ---------------------------------------------------------------------- */
static void con_out_string(const char *s)
{
    CHAR16 line[160];
    UINTN i = 0;
    const char *p = s;

    for (p = s; *p; p++) {
        com1_putc(*p == '\n' ? '\r' : *p);
        if (*p == '\n') {
            com1_putc('\n');
        }
    }
    if (!gMirrorConOut || !gOut) {
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

static void out_str(const char *s) { con_out_string(s); }

static void out_dec(UINT64 v)
{
    char buf[24];
    int n = 0, i;
    char tmp[24];

    if (v == 0) {
        out_str("0");
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
    out_str(buf);
}

static void out_hex(UINT64 v, int width)
{
    static const char digits[] = "0123456789abcdef";
    char buf[17];
    int i;

    for (i = width - 1; i >= 0; i--) {
        buf[i] = digits[v & 0xF];
        v >>= 4;
    }
    buf[width] = 0;
    out_str(buf);
}

/* ---------------------------------------------------------------------- */
/* String helpers                                                          */
/* ---------------------------------------------------------------------- */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a++ != *b++)
            return 0;
    }
    return *a == *b;
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static UINTN str_len(const char *s)
{
    UINTN n = 0;
    while (s[n])
        n++;
    return n;
}

static void str_copy(char *dst, const char *src, UINTN max)
{
    UINTN i = 0;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void ascii_to_char16(CHAR16 *dst, const char *src, UINTN max)
{
    UINTN i = 0;
    while (src[i] && i + 1 < max) {
        dst[i] = (CHAR16)(UINT8)src[i];
        i++;
    }
    dst[i] = 0;
}

static int char16_to_ascii(char *dst, const CHAR16 *src, UINTN max)
{
    UINTN i = 0;
    while (src[i] && i + 1 < max) {
        if (src[i] > 0x7E)
            dst[i] = '?';
        else
            dst[i] = (char)src[i];
        i++;
    }
    dst[i] = 0;
    return (int)i;
}

/* ---------------------------------------------------------------------- */
/* Filesystem                                                              */
/* ---------------------------------------------------------------------- */
/* Join the current directory with `name` into `out` (DOS-style, backslashes).
 * `..` pops one component; a leading \ or / makes the name absolute. */
static void path_join(char *out, UINTN outsz, const char *name)
{
    UINTN len;

    if (name[0] == '\\' || name[0] == '/') {
        str_copy(out, name + 1, outsz);
    } else if (str_eq(name, "..")) {
        str_copy(out, gCwd, outsz);
        len = str_len(out);
        while (len > 0 && out[len - 1] != '\\')
            len--;
        if (len > 0)
            len--;
        out[len] = 0;
    } else if (gCwd[0]) {
        len = str_len(gCwd);
        str_copy(out, gCwd, outsz);
        if (len + 1 < outsz) {
            out[len] = '\\';
            str_copy(out + len + 1, name, outsz - len - 1);
        }
    } else {
        str_copy(out, name, outsz);
    }
    /* normalise: '/' -> '\\' */
    for (len = 0; out[len]; len++) {
        if (out[len] == '/')
            out[len] = '\\';
    }
}

static EFI_FILE *open_path(const char *path)
{
    CHAR16 wpath[260];
    EFI_FILE *f = 0;

    if (!gRoot)
        return 0;
    ascii_to_char16(wpath, path, 260);
    if (EFI_ERROR(gRoot->Open(gRoot, &f, wpath[0] ? wpath : L"\\", 1 /*READ*/, 0))) {
        return 0;
    }
    return f;
}

static void cmd_ls(const char *arg)
{
    char path[128];
    EFI_FILE *dir;
    UINT8 *info;
    UINTN size = 1024;
    UINTN entries = 0;
    UINTN total_bytes = 0;

    if (!gRoot) {
        out_str("ls: no file system on the volume this image was started from\n");
        return;
    }
    if (*arg) {
        path_join(path, sizeof(path), arg);
    } else {
        str_copy(path, gCwd, sizeof(path));
    }

    dir = open_path(path);
    if (!dir) {
        out_str("ls: cannot open ");
        out_str(path[0] ? path : "\\");
        out_str("\n");
        return;
    }
    if (EFI_ERROR(gST->BootServices->AllocatePool(EfiLoaderData, size,
                                                 (void **)&info))) {
        out_str("ls: out of memory\n");
        dir->Close(dir);
        return;
    }

    out_str("listing ");
    out_str(path[0] ? path : "\\");
    out_str("\n");
    for (;;) {
        UINTN bufsize = size;
        EFI_FILE_INFO *fi;
        char name[200];

        if (EFI_ERROR(dir->Read(dir, &bufsize, info))) {
            break;
        }
        if (bufsize == 0) {
            break;                       /* end of directory */
        }
        fi = (EFI_FILE_INFO *)info;
        char16_to_ascii(name, fi->FileName, sizeof(name));
        if (str_eq(name, ".") || str_eq(name, ".."))
            continue;
        entries++;
        out_str("  ");
        if (fi->Attribute & 0x10) {      /* EFI_FILE_DIRECTORY */
            out_str("<DIR>  ");
        } else {
            out_dec(fi->FileSize);
            total_bytes += (UINTN)fi->FileSize;
            out_str("  ");
        }
        out_str(name);
        out_str("\n");
    }
    out_str("  ");
    out_dec(entries);
    out_str(" entries, ");
    out_dec(total_bytes);
    out_str(" bytes of files\n");

    gST->BootServices->FreePool(info);
    dir->Close(dir);
}

static void cmd_cat(const char *arg)
{
    char path[128];
    EFI_FILE *f;
    UINT8 buf[512];
    UINTN scanned = 0;

    if (!*arg) {
        out_str("usage: cat <file>\n");
        return;
    }
    path_join(path, sizeof(path), arg);
    f = open_path(path);
    if (!f) {
        out_str("cat: cannot open ");
        out_str(path);
        out_str("\n");
        return;
    }
    for (;;) {
        UINTN n = sizeof(buf);
        if (EFI_ERROR(f->Read(f, &n, buf)) || n == 0)
            break;
        for (UINTN i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == 0)
                break;
            if (scanned++ > 4096) {      /* keep the log bounded */
                out_str("\ncat: truncated at 4096 bytes\n");
                f->Close(f);
                return;
            }
            if (c == '\r')
                continue;
            {
                char one[2];
                one[0] = c;
                one[1] = 0;
                out_str(one);
            }
        }
        if (n < sizeof(buf))
            break;
    }
    if (!str_eq(path, "") && scanned && 0)
        ;                                /* no-op, keeps -Wunused quiet */
    f->Close(f);
    out_str("\n");
}

static void cmd_cd(const char *arg)
{
    char path[128];
    EFI_FILE *dir;

    if (!*arg) {
        out_str(gCwd[0] ? gCwd : "\\");
        out_str("\n");
        return;
    }
    path_join(path, sizeof(path), arg);
    if (!path[0]) {                      /* cd \ */
        gCwd[0] = 0;
        return;
    }
    dir = open_path(path);
    if (!dir) {
        out_str("cd: no such directory: ");
        out_str(path);
        out_str("\n");
        return;
    }
    dir->Close(dir);
    str_copy(gCwd, path, sizeof(gCwd));
}

/* Print the kernel payload's ELF header -- proof the loader's payload is on
 * the volume, without booting it. */
static void cmd_kernel(void)
{
    EFI_FILE *f = open_path("EFI\\KERNEL.ELF");
    UINT8 hdr[64];
    UINTN n = sizeof(hdr);

    if (!f) {
        out_str("kernel: no /EFI/KERNEL.ELF on this volume\n");
        return;
    }
    if (EFI_ERROR(f->Read(f, &n, hdr)) || n < 64) {
        out_str("kernel: cannot read the ELF header\n");
        f->Close(f);
        return;
    }
    f->Close(f);
    if (!(hdr[0] == 0x7F && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F')) {
        out_str("kernel: /EFI/KERNEL.ELF is not an ELF image\n");
        return;
    }
    {
        UINT64 entry = *(UINT64 *)&hdr[24];
        out_str("kernel: /EFI/KERNEL.ELF is ELF64, entry 0x");
        out_hex(entry, 16);
        out_str("\n");
    }
}

static void cmd_memmap(void)
{
    EFI_BOOT_SERVICES *bs = gST->BootServices;
    UINTN size = 0, descsize = 0, key = 0, off, usable = 0, regions = 0;
    UINT32 ver = 0;
    EFI_MEMORY_DESCRIPTOR *map = 0;

    bs->GetMemoryMap(&size, 0, &key, &descsize, &ver);
    size += 2 * descsize;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, size, (void **)&map))) {
        out_str("memmap: out of memory\n");
        return;
    }
    if (EFI_ERROR(bs->GetMemoryMap(&size, map, &key, &descsize, &ver))) {
        out_str("memmap: GetMemoryMap failed\n");
        bs->FreePool(map);
        return;
    }
    out_str("memory map (conventional regions):\n");
    for (off = 0; off < size; off += descsize) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + off);
        if (d->Type != EfiConventionalMemory)
            continue;
        regions++;
        usable += (UINTN)(d->NumberOfPages * 4096);
        out_str("  0x");
        out_hex(d->PhysicalStart, 12);
        out_str("  ");
        out_dec(d->NumberOfPages * 4096 / (1024 * 1024));
        out_str(" MiB\n");
    }
    out_str("  ");
    out_dec(usable / (1024 * 1024));
    out_str(" MiB usable in ");
    out_dec(regions);
    out_str(" region(s)\n");
    bs->FreePool(map);
}

/* ---------------------------------------------------------------------- */
/* Input                                                                   */
/* ---------------------------------------------------------------------- */
/* Poll COM1 first (a serial terminal always works), then ConIn so a real
 * keyboard still drives the shell.  Returns -1 when no key is pending. */
static int read_key(void)
{
    int c = com1_try_getc();
    EFI_INPUT_KEY key;

    if (c >= 0) {
        return c;
    }
    if (!gIn || !gST->BootServices->CheckEvent(gIn->WaitForKey))
        return -1;                       /* CheckEvent: 0 == signalled */
    if (EFI_ERROR(gIn->ReadKeyStroke(gIn, &key)))
        return -1;
    if (key.ScanCode == 0x17)            /* ESC */
        return 0x1B;
    if (key.UnicodeChar == 0)
        return -1;                       /* an unconverted scan code */
    return (int)key.UnicodeChar;
}

/* Wait up to `spin` iterations for a key; -1 on timeout. */
static int wait_key(UINT32 spin)
{
    while (spin--) {
        int c = read_key();
        if (c >= 0)
            return c;
        gST->BootServices->Stall(1000);
    }
    return -1;
}

#define LINE_MAX 128

static int read_line(char *line)
{
    UINTN len = 0;

    for (;;) {
        int c = wait_key(100000);
        char one[2];

        if (c < 0)
            continue;
        if (c == 0x1B) {                 /* ESC leaves the shell */
            line[0] = 0;
            return -1;
        }
        if (c == '\r' || c == '\n') {
            out_str("\n");
            line[len] = 0;
            return (int)len;
        }
        if (c == 0x7F || c == 0x08) {
            if (len > 0) {
                len--;
                out_str("\b \b");
            }
            continue;
        }
        if (c == 0x03) {                 /* ^C: drop the line */
            len = 0;
            out_str("^C\n");
            line[0] = 0;
            return 0;
        }
        if (c >= 0x20 && c < 0x7F && len + 1 < LINE_MAX) {
            line[len++] = (char)c;
            one[0] = (char)c;
            one[1] = 0;
            out_str(one);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Built-ins                                                               */
/* ---------------------------------------------------------------------- */
static void cmd_help(void)
{
    out_str("ravynOS shell builtins:\n"
            "  help            this list\n"
            "  ver             shell and firmware identity\n"
            "  ls [dir]        list a directory (default: current)\n"
            "  cat <file>      print a text file (max 4 KiB)\n"
            "  cd <dir|..>     change directory; cd \\ for the volume root\n"
            "  pwd             print the current directory\n"
            "  kernel          show the /EFI/KERNEL.ELF header, if present\n"
            "  memmap          firmware memory map (conventional regions)\n"
            "  echo <text>     print text\n"
            "  reboot          reset the machine (UEFI runtime call)\n"
            "  exit            leave the shell (also ESC)\n");
}

static void cmd_ver(void)
{
    out_str("ravynOS boot shell 0.2 (uefi, freestanding)\n");
    out_str("firmware table revision 0x");
    out_hex(gST->Hdr.Revision, 8);
    out_str("\nconsole: COM1 115200 8n1");
    if (gSerial) {
        out_str(" + EFI Serial I/O");
    }
    if (gMirrorConOut) {
        out_str(" + ConOut");
    }
    out_str("\n");
    out_str("volume: ");
    if (gRoot) {
        out_str("file system mounted, cwd ");
        out_str(gCwd[0] ? gCwd : "\\");
    } else {
        out_str("not mounted");
    }
    out_str("\n");
}

/* UEFI runtime ResetSystem, reached without a full RuntimeServices table
 * declaration: only the one entry we need is described. */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *GetTime;
    void *SetTime;
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertVirtualAddressMap;
    void *GetVariable;
    void *GetNextVariableName;
    void *SetVariable;
    void *GetNextHighMonotonicCount;
    void (EFIAPI *ResetSystem)(UINT32 ResetType, EFI_STATUS ResetStatus,
                               UINTN DataSize, void *ResetData);
} EFI_RUNTIME_SERVICES;

static void cmd_reboot(void)
{
    EFI_RUNTIME_SERVICES *rt = (EFI_RUNTIME_SERVICES *)gST->RuntimeServices;

    out_str("rebooting\n");
    if (!rt || !rt->ResetSystem) {
        out_str("reboot: no runtime ResetSystem; halting instead\n");
        return;
    }
    rt->ResetSystem(0 /* EfiResetCold */, EFI_SUCCESS, 0, 0);
}

static int execute(char *line)
{
    char *p = (char *)skip_spaces(line);
    char word[16];
    UINTN n = 0;
    const char *arg;

    if (!*p)
        return 0;
    while (p[n] && p[n] != ' ' && n + 1 < sizeof(word)) {
        word[n] = p[n];
        n++;
    }
    word[n] = 0;
    arg = skip_spaces(p + str_len(word));

    /* Dispatch on the first word. */
    {
        if (str_eq(word, "help") || str_eq(word, "?")) {
            cmd_help();
        } else if (str_eq(word, "ver")) {
            cmd_ver();
        } else if (str_eq(word, "ls") || str_eq(word, "dir")) {
            cmd_ls(arg);
        } else if (str_eq(word, "cat") || str_eq(word, "type")) {
            cmd_cat(arg);
        } else if (str_eq(word, "cd")) {
            cmd_cd(arg);
        } else if (str_eq(word, "pwd")) {
            out_str(gCwd[0] ? gCwd : "\\");
            out_str("\n");
        } else if (str_eq(word, "kernel")) {
            cmd_kernel();
        } else if (str_eq(word, "memmap")) {
            cmd_memmap();
        } else if (str_eq(word, "echo")) {
            out_str(arg);
            out_str("\n");
        } else if (str_eq(word, "reboot")) {
            cmd_reboot();
        } else if (str_eq(word, "exit")) {
            return 1;
        } else {
            out_str("shell: unknown command: ");
            out_str(word);
            out_str("  (try help)\n");
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Entry point                                                             */
/* ---------------------------------------------------------------------- */
/* `EfiMain` is lld-link's default entry for a PE image and the only name
 * zig's COFF linker accepts; both build paths use it (see build.sh). */
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    static EFI_GUID serial_guid;
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    EFI_SIMPLE_FILE_SYSTEM *fs = 0;
    char line[LINE_MAX];

    serial_guid = gSerialIoGuid;
    gST = st;
    gOut = st->ConOut;
    gIn = st->ConIn;
    com1_init();

    if (st->BootServices &&
        !EFI_ERROR(st->BootServices->LocateProtocol(&serial_guid, 0,
                                                    (void **)&gSerial))) {
        gSerial->Reset(gSerial);
        gSerial->SetAttributes(gSerial, 115200, 0, 0, 0, 8, 1);
        /* The firmware console is already the serial port; mirroring to
         * ConOut would print every line twice. */
        gMirrorConOut = 0;
    }

    if (st->BootServices &&
        !EFI_ERROR(st->BootServices->HandleProtocol(image_handle,
                                                    &gLoadedImageGuid,
                                                    (void **)&li)) && li) {
        if (!EFI_ERROR(st->BootServices->HandleProtocol(li->DeviceHandle,
                                                        &gSimpleFsGuid,
                                                        (void **)&fs)) && fs) {
            if (EFI_ERROR(fs->OpenVolume(fs, &gRoot)))
                gRoot = 0;
        }
    }

    out_str("ravynOS boot shell -- type 'help' for commands\n");
    out_str("RAVYNOS-SHELL-OK\n");

    for (;;) {
        out_str(gCwd[0] ? gCwd : "\\");
        out_str("ravyn> ");
        if (read_line(line) < 0) {
            out_str("RAVYNOS-SHELL-EXIT\n");
            break;
        }
        if (execute(line)) {
            out_str("RAVYNOS-SHELL-EXIT\n");
            break;
        }
    }

    out_str("shell: returning to firmware\n");
    return EFI_SUCCESS;
}
