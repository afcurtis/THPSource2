# Cross-compile the 64-bit standalone (milestone 11b, docs/PORT64_AUDIT.md D5) from Linux with the pinned llvm-mingw
# (`tony setup llvm-mingw`, re/config/llvm_mingw.yml): clang + lld + libc++ for x86_64-w64-mingw32 on the UCRT, C++20 with
# exceptions. MinGW keeps long double as the x87's 80 bits on x86-64 (the MSVC ABI would make it 64), which math_kit.hpp
# needs and asserts. The C++ runtime is linked statically.
#
#   cmake -S src/hooks/kits -B build/hooks/kits64 -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/win64-llvm-mingw.cmake \
#     -DOPENTONY_LLVM_MINGW=.tools/llvm-mingw/<root>

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT OPENTONY_LLVM_MINGW AND DEFINED ENV{OPENTONY_LLVM_MINGW})
    set(OPENTONY_LLVM_MINGW "$ENV{OPENTONY_LLVM_MINGW}")
endif()
if(NOT OPENTONY_LLVM_MINGW OR NOT EXISTS "${OPENTONY_LLVM_MINGW}/bin/x86_64-w64-mingw32-clang++")
    message(FATAL_ERROR
        "OPENTONY_LLVM_MINGW must name the llvm-mingw root (run `tony setup llvm-mingw`); got '${OPENTONY_LLVM_MINGW}'")
endif()
get_filename_component(OPENTONY_LLVM_MINGW "${OPENTONY_LLVM_MINGW}" ABSOLUTE)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES OPENTONY_LLVM_MINGW)

set(CMAKE_C_COMPILER "${OPENTONY_LLVM_MINGW}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${OPENTONY_LLVM_MINGW}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "${OPENTONY_LLVM_MINGW}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_AR "${OPENTONY_LLVM_MINGW}/bin/llvm-ar")
set(CMAKE_RANLIB "${OPENTONY_LLVM_MINGW}/bin/llvm-ranlib")

# double and float on SSE2 is the x86-64 default; long double stays x87 (80 bits)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

set(CMAKE_FIND_ROOT_PATH "${OPENTONY_LLVM_MINGW}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
