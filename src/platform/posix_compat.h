#ifndef OPENTONY_POSIX_COMPAT_H
#define OPENTONY_POSIX_COMPAT_H

/* The Linux build's forced include (src/linux/CMakeLists.txt, -include): the Microsoft keywords the shared sources spell, for a
 * GCC or Clang x86-64 ELF target. On x86-64 there is one calling convention (System V here), so the retail conventions the Kit
 * adapters name are all that one, as they are all the Microsoft x64 convention in the Windows 64-bit build (kit_dll.cpp,
 * "On x86-64 the three conventions are one"). Never included in a Windows build. */

#if defined(_WIN32)
#error "posix_compat.h is for the Linux build only"
#endif
#if !defined(__x86_64__)
#error "the Linux build targets x86-64 only (math_kit.hpp runs x87 instructions, docs/PORT64_AUDIT.md)"
#endif

#define __cdecl
#define __stdcall
#define __fastcall
#define __thiscall
#define WINAPI

/* __declspec(dllexport) is a default-visibility symbol of the shared object; the others the sources use have no ELF meaning */
#define __declspec(x) OT_DECLSPEC_##x
#define OT_DECLSPEC_dllexport __attribute__((visibility("default")))
#define OT_DECLSPEC_dllimport
#define OT_DECLSPEC_noinline __attribute__((noinline))
#define OT_DECLSPEC_noreturn __attribute__((noreturn))

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

#ifdef __cplusplus
#include <cstring>
#include <strings.h>
#else
#include <string.h>
#include <strings.h>
#endif

#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp

#endif /* OPENTONY_POSIX_COMPAT_H */
