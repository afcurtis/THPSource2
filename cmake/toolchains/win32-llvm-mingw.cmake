# Cross-compile the 32-bit Kit DLL (opentony_native.dll, src/hooks/kits) from
# Linux with the pinned llvm-mingw (`tony setup llvm-mingw`,
# re/config/llvm_mingw.yml): clang + lld + libc++ for i686-w64-mingw32 on the
# UCRT, C++20 with exceptions. The C++ runtime is linked statically, so the DLL
# needs nothing beside it but the system's UCRT.
#
#   cmake -S src/hooks/kits -B build/hooks/kits -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/win32-llvm-mingw.cmake \
#     -DOPENTONY_LLVM_MINGW=.tools/llvm-mingw/<root>

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(NOT OPENTONY_LLVM_MINGW AND DEFINED ENV{OPENTONY_LLVM_MINGW})
    set(OPENTONY_LLVM_MINGW "$ENV{OPENTONY_LLVM_MINGW}")
endif()
if(NOT OPENTONY_LLVM_MINGW OR NOT EXISTS "${OPENTONY_LLVM_MINGW}/bin/i686-w64-mingw32-clang++")
    message(FATAL_ERROR
        "OPENTONY_LLVM_MINGW must name the llvm-mingw root (run `tony setup llvm-mingw`); got '${OPENTONY_LLVM_MINGW}'")
endif()
get_filename_component(OPENTONY_LLVM_MINGW "${OPENTONY_LLVM_MINGW}" ABSOLUTE)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES OPENTONY_LLVM_MINGW)

set(CMAKE_C_COMPILER "${OPENTONY_LLVM_MINGW}/bin/i686-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${OPENTONY_LLVM_MINGW}/bin/i686-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "${OPENTONY_LLVM_MINGW}/bin/i686-w64-mingw32-windres")
set(CMAKE_AR "${OPENTONY_LLVM_MINGW}/bin/llvm-ar")
set(CMAKE_RANLIB "${OPENTONY_LLVM_MINGW}/bin/llvm-ranlib")

# double and float arithmetic on SSE2, as in the x86-64 builds the Kits were
# validated with; long double stays x87 (80 bits) on both.
set(CMAKE_C_FLAGS_INIT "-msse2 -mfpmath=sse")
set(CMAKE_CXX_FLAGS_INIT "-msse2 -mfpmath=sse")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

set(CMAKE_FIND_ROOT_PATH "${OPENTONY_LLVM_MINGW}/i686-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
