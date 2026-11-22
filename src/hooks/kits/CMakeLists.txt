cmake_minimum_required(VERSION 3.20)

# opentony_native.dll (Phase 5.0): the Kits of src/runtime/ inside the retail
# process, with their generated hook adapters (kit_hooks_generated.cpp, from
# `tony hooks generate`) and the live host (kit_dll.cpp). opentony.dll loads it
# (src/hooks/kit_abi.h). Built with llvm-mingw from Linux
# (cmake/toolchains/win32-llvm-mingw.cmake, `tony hooks build`).
project(OpenTonyKits LANGUAGES C CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "OpenTonyKits builds Windows binaries (cmake/toolchains/win32-llvm-mingw.cmake, or win64-llvm-mingw.cmake for the 64-bit standalone)")
endif()
# Milestone 11b (docs/PORT64_AUDIT.md D1, D5): the 64-bit build is the standalone only (its DLL, the spike, the video DLL). The hooked build
# (opentony_native.dll) and the Bink compare tool live in the 32-bit retail process and stay 32-bit.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(OPENTONY_KITS64 ON)
else()
    set(OPENTONY_KITS64 OFF)
endif()

get_filename_component(OPENTONY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
include("${OPENTONY_ROOT}/cmake/opentony_sources.cmake")

# Milestone 8c (Phase 5.3c): upstream zlib, compiled from the pinned source (re/config/zlib.yml, `tony hooks build` fetches and checks it) as a static library.
# It stands in for the retail executable's zlib 1.1.2 (0x004f9c80..0x004ffd39): the zlib seams of the standalone (standalone_host.cpp) and pkr_asset.cpp (the
# PKR archive reader) use it. An archive member that nothing references is not linked, so opentony_native.dll (the hooked build) carries none of it.
set(OPENTONY_ZLIB_SOURCE "" CACHE PATH "the pinned zlib source directory (tony hooks build passes it)")
if(NOT EXISTS "${OPENTONY_ZLIB_SOURCE}/zlib.h")
    message(FATAL_ERROR "OPENTONY_ZLIB_SOURCE must contain zlib.h (re/config/zlib.yml; `tony hooks build` passes it)")
endif()
set(OPENTONY_ZLIB_FILES adler32 compress crc32 deflate inflate infback inffast inftrees trees uncompr zutil)
list(TRANSFORM OPENTONY_ZLIB_FILES PREPEND "${OPENTONY_ZLIB_SOURCE}/")
list(TRANSFORM OPENTONY_ZLIB_FILES APPEND ".c")
add_library(opentony_zlib STATIC ${OPENTONY_ZLIB_FILES})
target_include_directories(opentony_zlib SYSTEM PUBLIC "${OPENTONY_ZLIB_SOURCE}")
target_compile_options(opentony_zlib PRIVATE -w)   # upstream code, built as shipped

# The portable reconstruction, as one archive: the DLL links only what the
# Kit hooks reach.
set(OPENTONY_KIT_LIBRARY_SOURCES ${OPENTONY_ASSETS_SOURCES} ${OPENTONY_TRG_SOURCES} ${OPENTONY_RUNTIME_SOURCES})
list(TRANSFORM OPENTONY_KIT_LIBRARY_SOURCES PREPEND "${OPENTONY_ROOT}/")

# 11d (docs/PORT64_AUDIT.md D3): two flavours of the same sources. `opentony_kits_runtime` is the production archive of the DLLs: the
# guest memory is the arena backend only (OPENTONY_GUEST_MEMORY_FLAT=1, PUBLIC so kit_dll.cpp and the generated adapters see the same
# class). `opentony_kits_runtime_fixture` keeps the sparse region backend for the 32-bit forced-call replays below and is built only for
# them. The two mangle `RetailMemory` differently (an inline namespace in retail_memory.hpp): no target may link both.
function(opentony_kits_runtime_library name)
    add_library(${name} STATIC ${ARGN} ${OPENTONY_KIT_LIBRARY_SOURCES})
    target_link_libraries(${name} PUBLIC opentony_zlib)
    target_compile_features(${name} PUBLIC cxx_std_20)
    target_include_directories(${name} PUBLIC
        "${OPENTONY_ROOT}/src/runtime"
        "${OPENTONY_ROOT}/src/trg"
        "${OPENTONY_ROOT}/src/assets"
        "${OPENTONY_ROOT}/src/camera"
        "${OPENTONY_ROOT}/src"
    )
endfunction()
opentony_kits_runtime_library(opentony_kits_runtime)
target_compile_definitions(opentony_kits_runtime PUBLIC OPENTONY_GUEST_MEMORY_FLAT=1)
opentony_kits_runtime_library(opentony_kits_runtime_fixture EXCLUDE_FROM_ALL)

if(NOT OPENTONY_KITS64)
add_library(opentony_native SHARED
    kit_dll.cpp
    kit_hooks_generated.cpp
)
target_link_libraries(opentony_native PRIVATE opentony_kits_runtime)
target_include_directories(opentony_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(opentony_native PROPERTIES PREFIX "" OUTPUT_NAME opentony_native)
target_compile_options(opentony_native PRIVATE -Wall -Wextra)
# only ot_kit_attach is exported (dllexport); no lib prefix, no import library needed
target_link_options(opentony_native PRIVATE -Wl,--no-insert-timestamp)
endif()

# Phase 5.3b: the HAL's movie and music decoder (src/hal/ffmpeg): Bink files decoded by the pinned FFmpeg, whose DLLs it loads at
# run time (`tony setup ffmpeg`, re/config/ffmpeg.yml). Only the standalone and the compare tool use it, never opentony_native.dll.
set(OPENTONY_FFMPEG_INCLUDE "" CACHE PATH "include directory of the pinned FFmpeg (tony hooks build passes it)")
if(NOT OPENTONY_FFMPEG_INCLUDE)
    message(FATAL_ERROR "OPENTONY_FFMPEG_INCLUDE is not set (run `tony setup ffmpeg`; `tony hooks build` passes it)")
endif()
add_library(opentony_movie_source STATIC "${OPENTONY_ROOT}/src/hal/ffmpeg/movie_source.cpp")
target_compile_features(opentony_movie_source PUBLIC cxx_std_20)
target_include_directories(opentony_movie_source PUBLIC "${OPENTONY_ROOT}/src/hal" "${OPENTONY_ROOT}/src/hal/ffmpeg")
target_include_directories(opentony_movie_source SYSTEM PUBLIC "${OPENTONY_FFMPEG_INCLUDE}")
target_compile_options(opentony_movie_source PRIVATE -Wall -Wextra)

# the proof tool: binkw32.dll and the FFmpeg source on the same file, frame by frame (tests/test_movie_compare.py)
if(NOT OPENTONY_KITS64)
add_executable(opentony_movie_compare movie_compare.cpp)
target_link_libraries(opentony_movie_compare PRIVATE opentony_movie_source)
target_compile_options(opentony_movie_compare PRIVATE -Wall -Wextra)
endif()

# Phase 5.4a: opentony_standalone.dll, for a program with no retail image: the same Kits and the same host code
# (kit_dll.cpp with OPENTONY_STANDALONE) over EVERY native by retail address (standalone_kits_generated.cpp) and the
# seams (standalone_seams.cpp). A separate DLL because the hooked build is the differential reference: linking all the
# natives into opentony_native.dll (4.6 -> 8.4 MB) changed the stress-clock session scenarios of the retail process.
add_library(opentony_standalone SHARED
    kit_dll.cpp
    standalone_kits_generated.cpp
    guest_arena.cpp                         # 11b: the guest arena and the guest heap, docs/PORT64_AUDIT.md section 5
    standalone_seams.cpp
    standalone_movie.cpp
    standalone_files.cpp
    standalone_surfaces.cpp
    standalone_platform.cpp
    standalone_music.cpp                    # the custom soundtrack (docs/LAUNCHER.md): the player's Bink files in Mods\Music for the level songs
    standalone_options.cpp                  # the player's options (docs/LAUNCHER.md): the display, fog, colour, controller options
    standalone_scores.cpp                   # the score file (docs/LAUNCHER.md, "Scores"): every finished run appended to OT_SCORES_FILE
    standalone_challenge.cpp                # the quick start's Goals Speed Run and Competition Session (docs/LAUNCHER.md, "Quick start")
    standalone_freelook.cpp                # the free look's switches (runtime/free_look.hpp)
    standalone_audio.cpp
    standalone_host.cpp
    standalone_boot.cpp                     # 9: the cold boot
    standalone_static_init_generated.cpp    # 9: scripts/static_init_transcribe.py
    standalone_session.cpp                  # 9: the session recorder
    standalone_scenario.cpp                 # 10f: a Warehouse scenario (the retail capture's frontend bootstrap, input and frame recorder)
    ../../capture/win32/shared_buffer.cpp   # 10f: the capture's own frame writer, so the wire layout is the retail recorder's
    ../mod_overlay.cpp   # the loose-file overlay of 5.2, the code of the hooked build, in front of the file backend (standalone_files.cpp)
)
target_compile_definitions(opentony_standalone PRIVATE OPENTONY_STANDALONE=1)
target_link_libraries(opentony_standalone PRIVATE opentony_kits_runtime opentony_movie_source opentony_zlib)
target_include_directories(opentony_standalone PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}/../../capture/win32")
set_target_properties(opentony_standalone PROPERTIES PREFIX "" OUTPUT_NAME opentony_standalone)
target_compile_options(opentony_standalone PRIVATE -Wall -Wextra)
target_link_options(opentony_standalone PRIVATE -Wl,--no-insert-timestamp)

# Phase 5.4a: a program with no retail image. It reserves the retail address range itself (the
# executable is linked far above it), copies the data sections out of THawk2.exe as a file and
# runs Kit hooks over that memory (PHASE_5_SUMMARY.md, "5.4a result").
add_executable(opentony_spike standalone_spike.cpp ../oracle_script.cpp)
set_target_properties(opentony_spike PROPERTIES OUTPUT_NAME opentony_spike)
# Milestone 7c: the program's video (standalone_video.cpp): a recording layer over the HAL's SDL GL backend, SDL3.dll loaded at run time (OT_VIDEO_SDL). A DLL
# of its own that the spike loads only when OT_VIDEO_DIR is set: linked into the executable, it moved the executable's zero-filled section past 0x00524000.
set(OPENTONY_SDL3_INCLUDE "" CACHE PATH "SDL3 include directory (tony hooks build passes it)")
if(NOT EXISTS "${OPENTONY_SDL3_INCLUDE}/SDL3/SDL.h")
    message(FATAL_ERROR "OPENTONY_SDL3_INCLUDE must contain SDL3/SDL.h (run `tony setup sdl3`; `tony hooks build` passes it)")
endif()
set(OPENTONY_HAL_DIR "${OPENTONY_ROOT}/src/hal")
add_library(opentony_spike_video SHARED
    standalone_video.cpp
    standalone_png.cpp
    standalone_texture_dump.cpp
    "${OPENTONY_HAL_DIR}/video.cpp"
    "${OPENTONY_HAL_DIR}/keymap.cpp"
    "${OPENTONY_HAL_DIR}/sdl/sdl_loader.cpp"
    "${OPENTONY_HAL_DIR}/sdl/sdl_window.cpp"
    "${OPENTONY_HAL_DIR}/sdl/sdl_input.cpp"
    "${OPENTONY_HAL_DIR}/sdl/sdl_audio.cpp"
    "${OPENTONY_HAL_DIR}/audio.cpp"
    "${OPENTONY_HAL_DIR}/movie_video.cpp"
    "${OPENTONY_HAL_DIR}/sdl/sdl_video_gl.cpp"
    "${OPENTONY_HAL_DIR}/sdl/sdl_video_gpu.cpp"
    "${OPENTONY_HAL_DIR}/oracle/video_gpu.cpp"
)
set_target_properties(opentony_spike_video PROPERTIES PREFIX "" OUTPUT_NAME opentony_spike_video)
target_include_directories(opentony_spike_video SYSTEM PRIVATE "${OPENTONY_SDL3_INCLUDE}")
target_compile_definitions(opentony_spike_video PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(opentony_spike_video PRIVATE -Wall -Wextra)
target_link_libraries(opentony_spike_video PRIVATE user32 opentony_zlib)   # zlib: the screenshot key's PNG
target_link_options(opentony_spike_video PRIVATE -Wl,--no-insert-timestamp)
add_dependencies(opentony_spike opentony_spike_video)

# Milestone 12: what a player starts. It writes the static-data image from THawk2.exe and unpacks ALL.PKR once, then runs opentony_spike.exe with the window,
# the input, the sound and the real-time clock on (src/launcher/opentony_launcher.cpp). An ordinary executable: the spike's link constraints are not its own.
add_executable(opentony ../../launcher/opentony_launcher.cpp ../../assets/pkr_asset.cpp utf8_manifest.rc)   # the UTF-8 code page (utf8.manifest)
target_include_directories(opentony PRIVATE "${OPENTONY_ROOT}/src")
target_compile_features(opentony PRIVATE cxx_std_20)
target_compile_options(opentony PRIVATE -Wall -Wextra)
target_link_libraries(opentony PRIVATE opentony_zlib bcrypt user32)
target_link_options(opentony PRIVATE -Wl,--no-insert-timestamp)
add_dependencies(opentony opentony_spike opentony_standalone)

# THPSource2.exe (docs/LAUNCHER.md): the player's launcher in front of opentony.exe. It finds or imports the game's files (an installed game, the disc,
# a disc image), keeps the settings (PARTYMOD's partymod.ini schema) and the game's own bindings, both in Config\THPSource2.ini, and starts opentony.exe with
# them. It draws with Dear ImGui (re/config/imgui.yml, the pinned source) on SDL3, linked through the pinned package's import library (SDL3.dll ships in Build\).
set(OPENTONY_IMGUI_SOURCE "" CACHE PATH "the pinned Dear ImGui source directory (tony hooks build passes it)")
if(NOT EXISTS "${OPENTONY_IMGUI_SOURCE}/imgui.h")
    message(FATAL_ERROR "OPENTONY_IMGUI_SOURCE must contain imgui.h (re/config/imgui.yml; `tony hooks build` passes it)")
endif()
add_library(opentony_imgui STATIC
    "${OPENTONY_IMGUI_SOURCE}/imgui.cpp"
    "${OPENTONY_IMGUI_SOURCE}/imgui_draw.cpp"
    "${OPENTONY_IMGUI_SOURCE}/imgui_tables.cpp"
    "${OPENTONY_IMGUI_SOURCE}/imgui_widgets.cpp"
    "${OPENTONY_IMGUI_SOURCE}/backends/imgui_impl_sdl3.cpp"
    "${OPENTONY_IMGUI_SOURCE}/backends/imgui_impl_sdlrenderer3.cpp"
)
target_include_directories(opentony_imgui SYSTEM PUBLIC "${OPENTONY_IMGUI_SOURCE}" "${OPENTONY_IMGUI_SOURCE}/backends" "${OPENTONY_SDL3_INCLUDE}")
target_compile_definitions(opentony_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_options(opentony_imgui PRIVATE -w)   # upstream code, built as shipped
get_filename_component(OPENTONY_SDL3_ROOT "${OPENTONY_SDL3_INCLUDE}" DIRECTORY)
if(OPENTONY_KITS64)
    set(OPENTONY_SDL3_IMPORT "${OPENTONY_SDL3_ROOT}/lib/x64/SDL3.lib")
else()
    set(OPENTONY_SDL3_IMPORT "${OPENTONY_SDL3_ROOT}/lib/x86/SDL3.lib")
endif()
set(OPENTONY_LAUNCHER_DIR "${OPENTONY_ROOT}/src/launcher/thpsource2")
# windres's includes are not scanned: a new icon or header picture (scripts/make_launcher_icon.py) must rebuild the resource object
set_source_files_properties("${OPENTONY_LAUNCHER_DIR}/app/launcher.rc" PROPERTIES
    OBJECT_DEPENDS "${OPENTONY_LAUNCHER_DIR}/app/thpsource2.ico;${OPENTONY_LAUNCHER_DIR}/app/header.png")
add_executable(THPSource2 WIN32
    "${OPENTONY_LAUNCHER_DIR}/app/main.cpp"
    "${OPENTONY_LAUNCHER_DIR}/app/platform.cpp"
    "${OPENTONY_LAUNCHER_DIR}/app/launcher.rc"
    "${OPENTONY_LAUNCHER_DIR}/core/ini.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/settings.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/bindings.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/iso9660.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/game_files.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/png.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/true_colour.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/upscale.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/upscale_setup.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/trick_sets.cpp"
    "${OPENTONY_LAUNCHER_DIR}/core/update_check.cpp"
    "${OPENTONY_ROOT}/src/runtime/trick_database.cpp"
    "${OPENTONY_ROOT}/src/runtime/trick_combo_tables.cpp"
    "${OPENTONY_LAUNCHER_DIR}/app/upscale_command.cpp"
    "${OPENTONY_LAUNCHER_DIR}/app/upscale_task.cpp"
    "${OPENTONY_LAUNCHER_DIR}/app/update_task.cpp"
    "${OPENTONY_HAL_DIR}/keymap.cpp"
)
target_include_directories(THPSource2 PRIVATE "${OPENTONY_ROOT}/src" "${OPENTONY_LAUNCHER_DIR}/core" "${OPENTONY_LAUNCHER_DIR}/app")
target_compile_features(THPSource2 PRIVATE cxx_std_20)
target_compile_options(THPSource2 PRIVATE -Wall -Wextra)
target_compile_definitions(THPSource2 PRIVATE NOMINMAX)
target_link_libraries(THPSource2 PRIVATE opentony_imgui opentony_zlib "${OPENTONY_SDL3_IMPORT}" shell32 imm32)
# the launcher sits in the install folder and SDL3.dll in its Build folder: SDL3.dll is loaded on the first SDL call, after main() has put Build on the DLL
# search path (app/main.cpp; SDL_MAIN_HANDLED, since SDL_main.h's WinMain would call SDL3.dll before main)
target_link_options(THPSource2 PRIVATE -Wl,--no-insert-timestamp -Wl,--delayload=SDL3.dll)
add_dependencies(THPSource2 opentony)

# The one-time reads of the player's files as a build step: `-DOPENTONY_GAME_DIR=<install>` makes `cmake --build . --target opentony_prepare` write
# <install>\Data\Unpacked\thawk2_static.img (the static data of THawk2.exe) and unpack ALL.PKR, the same work the first run of opentony.exe does. Run through
# OPENTONY_PREPARE_RUNNER (e.g. `wine`) when the build is a cross build on Linux; an install on a Windows disk from WSL needs none of this (opentony.exe does it).
set(OPENTONY_GAME_DIR "" CACHE PATH "a THawk2 install (THawk2.exe + ALL.PKR) for the opentony_prepare target")
set(OPENTONY_PREPARE_RUNNER "" CACHE STRING "program that runs opentony.exe during the build (wine), empty on Windows")
if(OPENTONY_GAME_DIR)
    separate_arguments(OPENTONY_PREPARE_RUNNER_LIST UNIX_COMMAND "${OPENTONY_PREPARE_RUNNER}")
    add_custom_target(opentony_prepare ALL
        COMMAND ${OPENTONY_PREPARE_RUNNER_LIST} $<TARGET_FILE:opentony> --root "${OPENTONY_GAME_DIR}" --game-dir "${OPENTONY_GAME_DIR}" --prepare
        DEPENDS opentony
        COMMENT "opentony_prepare: static data and package of ${OPENTONY_GAME_DIR}"
        VERBATIM)
endif()
target_compile_features(opentony_spike PRIVATE cxx_std_20)
target_compile_options(opentony_spike PRIVATE -Wall -Wextra)
if(OPENTONY_KITS64)
    # 11b: the guest memory is a 4 GiB arena the program reserves anywhere (guest_arena.hpp); the executable itself goes where the loader puts it
    target_link_options(opentony_spike PRIVATE -Wl,--no-insert-timestamp)
    # the UTF-8 code page (utf8.manifest), so an install path outside the system's code page works; not in the 32-bit build, whose image layout is fixed
    target_sources(opentony_spike PRIVATE utf8_manifest.rc)
else()
    target_link_options(opentony_spike PRIVATE -Wl,--image-base=0x00400000 -Wl,--no-dynamicbase -Wl,--no-insert-timestamp)
endif()

# The forced-call replays of src/runtime/*_retail_calls_test.cpp, built 32-bit:
# `tony hooks replay32` runs them under Wine on the fixtures the native (x86-64)
# ctest uses, so the Kits are known to behave the same in a 32-bit build.
file(GLOB OPENTONY_REPLAY_TESTS "${OPENTONY_ROOT}/src/runtime/*_retail_calls_test.cpp")
add_custom_target(kit_replay_tests)
foreach(source ${OPENTONY_REPLAY_TESTS})
    get_filename_component(test "${source}" NAME_WE)
    add_executable(opentony_${test} EXCLUDE_FROM_ALL "${source}")
    target_link_libraries(opentony_${test} PRIVATE opentony_kits_runtime_fixture)   # 11d: the sparse backend the fixtures need
    target_include_directories(opentony_${test} PRIVATE "${OPENTONY_ROOT}")
    add_dependencies(kit_replay_tests opentony_${test})
endforeach()

# Phase 5.3b: plays movies and music through the standalone seams (no retail image, no Bink) into a recording HAL;
# tests/test_movie_compare.py checks what it recorded against Bink's own decode. Linked at the retail base like the spike.
if(NOT OPENTONY_KITS64)   # 11b: still the 32-bit reserving-section layout (movie_player.cpp)
add_executable(opentony_movie_player movie_player.cpp)
target_compile_features(opentony_movie_player PRIVATE cxx_std_20)
target_include_directories(opentony_movie_player PRIVATE "${OPENTONY_ROOT}/src/hal")
target_compile_options(opentony_movie_player PRIVATE -Wall -Wextra)
target_link_options(opentony_movie_player PRIVATE -Wl,--image-base=0x00400000 -Wl,--no-dynamicbase -Wl,--no-insert-timestamp)
endif()
