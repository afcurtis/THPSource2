# Cross-compile the 32-bit Windows components (hook DLL, dinput proxy, capture
# recorder) from Linux with clang-cl and lld-link.
#
# Headers, import libraries and the static CRT come from the pinned Visual C++
# 6.0 SP3 toolchain (`tony setup vc6`). VC6 ships upper-case file names, so
# `tony hooks sdk` publishes a lower-case symlink view under
# build/toolchain/vc6-sdk and passes it here as OPENTONY_WIN32_SDK. The same
# sources also build with a native MSVC `-A Win32` generator on Windows.
#
#   cmake -S src/hooks -B build/hooks/win32 -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/win32-clang-cl.cmake \
#     -DOPENTONY_WIN32_SDK=build/toolchain/vc6-sdk

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 5.0)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(NOT OPENTONY_WIN32_SDK AND DEFINED ENV{OPENTONY_WIN32_SDK})
    set(OPENTONY_WIN32_SDK "$ENV{OPENTONY_WIN32_SDK}")
endif()
if(NOT OPENTONY_WIN32_SDK OR NOT EXISTS "${OPENTONY_WIN32_SDK}/include/windows.h")
    message(FATAL_ERROR
        "OPENTONY_WIN32_SDK must name the lower-case VC6 SDK view "
        "(run `tony hooks sdk`); got '${OPENTONY_WIN32_SDK}'")
endif()
get_filename_component(OPENTONY_WIN32_SDK "${OPENTONY_WIN32_SDK}" ABSOLUTE)
# try_compile() projects re-read this file; forward the SDK location to them.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES OPENTONY_WIN32_SDK)

find_program(OPENTONY_CLANG_CL NAMES clang-cl clang-cl-21 clang-cl-20 clang-cl-19 REQUIRED)
find_program(OPENTONY_LLD_LINK NAMES lld-link lld-link-21 lld-link-20 lld-link-19 REQUIRED)
find_program(OPENTONY_LLVM_LIB NAMES llvm-lib llvm-lib-21 llvm-lib-20 llvm-lib-19 REQUIRED)
find_program(OPENTONY_LLVM_RC NAMES llvm-rc llvm-rc-21 llvm-rc-20 llvm-rc-19)

set(CMAKE_C_COMPILER "${OPENTONY_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${OPENTONY_CLANG_CL}")
set(CMAKE_C_COMPILER_TARGET i686-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET i686-pc-windows-msvc)
set(CMAKE_LINKER "${OPENTONY_LLD_LINK}")
set(CMAKE_AR "${OPENTONY_LLVM_LIB}")
if(OPENTONY_LLVM_RC)
    set(CMAKE_RC_COMPILER "${OPENTONY_LLVM_RC}")
endif()

# _MSC_VER 1600 keeps CMake on linker flags lld-link accepts (no /pdbtype,
# plain /INCREMENTAL); the VC6 headers only test _MSC_VER lower bounds. No stack
# cookies or thread-safe statics: the VC6 CRT has neither runtime helper.
set(_opentony_win32_cflags
    "-fms-compatibility-version=16.00 /GS- /Zc:threadSafeInit- -Wno-nonportable-include-path -imsvc \"${OPENTONY_WIN32_SDK}/include\"")
set(CMAKE_C_FLAGS_INIT "${_opentony_win32_cflags}")
set(CMAKE_CXX_FLAGS_INIT "${_opentony_win32_cflags} /GR- /EHs-c-")

# VC6 CRT objects predate SafeSEH tables. lld resolves archive members in
# command-line order, so a DLL must pull the DLL CRT entry before any CRT
# helper can drag in the EXE startup object (duplicate ___app_type etc.).
set(_opentony_win32_ldflags "/safeseh:no /manifest:no \"/libpath:${OPENTONY_WIN32_SDK}/lib\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_opentony_win32_ldflags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_opentony_win32_ldflags} /include:__DllMainCRTStartup@12")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_opentony_win32_ldflags}")

set(CMAKE_FIND_ROOT_PATH "${OPENTONY_WIN32_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
