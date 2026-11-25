#pragma once

// The operating-system services the standalone host needs beyond the C++ library, for the Linux build (ot_os_posix.cpp). The Windows
// build keeps its Win32 calls where they are; a shared source picks this header under `#ifndef _WIN32`.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace opentony::os {

// ---- address space (VirtualAlloc / VirtualQuery) ----

// Reserves `bytes` of address space, no access, at `hint` exactly when it is not null (nullptr when that place is taken), else anywhere.
[[nodiscard]] void* reserve(std::size_t bytes, void* hint = nullptr);
// Makes [at, at + bytes) of a reservation readable and writable (zero-filled the first time). Page-rounded outwards.
bool commit(void* at, std::size_t bytes);
// Whether [at, at + bytes) is mapped readable (and writable, when `write`). When it is, `span_begin`/`span_end` (optional) receive the
// readable-and-writable mapping the start lies in, or 0/0 when that mapping is read-only.
[[nodiscard]] bool accessible(const void* at, std::size_t bytes, bool write, std::uintptr_t* span_begin = nullptr, std::uintptr_t* span_end = nullptr);

// ---- shared objects (LoadLibrary / GetProcAddress) ----

using Module = void*;
[[nodiscard]] Module load_module(const char* path);
[[nodiscard]] void* symbol(Module module, const char* name);
// the file of the shared object or executable that holds `address`, "" when none does
[[nodiscard]] std::string module_file_of(const void* address);
// the base address the object holding `address` was loaded at, 0 when none does
[[nodiscard]] std::uintptr_t module_base_of(const void* address);

// ---- the process ----

// the running executable's own file (GetModuleFileName(nullptr))
[[nodiscard]] std::string executable_path();
// the last error of a call above, as text (dlerror or strerror(errno))
[[nodiscard]] std::string last_error();
void sleep_ms(std::uint32_t milliseconds);

// ---- files ----

// fopen, then, when the name does not exist as spelled, the same name matched without regard to case component by component (the retail
// game names its files as a case-insensitive Windows file system lets it: "DATA\\levels\\Ware.TRG" for data/LEVELS/WARE.trg).
// `path` uses '/'.
[[nodiscard]] std::FILE* fopen_nocase(const std::string& path, const char* mode);
// the existing file `path` names without regard to case, or `path` itself when nothing matches
[[nodiscard]] std::string resolve_nocase(const std::string& path);

}  // namespace opentony::os
