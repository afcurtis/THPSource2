// The Linux side of win32_posix.h.
#include "win32_posix.h"

#include "ot_os.hpp"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

thread_local DWORD t_error = 0;

DWORD error_of(int error) {
    switch (error) {
    case 0: return 0;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    default: return 0x20000000u | static_cast<DWORD>(error);   // the customer bit: an errno with no Win32 name
    }
}

void fail_errno() { t_error = error_of(errno); }

// a Windows path as the host's: '\' is '/', and a name that exists under another case is that one
std::string host_path(const char* path, bool resolve = true) {
    std::string out = path != nullptr ? path : "";
    std::replace(out.begin(), out.end(), '\\', '/');
    return resolve ? opentony::os::resolve_nocase(out) : out;
}

void put_utf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xc0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xe0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    }
}

std::string utf8_of(const wchar_t* wide, std::size_t length) {
    std::string out;
    for (std::size_t i = 0; i < length; ++i) {
        put_utf8(out, static_cast<std::uint32_t>(wide[i]));
    }
    return out;
}
std::string utf8_of(const wchar_t* wide) { return wide != nullptr ? utf8_of(wide, std::wcslen(wide)) : std::string(); }

std::wstring wide_of(const char* text, std::size_t length) {
    std::wstring out;
    for (std::size_t i = 0; i < length;) {
        const auto b = static_cast<unsigned char>(text[i]);
        std::uint32_t c = b;
        int extra = b >= 0xf0 ? 3 : b >= 0xe0 ? 2 : b >= 0xc0 ? 1 : 0;
        if (extra != 0) {
            c = b & (0x3f >> extra);
        }
        ++i;
        while (extra-- > 0 && i < length) {
            c = (c << 6) | (static_cast<unsigned char>(text[i++]) & 0x3f);
        }
        out += static_cast<wchar_t>(c);
    }
    return out;
}

DWORD copy_out(const std::string& value, char* out, DWORD size) {
    // the Win32 contract: the length copied without the terminator, or the size needed with it when the buffer is too small
    if (out == nullptr || size <= value.size()) {
        return static_cast<DWORD>(value.size() + 1);
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return static_cast<DWORD>(value.size());
}

DWORD copy_out(const std::wstring& value, wchar_t* out, DWORD size) {
    if (out == nullptr || size <= value.size()) {
        return static_cast<DWORD>(value.size() + 1);
    }
    std::wmemcpy(out, value.c_str(), value.size() + 1);
    return static_cast<DWORD>(value.size());
}

// ---- handles: a file descriptor or a directory search ----

struct Handle {
    enum class Kind { file, search } kind;
    int fd = -1;
    std::vector<WIN32_FIND_DATAA> found;
    std::size_t next = 0;
};

Handle* handle_of(HANDLE handle) {
    return handle == nullptr || handle == INVALID_HANDLE_VALUE ? nullptr : static_cast<Handle*>(handle);
}

void fill_find(WIN32_FIND_DATAA& data, const std::string& name, const struct stat& info) {
    data = WIN32_FIND_DATAA{};
    std::snprintf(data.cFileName, sizeof data.cFileName, "%s", name.c_str());
    data.dwFileAttributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    const auto size = static_cast<std::uint64_t>(info.st_size);
    data.nFileSizeLow = static_cast<DWORD>(size);
    data.nFileSizeHigh = static_cast<DWORD>(size >> 32);
    // FILETIME: 100 ns units since 1601
    const std::uint64_t time = (static_cast<std::uint64_t>(info.st_mtime) + 11644473600ull) * 10000000ull;
    data.ftLastWriteTime = {static_cast<DWORD>(time), static_cast<DWORD>(time >> 32)};
    data.ftCreationTime = data.ftLastAccessTime = data.ftLastWriteTime;
}

// ---- INI files ----

std::mutex g_ini_mutex;

std::string trim(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream stream(path, std::ios::binary);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool is_section(const std::string& line, std::string* name) {
    const std::string text = trim(line);
    if (text.size() < 2 || text.front() != '[') {
        return false;
    }
    const std::size_t close = text.find(']');
    if (close == std::string::npos) {
        return false;
    }
    if (name != nullptr) {
        *name = trim(text.substr(1, close - 1));
    }
    return true;
}

bool is_key(const std::string& line, std::string* key, std::string* value) {
    const std::string text = trim(line);
    if (text.empty() || text[0] == ';') {
        return false;
    }
    const std::size_t equals = text.find('=');
    if (equals == std::string::npos) {
        return false;
    }
    if (key != nullptr) {
        *key = trim(text.substr(0, equals));
    }
    if (value != nullptr) {
        *value = trim(text.substr(equals + 1));
    }
    return true;
}

bool same(const std::string& a, const char* b) { return ::strcasecmp(a.c_str(), b) == 0; }

}  // namespace

extern "C" {

DWORD GetLastError(void) { return t_error; }
void SetLastError(DWORD error) { t_error = error; }

int lstrlenA(const char* text) { return text != nullptr ? static_cast<int>(std::strlen(text)) : 0; }
char* lstrcpyA(char* out, const char* text) { return std::strcpy(out, text != nullptr ? text : ""); }
char* lstrcpynA(char* out, const char* text, int size) {
    if (size <= 0) {
        return out;
    }
    std::snprintf(out, static_cast<std::size_t>(size), "%s", text != nullptr ? text : "");
    return out;
}
char* lstrcatA(char* out, const char* text) { return std::strcat(out, text != nullptr ? text : ""); }
int lstrcmpA(const char* a, const char* b) { return std::strcmp(a != nullptr ? a : "", b != nullptr ? b : ""); }
int lstrcmpiA(const char* a, const char* b) { return ::strcasecmp(a != nullptr ? a : "", b != nullptr ? b : ""); }

int WideCharToMultiByte(UINT, DWORD, const wchar_t* wide, int wide_length, char* out, int out_size, const char*, BOOL* used_default) {
    if (used_default != nullptr) {
        *used_default = FALSE;
    }
    // a length of -1 includes the terminator, as the Win32 call counts it
    const std::string text = wide_length < 0 ? utf8_of(wide) + std::string(1, '\0') : utf8_of(wide, static_cast<std::size_t>(wide_length));
    if (out_size == 0) {
        return static_cast<int>(text.size());
    }
    if (static_cast<std::size_t>(out_size) < text.size()) {
        t_error = 122;   // ERROR_INSUFFICIENT_BUFFER
        return 0;
    }
    std::memcpy(out, text.data(), text.size());
    return static_cast<int>(text.size());
}

int MultiByteToWideChar(UINT, DWORD, const char* text, int length, wchar_t* out, int out_size) {
    const std::wstring wide = length < 0 ? wide_of(text, std::strlen(text) + 1) : wide_of(text, static_cast<std::size_t>(length));
    if (out_size == 0) {
        return static_cast<int>(wide.size());
    }
    if (static_cast<std::size_t>(out_size) < wide.size()) {
        t_error = 122;
        return 0;
    }
    std::wmemcpy(out, wide.data(), wide.size());
    return static_cast<int>(wide.size());
}

BOOL QueryPerformanceCounter(LARGE_INTEGER* count) {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    count->QuadPart = static_cast<LONGLONG>(now.tv_sec) * 1000000000LL + now.tv_nsec;
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency) {
    frequency->QuadPart = 1000000000LL;   // nanoseconds
    return TRUE;
}

DWORD GetTickCount(void) {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<DWORD>(static_cast<std::uint64_t>(now.tv_sec) * 1000u + static_cast<std::uint64_t>(now.tv_nsec) / 1000000u);
}

void Sleep(DWORD milliseconds) {
    if (milliseconds == 0) {
        sched_yield();
        return;
    }
    opentony::os::sleep_ms(milliseconds);
}

BOOL SwitchToThread(void) { return sched_yield() == 0; }

void GetLocalTime(SYSTEMTIME* time) {
    timeval now{};
    gettimeofday(&now, nullptr);
    tm local{};
    const time_t seconds = now.tv_sec;
    localtime_r(&seconds, &local);
    time->wYear = static_cast<WORD>(local.tm_year + 1900);
    time->wMonth = static_cast<WORD>(local.tm_mon + 1);
    time->wDayOfWeek = static_cast<WORD>(local.tm_wday);
    time->wDay = static_cast<WORD>(local.tm_mday);
    time->wHour = static_cast<WORD>(local.tm_hour);
    time->wMinute = static_cast<WORD>(local.tm_min);
    time->wSecond = static_cast<WORD>(local.tm_sec);
    time->wMilliseconds = static_cast<WORD>(now.tv_usec / 1000);
}

DWORD GetEnvironmentVariableA(const char* name, char* out, DWORD size) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        t_error = 203;   // ERROR_ENVVAR_NOT_FOUND
        return 0;
    }
    return copy_out(std::string(value), out, size);
}

DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* out, DWORD size) {
    const char* value = std::getenv(utf8_of(name).c_str());
    if (value == nullptr) {
        t_error = 203;
        return 0;
    }
    return copy_out(wide_of(value, std::strlen(value)), out, size);
}

BOOL SetEnvironmentVariableA(const char* name, const char* value) {
    return (value != nullptr ? ::setenv(name, value, 1) : ::unsetenv(name)) == 0;
}

void ExitProcess(UINT code) {
    std::fflush(nullptr);
    std::_Exit(static_cast<int>(code));
}

HANDLE GetCurrentProcess(void) { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)); }
DWORD GetCurrentProcessId(void) { return static_cast<DWORD>(::getpid()); }
DWORD GetCurrentThreadId(void) { return static_cast<DWORD>(::gettid()); }

BOOL TerminateProcess(HANDLE process, UINT code) {
    if (process != GetCurrentProcess()) {
        t_error = ERROR_ACCESS_DENIED;
        return FALSE;
    }
    std::_Exit(static_cast<int>(code));   // TerminateProcess runs no exit handlers
}

DWORD GetCurrentDirectoryA(DWORD size, char* out) {
    char directory[4096];
    if (::getcwd(directory, sizeof directory) == nullptr) {
        fail_errno();
        return 0;
    }
    return copy_out(std::string(directory), out, size);
}

BOOL IsBadReadPtr(const void* at, UINT_PTR size) {
    if (size == 0) {
        return FALSE;
    }
    if (at == nullptr) {
        return TRUE;
    }
    // process_vm_readv on this process answers EFAULT for memory that cannot be read, and never faults: one byte of each page is enough
    const std::uintptr_t page = static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE));
    const auto begin = reinterpret_cast<std::uintptr_t>(at);
    const std::uintptr_t end = begin + size;
    if (end < begin) {
        return TRUE;
    }
    for (std::uintptr_t probe = begin; probe < end; probe = (probe & ~(page - 1)) + page) {
        char byte;
        iovec local{&byte, 1};
        iovec remote{reinterpret_cast<void*>(probe), 1};
        if (::process_vm_readv(::getpid(), &local, 1, &remote, 1, 0) != 1) {
            return TRUE;
        }
    }
    return FALSE;
}

ULONGLONG GetTickCount64(void) {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<ULONGLONG>(now.tv_sec) * 1000u + static_cast<ULONGLONG>(now.tv_nsec) / 1000000u;
}

// The calls the sources make: a fresh block anywhere (reserve and commit, read/write or no access), or a commit inside a reservation of opentony::os::reserve
void* VirtualAlloc(void* at, size_t size, DWORD type, DWORD protect) {
    if (at == nullptr) {
        void* block = opentony::os::reserve(size);
        if (block != nullptr && (type & MEM_COMMIT) != 0 && protect != PAGE_NOACCESS && !opentony::os::commit(block, size)) {
            ::munmap(block, size);
            block = nullptr;
        }
        if (block == nullptr) {
            t_error = 8;   // ERROR_NOT_ENOUGH_MEMORY
        }
        return block;
    }
    if ((type & MEM_COMMIT) != 0 && protect != PAGE_NOACCESS) {
        return opentony::os::commit(at, size) ? at : nullptr;
    }
    return at;
}

BOOL VirtualFree(void* at, size_t size, DWORD type) {
    (void)type;
    return at != nullptr && size != 0 && ::munmap(at, size) == 0;   // (MEM_RELEASE with a size of 0 cannot be told apart here: the callers give the size)
}

HMODULE LoadLibraryA(const char* path) {
    HMODULE module = opentony::os::load_module(path);
    if (module == nullptr) {
        t_error = 126;   // ERROR_MOD_NOT_FOUND
    }
    return module;
}

void* GetProcAddress(HMODULE module, const char* name) {
    void* found = opentony::os::symbol(module, name);
    if (found == nullptr) {
        t_error = 127;   // ERROR_PROC_NOT_FOUND
    }
    return found;
}

BOOL FreeLibrary(HMODULE module) { return module != nullptr && ::dlclose(module) == 0; }

DWORD GetModuleFileNameA(void*, char* out, DWORD size) {
    const std::string path = opentony::os::executable_path();
    if (size == 0) {
        return 0;
    }
    const std::size_t copied = std::min<std::size_t>(path.size(), size - 1);
    std::memcpy(out, path.data(), copied);
    out[copied] = '\0';
    return static_cast<DWORD>(copied);
}

DWORD GetModuleFileNameW(void*, wchar_t* out, DWORD size) {
    const std::string path = opentony::os::executable_path();
    const std::wstring wide = wide_of(path.data(), path.size());
    if (size == 0) {
        return 0;
    }
    const std::size_t copied = std::min<std::size_t>(wide.size(), size - 1);
    std::wmemcpy(out, wide.data(), copied);
    out[copied] = L'\0';
    return static_cast<DWORD>(copied);
}

DWORD GetFileAttributesA(const char* path) {
    struct stat info{};
    if (::stat(host_path(path).c_str(), &info) != 0) {
        fail_errno();
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD attributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
    if (::access(host_path(path).c_str(), W_OK) != 0) {
        attributes |= FILE_ATTRIBUTE_READONLY;
    }
    return attributes != 0 ? attributes : FILE_ATTRIBUTE_NORMAL;
}

DWORD GetFileAttributesW(const wchar_t* path) { return GetFileAttributesA(utf8_of(path).c_str()); }

BOOL CreateDirectoryA(const char* path, void*) {
    if (::mkdir(host_path(path).c_str(), 0777) != 0) {
        fail_errno();
        return FALSE;
    }
    return TRUE;
}

BOOL CreateDirectoryW(const wchar_t* path, void* security) { return CreateDirectoryA(utf8_of(path).c_str(), security); }

BOOL DeleteFileA(const char* path) {
    if (::unlink(host_path(path).c_str()) != 0) {
        fail_errno();
        return FALSE;
    }
    return TRUE;
}

BOOL MoveFileExA(const char* from, const char* to, DWORD flags) {
    const std::string target = host_path(to);
    if ((flags & MOVEFILE_REPLACE_EXISTING) == 0 && ::access(target.c_str(), F_OK) == 0) {
        t_error = ERROR_ALREADY_EXISTS;
        return FALSE;
    }
    if (std::rename(host_path(from).c_str(), target.c_str()) != 0) {
        fail_errno();
        return FALSE;
    }
    return TRUE;
}

DWORD GetFullPathNameA(const char* path, DWORD size, char* out, char** file_part) {
    std::string full = host_path(path, false);
    if (full.empty() || full[0] != '/') {
        char directory[4096];
        if (::getcwd(directory, sizeof directory) == nullptr) {
            fail_errno();
            return 0;
        }
        full = std::string(directory) + "/" + full;
    }
    const DWORD result = copy_out(full, out, size);
    if (file_part != nullptr && out != nullptr && result < size) {
        char* slash = std::strrchr(out, '/');
        *file_part = slash != nullptr ? slash + 1 : out;
    }
    return result;
}

HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* found) {
    const std::string path = host_path(pattern, false);
    const std::size_t slash = path.rfind('/');
    const std::string directory = slash == std::string::npos ? "." : opentony::os::resolve_nocase(slash == 0 ? "/" : path.substr(0, slash));
    std::string glob = slash == std::string::npos ? path : path.substr(slash + 1);
    if (glob == "*.*") {
        glob = "*";   // Win32 matches every name with "*.*", dotted or not
    }
    DIR* dir = ::opendir(directory.c_str());
    if (dir == nullptr) {
        fail_errno();
        return INVALID_HANDLE_VALUE;
    }
    auto* search = new Handle{Handle::Kind::search};
    while (const dirent* entry = ::readdir(dir)) {
        if (::fnmatch(glob.c_str(), entry->d_name, FNM_CASEFOLD) != 0) {
            continue;
        }
        struct stat info{};
        if (::stat((directory + "/" + entry->d_name).c_str(), &info) != 0) {
            continue;
        }
        WIN32_FIND_DATAA data;
        fill_find(data, entry->d_name, info);
        search->found.push_back(data);
    }
    ::closedir(dir);
    // NTFS returns a directory's names in its case-insensitive order ("." and ".." first, as they sort): the same order here, whatever the host's file system keeps
    std::sort(search->found.begin(), search->found.end(),
              [](const WIN32_FIND_DATAA& a, const WIN32_FIND_DATAA& b) { return ::strcasecmp(a.cFileName, b.cFileName) < 0; });
    if (search->found.empty()) {
        delete search;
        t_error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    *found = search->found[0];
    search->next = 1;
    return search;
}

BOOL FindNextFileA(HANDLE handle, WIN32_FIND_DATAA* found) {
    Handle* search = handle_of(handle);
    if (search == nullptr || search->kind != Handle::Kind::search || search->next >= search->found.size()) {
        t_error = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    *found = search->found[search->next++];
    return TRUE;
}

BOOL FindClose(HANDLE handle) {
    Handle* search = handle_of(handle);
    if (search == nullptr || search->kind != Handle::Kind::search) {
        return FALSE;
    }
    delete search;
    return TRUE;
}

HANDLE CreateFileA(const char* path, DWORD access, DWORD, void*, DWORD disposition, DWORD, HANDLE) {
    int flags = O_CLOEXEC;
    flags |= (access & GENERIC_READ) != 0 && (access & GENERIC_WRITE) != 0 ? O_RDWR : (access & GENERIC_WRITE) != 0 ? O_WRONLY : O_RDONLY;
    switch (disposition) {
    case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    default: break;
    }
    // a new file keeps its own name in the directory that exists under another case
    std::string target = host_path(path);
    if (::access(target.c_str(), F_OK) != 0) {
        const std::size_t slash = target.rfind('/');
        if (slash != std::string::npos && slash != 0) {
            target = opentony::os::resolve_nocase(target.substr(0, slash)) + target.substr(slash);
        }
    }
    const int fd = ::open(target.c_str(), flags, 0666);
    if (fd < 0) {
        fail_errno();
        return INVALID_HANDLE_VALUE;
    }
    return new Handle{Handle::Kind::file, fd};
}

HANDLE CreateFileW(const wchar_t* path, DWORD access, DWORD share, void* security, DWORD disposition, DWORD attributes, HANDLE template_file) {
    return CreateFileA(utf8_of(path).c_str(), access, share, security, disposition, attributes, template_file);
}

BOOL ReadFile(HANDLE handle, void* buffer, DWORD size, DWORD* read, void*) {
    Handle* file = handle_of(handle);
    if (file == nullptr || file->kind != Handle::Kind::file) {
        return FALSE;
    }
    const ssize_t got = ::read(file->fd, buffer, size);
    if (got < 0) {
        fail_errno();
        return FALSE;
    }
    if (read != nullptr) {
        *read = static_cast<DWORD>(got);
    }
    return TRUE;
}

BOOL WriteFile(HANDLE handle, const void* buffer, DWORD size, DWORD* written, void*) {
    Handle* file = handle_of(handle);
    if (file == nullptr || file->kind != Handle::Kind::file) {
        return FALSE;
    }
    std::size_t done = 0;
    while (done < size) {
        const ssize_t put = ::write(file->fd, static_cast<const char*>(buffer) + done, size - done);
        if (put < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail_errno();
            if (written != nullptr) {
                *written = static_cast<DWORD>(done);
            }
            return FALSE;
        }
        done += static_cast<std::size_t>(put);
    }
    if (written != nullptr) {
        *written = static_cast<DWORD>(done);
    }
    return TRUE;
}

DWORD SetFilePointer(HANDLE handle, LONG distance, LONG* distance_high, DWORD method) {
    Handle* file = handle_of(handle);
    if (file == nullptr || file->kind != Handle::Kind::file) {
        return INVALID_SET_FILE_POINTER;
    }
    off_t offset = distance_high != nullptr ? static_cast<off_t>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(*distance_high)) << 32) |
                                                                 static_cast<std::uint32_t>(distance))
                                            : static_cast<off_t>(distance);
    const int whence = method == FILE_END ? SEEK_END : method == FILE_CURRENT ? SEEK_CUR : SEEK_SET;
    const off_t at = ::lseek(file->fd, offset, whence);
    if (at < 0) {
        fail_errno();
        return INVALID_SET_FILE_POINTER;
    }
    if (distance_high != nullptr) {
        *distance_high = static_cast<LONG>(static_cast<std::uint64_t>(at) >> 32);
    }
    return static_cast<DWORD>(at);
}

BOOL SetEndOfFile(HANDLE handle) {
    Handle* file = handle_of(handle);
    if (file == nullptr || file->kind != Handle::Kind::file) {
        return FALSE;
    }
    const off_t at = ::lseek(file->fd, 0, SEEK_CUR);
    if (at < 0 || ::ftruncate(file->fd, at) != 0) {
        fail_errno();
        return FALSE;
    }
    return TRUE;
}

BOOL FlushFileBuffers(HANDLE handle) {
    Handle* file = handle_of(handle);
    return file != nullptr && file->kind == Handle::Kind::file && ::fsync(file->fd) == 0;
}

BOOL CloseHandle(HANDLE handle) {
    Handle* file = handle_of(handle);
    if (file == nullptr || file->kind != Handle::Kind::file) {
        return FALSE;
    }
    ::close(file->fd);
    delete file;
    return TRUE;
}

DWORD GetFileSize(HANDLE handle, DWORD* high) {
    Handle* file = handle_of(handle);
    struct stat info{};
    if (file == nullptr || file->kind != Handle::Kind::file || ::fstat(file->fd, &info) != 0) {
        return INVALID_FILE_ATTRIBUTES;   // INVALID_FILE_SIZE, the same value
    }
    if (high != nullptr) {
        *high = static_cast<DWORD>(static_cast<std::uint64_t>(info.st_size) >> 32);
    }
    return static_cast<DWORD>(info.st_size);
}

DWORD GetPrivateProfileStringA(const char* section, const char* key, const char* fallback, char* out, DWORD size, const char* file) {
    if (out == nullptr || size == 0) {
        return 0;
    }
    std::lock_guard lock(g_ini_mutex);
    const std::vector<std::string> lines = read_lines(host_path(file));
    // a null section or key lists the names (each terminated, the list ends in a second terminator), as the Win32 call does
    std::string listing;
    bool listing_mode = section == nullptr || key == nullptr;
    std::string value;
    bool have = false;
    bool inside = false;
    for (const std::string& line : lines) {
        std::string name;
        if (is_section(line, &name)) {
            inside = section != nullptr && same(name, section);
            if (section == nullptr) {
                listing += name;
                listing += '\0';
            }
            continue;
        }
        std::string k, v;
        if (inside && is_key(line, &k, &v)) {
            if (key == nullptr) {
                listing += k;
                listing += '\0';
            } else if (!have && same(k, key)) {
                value = v;
                have = true;
            }
        }
    }
    if (listing_mode) {
        if (listing.size() + 1 > size) {
            listing.resize(size >= 2 ? size - 2 : 0);
            listing += '\0';
        }
        std::memcpy(out, listing.data(), listing.size());
        out[listing.size()] = '\0';
        return static_cast<DWORD>(listing.size());   // the names and their terminators, not the list's final one
    }
    if (!have) {
        value = fallback != nullptr ? trim(fallback) : "";
    } else if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
        value = value.substr(1, value.size() - 2);   // a quoted value loses its quotes
    }
    if (value.size() >= size) {
        value.resize(size - 1);
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return static_cast<DWORD>(value.size());
}

UINT GetPrivateProfileIntA(const char* section, const char* key, int fallback, const char* file) {
    char text[64];
    if (GetPrivateProfileStringA(section, key, "", text, sizeof text, file) == 0) {
        return static_cast<UINT>(fallback);
    }
    return static_cast<UINT>(std::strtol(text, nullptr, 0));
}

BOOL WritePrivateProfileStringA(const char* section, const char* key, const char* value, const char* file) {
    if (section == nullptr) {
        return TRUE;   // the all-null call flushes the cache Windows keeps; writes here go straight to the file
    }
    std::lock_guard lock(g_ini_mutex);
    const std::string path = host_path(file);
    std::vector<std::string> lines = read_lines(path);
    std::vector<std::string> out;
    bool inside = false;
    bool found_section = false;
    bool written = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        std::string name;
        if (is_section(line, &name)) {
            if (inside && !written && key != nullptr && value != nullptr) {   // the key was not in the section: it goes at its end
                // before any blank lines that end the section
                std::size_t at = out.size();
                while (at > 0 && trim(out[at - 1]).empty()) {
                    --at;
                }
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), std::string(key) + "=" + value);
                written = true;
            }
            inside = same(name, section);
            if (inside) {
                found_section = true;
                if (key == nullptr) {   // (section, null): the whole section goes
                    while (i + 1 < lines.size() && !is_section(lines[i + 1], nullptr)) {
                        ++i;
                    }
                    inside = false;
                    continue;
                }
            }
            out.push_back(line);
            continue;
        }
        std::string k;
        if (inside && key != nullptr && is_key(line, &k, nullptr) && same(k, key)) {
            if (!written && value != nullptr) {
                out.push_back(k + "=" + value);
                written = true;
            }
            continue;   // a later duplicate, or a deleted key, is dropped
        }
        out.push_back(line);
    }
    if (key != nullptr && value != nullptr && !written) {
        if (!found_section) {
            out.push_back(std::string("[") + section + "]");
        } else {
            std::size_t at = out.size();
            while (at > 0 && trim(out[at - 1]).empty()) {
                --at;
            }
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), std::string(key) + "=" + value);
            written = true;
        }
        if (!written) {
            out.push_back(std::string(key) + "=" + value);
        }
    }
    std::FILE* stream = std::fopen(path.c_str(), "wb");
    if (stream == nullptr) {
        fail_errno();
        return FALSE;
    }
    for (const std::string& line : out) {
        std::fputs(line.c_str(), stream);
        std::fputs("\r\n", stream);   // the file a Windows install has: CRLF lines
    }
    std::fclose(stream);
    return TRUE;
}

int _stat32(const char* path, struct _stat32* status) {
    struct stat info{};
    const std::string host = host_path(path);
    if (::stat(host.c_str(), &info) != 0) {
        fail_errno();
        return -1;
    }
    *status = {};
    const bool directory = S_ISDIR(info.st_mode);
    // the MSVC runtime: read always, write unless read-only, execute for a directory and for .exe/.com/.bat/.cmd; the owner's bits copied to the group's and others'
    unsigned mode = 0x100u;
    if (::access(host.c_str(), W_OK) == 0) {
        mode |= 0x80u;
    }
    const std::size_t dot = host.rfind('.');
    const std::string extension = dot == std::string::npos ? "" : host.substr(dot);
    if (directory || ::strcasecmp(extension.c_str(), ".exe") == 0 || ::strcasecmp(extension.c_str(), ".com") == 0 ||
        ::strcasecmp(extension.c_str(), ".bat") == 0 || ::strcasecmp(extension.c_str(), ".cmd") == 0) {
        mode |= 0x40u;
    }
    mode |= (mode & 0x1c0u) >> 3 | (mode & 0x1c0u) >> 6;
    status->st_mode = static_cast<std::uint16_t>((directory ? 0x4000u : 0x8000u) | mode);
    status->st_dev = status->st_rdev = 2;
    status->st_nlink = 1;
    status->st_size = static_cast<std::int32_t>(info.st_size);
    status->st_atime_ = static_cast<std::int32_t>(info.st_atime);
    status->st_mtime_ = static_cast<std::int32_t>(info.st_mtime);
    status->st_ctime_ = static_cast<std::int32_t>(info.st_ctime);
    return 0;
}

namespace {
void fill_finddata(struct _finddata32_t* out, const WIN32_FIND_DATAA& data) {
    *out = {};
    out->attrib = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? 0x10u : 0x20u;
    const auto seconds = [](const FILETIME& time) {
        const std::uint64_t ticks = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        return static_cast<std::int32_t>(ticks / 10000000ull - 11644473600ull);
    };
    out->time_create = seconds(data.ftCreationTime);
    out->time_access = seconds(data.ftLastAccessTime);
    out->time_write = seconds(data.ftLastWriteTime);
    out->size = data.nFileSizeLow;
    std::snprintf(out->name, sizeof out->name, "%s", data.cFileName);
}
}  // namespace

intptr_t _findfirst32(const char* pattern, struct _finddata32_t* found) {
    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return -1;
    }
    fill_finddata(found, data);
    return reinterpret_cast<intptr_t>(search);
}

int _findnext32(intptr_t search, struct _finddata32_t* found) {
    WIN32_FIND_DATAA data;
    if (!FindNextFileA(reinterpret_cast<HANDLE>(search), &data)) {
        errno = ENOENT;
        return -1;
    }
    fill_finddata(found, data);
    return 0;
}

int _findclose(intptr_t search) { return FindClose(reinterpret_cast<HANDLE>(search)) ? 0 : -1; }

char* _getcwd(char* out, int size) { return ::getcwd(out, static_cast<std::size_t>(size)); }

}  // extern "C"
