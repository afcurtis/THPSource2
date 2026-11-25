// The Linux side of ot_os.hpp.
#include "ot_os.hpp"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opentony::os {
namespace {

thread_local std::string t_last_error;

void set_errno_error(const char* what) { t_last_error = std::string(what) + ": " + std::strerror(errno); }

std::uintptr_t page_size() {
    static const auto size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    return size;
}

struct Mapping {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    bool read = false;
    bool write = false;
};

// One line of /proc/self/maps: "begin-end perms offset dev inode path"
bool parse_line(std::string_view line, Mapping& out) {
    char* rest = nullptr;
    const std::string text(line.substr(0, std::min<std::size_t>(line.size(), 64)));
    out.begin = static_cast<std::uintptr_t>(std::strtoull(text.c_str(), &rest, 16));
    if (rest == nullptr || *rest != '-') {
        return false;
    }
    out.end = static_cast<std::uintptr_t>(std::strtoull(rest + 1, &rest, 16));
    if (rest == nullptr || *rest != ' ' || rest[1] == '\0' || rest[2] == '\0') {
        return false;
    }
    out.read = rest[1] == 'r';
    out.write = rest[2] == 'w';
    return true;
}

// The whole of /proc/self/maps (read(2) in one buffer: the file is produced as it is read, so a stream with small reads could see it change)
std::vector<Mapping> read_maps() {
    std::vector<Mapping> maps;
    const int fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return maps;
    }
    std::string text;
    char buffer[65536];
    for (;;) {
        const ssize_t got = ::read(fd, buffer, sizeof buffer);
        if (got <= 0) {
            break;
        }
        text.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(fd);
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        Mapping mapping;
        if (parse_line(std::string_view(text).substr(at, end - at), mapping)) {
            // adjacent mappings with the same access read as one span (an arena committed piece by piece is many VMAs or one)
            if (!maps.empty() && maps.back().end == mapping.begin && maps.back().read == mapping.read && maps.back().write == mapping.write) {
                maps.back().end = mapping.end;
            } else {
                maps.push_back(mapping);
            }
        }
        at = end + 1;
    }
    return maps;
}

}  // namespace

void* reserve(std::size_t bytes, void* hint) {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    if (hint != nullptr) {
        flags |= MAP_FIXED_NOREPLACE;
    }
    void* at = ::mmap(hint, bytes, PROT_NONE, flags, -1, 0);
    if (at == MAP_FAILED) {
        set_errno_error("mmap");
        return nullptr;
    }
    if (hint != nullptr && at != hint) {   // a kernel older than 4.17 takes MAP_FIXED_NOREPLACE as a hint only
        ::munmap(at, bytes);
        t_last_error = "mmap: the hinted address is taken";
        return nullptr;
    }
    return at;
}

bool commit(void* at, std::size_t bytes) {
    const std::uintptr_t mask = page_size() - 1;
    const auto begin = reinterpret_cast<std::uintptr_t>(at) & ~mask;
    const auto end = (reinterpret_cast<std::uintptr_t>(at) + bytes + mask) & ~mask;
    if (::mprotect(reinterpret_cast<void*>(begin), end - begin, PROT_READ | PROT_WRITE) != 0) {
        set_errno_error("mprotect");
        return false;
    }
    return true;
}

bool accessible(const void* at, std::size_t bytes, bool write, std::uintptr_t* span_begin, std::uintptr_t* span_end) {
    if (span_begin != nullptr) {
        *span_begin = 0;
    }
    if (span_end != nullptr) {
        *span_end = 0;
    }
    auto address = reinterpret_cast<std::uintptr_t>(at);
    const std::uintptr_t end = address + (bytes == 0 ? 1 : bytes);
    const std::vector<Mapping> maps = read_maps();
    bool first = true;
    for (const Mapping& mapping : maps) {
        if (mapping.end <= address) {
            continue;
        }
        if (mapping.begin > address || !mapping.read || (write && !mapping.write)) {
            return false;
        }
        if (first && mapping.read && mapping.write) {
            if (span_begin != nullptr) {
                *span_begin = mapping.begin;
            }
            if (span_end != nullptr) {
                *span_end = mapping.end;
            }
        }
        first = false;
        address = mapping.end;
        if (address >= end) {
            return true;
        }
    }
    return false;
}

Module load_module(const char* path) {
    void* module = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        const char* error = ::dlerror();
        t_last_error = error != nullptr ? error : "dlopen failed";
    }
    return module;
}

void* symbol(Module module, const char* name) {
    void* found = ::dlsym(module, name);
    if (found == nullptr) {
        t_last_error = std::string("no symbol ") + name;
    }
    return found;
}

std::string module_file_of(const void* address) {
    Dl_info info{};
    if (::dladdr(address, &info) != 0 && info.dli_fname != nullptr) {
        return info.dli_fname;
    }
    return {};
}

std::uintptr_t module_base_of(const void* address) {
    Dl_info info{};
    if (::dladdr(address, &info) != 0) {
        return reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    }
    return 0;
}

std::string executable_path() {
    std::string path(4096, '\0');
    const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size());
    if (length <= 0) {
        set_errno_error("readlink /proc/self/exe");
        return {};
    }
    path.resize(static_cast<std::size_t>(length));
    return path;
}

std::string last_error() { return t_last_error.empty() ? std::string(std::strerror(errno)) : t_last_error; }

void sleep_ms(std::uint32_t milliseconds) {
    timespec time{static_cast<time_t>(milliseconds / 1000), static_cast<long>(milliseconds % 1000) * 1000000L};
    while (::nanosleep(&time, &time) != 0 && errno == EINTR) {
    }
}

namespace {

std::mutex g_nocase_mutex;
std::unordered_map<std::string, std::string> g_nocase_cache;   // a path as asked -> the existing file it names

bool exists(const std::string& path) {
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0;
}

// `name` in `directory` without regard to case: the entry's own spelling, or "" when there is none
std::string find_entry(const std::string& directory, const std::string& name) {
    DIR* dir = ::opendir(directory.empty() ? "." : directory.c_str());
    if (dir == nullptr) {
        return {};
    }
    std::string found;
    while (const dirent* entry = ::readdir(dir)) {
        if (::strcasecmp(entry->d_name, name.c_str()) == 0) {
            found = entry->d_name;
            if (found == name) {
                break;   // an exact spelling wins over a case variant
            }
        }
    }
    ::closedir(dir);
    return found;
}

}  // namespace

std::string resolve_nocase(const std::string& path) {
    if (path.empty() || exists(path)) {
        return path;
    }
    {
        std::lock_guard lock(g_nocase_mutex);
        if (const auto hit = g_nocase_cache.find(path); hit != g_nocase_cache.end()) {
            return hit->second;
        }
    }
    // walk the components; "." and ".." and repeated slashes are passed on as they are
    std::string resolved = path[0] == '/' ? "/" : "";
    std::size_t at = path[0] == '/' ? 1 : 0;
    while (at <= path.size()) {
        std::size_t next = path.find('/', at);
        if (next == std::string::npos) {
            next = path.size();
        }
        const std::string component = path.substr(at, next - at);
        const bool last = next == path.size();
        if (!component.empty()) {
            std::string candidate = resolved + component;
            if (component != "." && component != ".." && !exists(candidate)) {
                const std::string entry = find_entry(resolved.empty() ? "." : resolved, component);
                if (entry.empty()) {
                    return path;   // nothing matches: the caller's own name (a file to create, or a miss it reports)
                }
                candidate = resolved + entry;
            }
            resolved = last ? candidate : candidate + "/";
        }
        at = next + 1;
    }
    if (exists(resolved)) {
        std::lock_guard lock(g_nocase_mutex);
        g_nocase_cache.emplace(path, resolved);
    }
    return resolved;
}

std::FILE* fopen_nocase(const std::string& path, const char* mode) {
    if (std::FILE* file = std::fopen(path.c_str(), mode)) {
        return file;
    }
    const int error = errno;
    if (error != ENOENT) {
        return nullptr;
    }
    // a file to create keeps its own name, in the directory that exists under another case
    std::string resolved = resolve_nocase(path);
    if (resolved == path) {
        const std::size_t slash = path.rfind('/');
        if (slash == std::string::npos || mode[0] == 'r') {
            errno = error;
            return nullptr;
        }
        const std::string directory = resolve_nocase(path.substr(0, slash));
        resolved = directory + path.substr(slash);
        if (resolved == path) {
            errno = error;
            return nullptr;
        }
    }
    return std::fopen(resolved.c_str(), mode);
}

}  // namespace opentony::os
