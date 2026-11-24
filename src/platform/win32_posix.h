#ifndef OPENTONY_WIN32_POSIX_H
#define OPENTONY_WIN32_POSIX_H

/* The Linux build's stand-in for <windows.h> in the standalone's host sources: the small set of Win32 calls they make that have a plain
 * POSIX meaning (strings, the clock, sleeps, the environment, file attributes and searches, INI settings, interlocked words, plain file
 * handles). A shared source includes it in place of <windows.h> under `#ifndef _WIN32`; calls whose meaning differs on Linux (the address
 * space, modules, timers, crash reports) are written out in the source instead (src/platform/ot_os.hpp).
 *
 * Paths: '\\' is taken as '/', and a path that names an existing file under another case is that file (ot_os.hpp, resolve_nocase).
 * Text: char strings are UTF-8 (the Windows build runs with the UTF-8 code page, src/hooks/kits/utf8.manifest); wchar_t strings are UTF-32. */

#if defined(_WIN32)
#error "win32_posix.h is for the Linux build only"
#endif

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int BOOL;
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uintptr_t UINT_PTR;
typedef intptr_t INT_PTR;
typedef uintptr_t ULONG_PTR;
typedef uint32_t UINT;
typedef unsigned char UCHAR, *PUCHAR;
typedef uint32_t ULONG;
typedef void *HANDLE;
typedef void *HMODULE;   /* a dlopen() handle */
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef const wchar_t *LPCWSTR;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define FILE_ATTRIBUTE_READONLY 0x01u
#define FILE_ATTRIBUTE_HIDDEN 0x02u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1u
#define FILE_SHARE_WRITE 2u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u
#define MOVEFILE_REPLACE_EXISTING 1u
#define CP_ACP 0u
#define CP_UTF8 65001u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_NO_MORE_FILES 18u
#define ERROR_ALREADY_EXISTS 183u

typedef union LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

typedef struct FILETIME {
    DWORD dwLowDateTime, dwHighDateTime;
} FILETIME;

typedef struct WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
    DWORD dwReserved0, dwReserved1;
    char cFileName[260];
    char cAlternateFileName[14];
} WIN32_FIND_DATAA;

/* ---- errors ---- */
DWORD GetLastError(void);
void SetLastError(DWORD error);

/* ---- strings ---- */
int lstrlenA(const char *text);
char *lstrcpyA(char *out, const char *text);
char *lstrcpynA(char *out, const char *text, int size);
char *lstrcatA(char *out, const char *text);
int lstrcmpA(const char *a, const char *b);
int lstrcmpiA(const char *a, const char *b);
int WideCharToMultiByte(UINT code_page, DWORD flags, const wchar_t *wide, int wide_length, char *out, int out_size, const char *default_char,
                        BOOL *used_default);
int MultiByteToWideChar(UINT code_page, DWORD flags, const char *text, int length, wchar_t *out, int out_size);

/* ---- time ---- */
BOOL QueryPerformanceCounter(LARGE_INTEGER *count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *frequency);
DWORD GetTickCount(void);
void Sleep(DWORD milliseconds);
BOOL SwitchToThread(void);
void GetLocalTime(SYSTEMTIME *time);

/* ---- modules (dlopen / dlsym) ---- */
HMODULE LoadLibraryA(const char *path);
void *GetProcAddress(HMODULE module, const char *name);
BOOL FreeLibrary(HMODULE module);

/* ---- the process ---- */
__attribute__((noreturn)) void ExitProcess(UINT code);
HANDLE GetCurrentProcess(void);
DWORD GetCurrentProcessId(void);
DWORD GetCurrentThreadId(void);
BOOL TerminateProcess(HANDLE process, UINT code);   /* the current process only */
DWORD GetCurrentDirectoryA(DWORD size, char *out);
/* whether any byte of [at, at + size) cannot be read (a probe of each page, never a fault) */
BOOL IsBadReadPtr(const void *at, UINT_PTR size);
ULONGLONG GetTickCount64(void);

/* ---- plain memory (VirtualAlloc of a fresh read/write block, and its release) ---- */
#define MEM_COMMIT 0x00001000u
#define MEM_RESERVE 0x00002000u
#define MEM_RELEASE 0x00008000u
#define PAGE_NOACCESS 0x01u
#define PAGE_READWRITE 0x04u
void *VirtualAlloc(void *at, size_t size, DWORD type, DWORD protect);
BOOL VirtualFree(void *at, size_t size, DWORD type);
DWORD GetEnvironmentVariableA(const char *name, char *out, DWORD size);
DWORD GetEnvironmentVariableW(const wchar_t *name, wchar_t *out, DWORD size);
BOOL SetEnvironmentVariableA(const char *name, const char *value);
/* only the executable's own file (module 0) */
DWORD GetModuleFileNameA(void *module, char *out, DWORD size);
DWORD GetModuleFileNameW(void *module, wchar_t *out, DWORD size);

/* ---- files ---- */
DWORD GetFileAttributesA(const char *path);
DWORD GetFileAttributesW(const wchar_t *path);
BOOL CreateDirectoryA(const char *path, void *security);
BOOL CreateDirectoryW(const wchar_t *path, void *security);
BOOL DeleteFileA(const char *path);
BOOL MoveFileExA(const char *from, const char *to, DWORD flags);
DWORD GetFullPathNameA(const char *path, DWORD size, char *out, char **file_part);
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *found);
BOOL FindNextFileA(HANDLE search, WIN32_FIND_DATAA *found);
BOOL FindClose(HANDLE search);
HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *security, DWORD disposition, DWORD attributes, HANDLE template_file);
HANDLE CreateFileW(const wchar_t *path, DWORD access, DWORD share, void *security, DWORD disposition, DWORD attributes, HANDLE template_file);
BOOL ReadFile(HANDLE file, void *buffer, DWORD size, DWORD *read, void *overlapped);
BOOL WriteFile(HANDLE file, const void *buffer, DWORD size, DWORD *written, void *overlapped);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG *distance_high, DWORD method);
BOOL SetEndOfFile(HANDLE file);
BOOL FlushFileBuffers(HANDLE file);
BOOL CloseHandle(HANDLE handle);
DWORD GetFileSize(HANDLE file, DWORD *high);

/* ---- INI settings (the Win32 profile API over a plain file: [section] and key=value lines, matched without regard to case) ---- */
DWORD GetPrivateProfileStringA(const char *section, const char *key, const char *fallback, char *out, DWORD size, const char *file);
UINT GetPrivateProfileIntA(const char *section, const char *key, int fallback, const char *file);
BOOL WritePrivateProfileStringA(const char *section, const char *key, const char *value, const char *file);

/* ---- the MSVC C runtime's file calls, with the 32-bit layouts the game's memory holds (MSVC 6 struct _stat and struct _finddata_t) ---- */
struct _stat32 {
    uint32_t st_dev;      /* 0: the drive number (C: is 2) */
    uint16_t st_ino;      /* 4 */
    uint16_t st_mode;     /* 6: _S_IFDIR 0x4000 / _S_IFREG 0x8000, read 0x100, write 0x80, exec 0x40, copied to the group and other bits */
    int16_t st_nlink;     /* 8 */
    int16_t st_uid;       /* 10 */
    int16_t st_gid;       /* 12 */
    uint16_t pad_;        /* 14 */
    uint32_t st_rdev;     /* 16 */
    int32_t st_size;      /* 20 */
    int32_t st_atime_;    /* 24 */
    int32_t st_mtime_;    /* 28 */
    int32_t st_ctime_;    /* 32 */
};
int _stat32(const char *path, struct _stat32 *status);

struct _finddata32_t {
    uint32_t attrib;      /* 0: _A_SUBDIR 0x10, _A_ARCH 0x20 */
    int32_t time_create;  /* 4 */
    int32_t time_access;  /* 8 */
    int32_t time_write;   /* 12 */
    uint32_t size;        /* 0x10 */
    char name[260];       /* 0x14 */
};
intptr_t _findfirst32(const char *pattern, struct _finddata32_t *found);
int _findnext32(intptr_t search, struct _finddata32_t *found);
int _findclose(intptr_t search);
char *_getcwd(char *out, int size);

#ifdef __cplusplus
}
#endif

/* ---- the PE file format (the retail executable is read as a file: src/hooks/kits/standalone_spike.cpp) ---- */
#define IMAGE_DOS_SIGNATURE 0x5a4du
#define IMAGE_NT_SIGNATURE 0x00004550u
#define IMAGE_FILE_MACHINE_I386 0x014cu
#define IMAGE_SIZEOF_SHORT_NAME 8
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16
#define IMAGE_SCN_CNT_CODE 0x00000020u
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040u
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080u
#define IMAGE_SCN_MEM_EXECUTE 0x20000000u
#define IMAGE_SCN_MEM_WRITE 0x80000000u

#pragma pack(push, 2)
typedef struct IMAGE_DOS_HEADER {
    WORD e_magic, e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc, e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    WORD e_res[4];
    WORD e_oemid, e_oeminfo;
    WORD e_res2[10];
    LONG e_lfanew;
} IMAGE_DOS_HEADER;
#pragma pack(pop)

typedef struct IMAGE_FILE_HEADER {
    WORD Machine;
    WORD NumberOfSections;
    DWORD TimeDateStamp;
    DWORD PointerToSymbolTable;
    DWORD NumberOfSymbols;
    WORD SizeOfOptionalHeader;
    WORD Characteristics;
} IMAGE_FILE_HEADER;

typedef struct IMAGE_DATA_DIRECTORY {
    DWORD VirtualAddress;
    DWORD Size;
} IMAGE_DATA_DIRECTORY;

typedef struct IMAGE_OPTIONAL_HEADER32 {
    WORD Magic;
    BYTE MajorLinkerVersion, MinorLinkerVersion;
    DWORD SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData, AddressOfEntryPoint, BaseOfCode, BaseOfData, ImageBase;
    DWORD SectionAlignment, FileAlignment;
    WORD MajorOperatingSystemVersion, MinorOperatingSystemVersion, MajorImageVersion, MinorImageVersion, MajorSubsystemVersion, MinorSubsystemVersion;
    DWORD Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
    WORD Subsystem, DllCharacteristics;
    DWORD SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit, LoaderFlags, NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER32;

typedef struct IMAGE_NT_HEADERS32 {
    DWORD Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER32 OptionalHeader;
} IMAGE_NT_HEADERS32;

typedef struct IMAGE_SECTION_HEADER {
    BYTE Name[IMAGE_SIZEOF_SHORT_NAME];
    union {
        DWORD PhysicalAddress;
        DWORD VirtualSize;
    } Misc;
    DWORD VirtualAddress, SizeOfRawData, PointerToRawData, PointerToRelocations, PointerToLinenumbers;
    WORD NumberOfRelocations, NumberOfLinenumbers;
    DWORD Characteristics;
} IMAGE_SECTION_HEADER;

#define IMAGE_FIRST_SECTION(headers)                                                                                          \
    ((IMAGE_SECTION_HEADER *)((uintptr_t)(headers) + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +                            \
                              ((const IMAGE_NT_HEADERS32 *)(headers))->FileHeader.SizeOfOptionalHeader))

#ifdef __cplusplus
static_assert(sizeof(IMAGE_DOS_HEADER) == 64 && sizeof(IMAGE_NT_HEADERS32) == 248 && sizeof(IMAGE_SECTION_HEADER) == 40, "the PE layouts");
static_assert(sizeof(struct _stat32) == 36 && sizeof(struct _finddata32_t) == 0x118, "the MSVC 6 runtime layouts");
#endif

/* ---- interlocked words ---- */
static inline LONG InterlockedExchange(volatile LONG *target, LONG value) { return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST); }
static inline LONG InterlockedIncrement(volatile LONG *target) { return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST); }
static inline LONG InterlockedDecrement(volatile LONG *target) { return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST); }
static inline LONG InterlockedCompareExchange(volatile LONG *target, LONG value, LONG comparand) {
    __atomic_compare_exchange_n(target, &comparand, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}

#endif /* OPENTONY_WIN32_POSIX_H */
