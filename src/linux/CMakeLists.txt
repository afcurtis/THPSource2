cmake_minimum_required(VERSION 3.20)

# The native x86-64 Linux build of the standalone game: the programs of src/hooks/kits/CMakeLists.txt's 64-bit standalone (opentony_standalone,
# opentony_spike, opentony_spike_video, opentony) as ELF shared objects and executables. Same sources, same process layout: the spike reserves the
# guest arena and dlopen()s the standalone and the video object beside it, as the Windows build LoadLibrary()s its DLLs. The OS differences are in
# src/platform (ot_os.hpp) behind `#ifdef _WIN32` in the shared sources; the Microsoft keywords in src/platform/posix_compat.h, a forced include.
#
#   cmake -S src/linux -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/linux
#
# SDL3 and FFmpeg are loaded at run time (dlopen, as the Windows build loads their DLLs): only their headers are needed to build. They default to the
# system's; OPENTONY_SDL3_INCLUDE / OPENTONY_FFMPEG_INCLUDE point elsewhere (scripts/linux/fetch-headers.sh unpacks the distribution's -dev packages
# into .tools/linux/sysroot without root).
project(OpenTonyLinux LANGUAGES C CXX)

if(WIN32 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    message(FATAL_ERROR "src/linux builds the x86-64 Linux standalone only")
endif()

get_filename_component(OPENTONY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${OPENTONY_ROOT}/cmake/opentony_sources.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # the runtime archive is linked into a shared object

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

set(OPENTONY_LINUX_SYSROOT "${OPENTONY_ROOT}/.tools/linux/sysroot/usr/include")
if(EXISTS "${OPENTONY_LINUX_SYSROOT}/SDL3/SDL.h")
    set(_sdl3_default "${OPENTONY_LINUX_SYSROOT}")
else()
    set(_sdl3_default "/usr/include")
endif()
if(EXISTS "${OPENTONY_LINUX_SYSROOT}/x86_64-linux-gnu/libavcodec/avcodec.h")
    set(_ffmpeg_default "${OPENTONY_LINUX_SYSROOT}/x86_64-linux-gnu")
elseif(EXISTS "/usr/include/x86_64-linux-gnu/libavcodec/avcodec.h")
    set(_ffmpeg_default "/usr/include/x86_64-linux-gnu")
else()
    set(_ffmpeg_default "/usr/include")
endif()
set(OPENTONY_SDL3_INCLUDE "${_sdl3_default}" CACHE PATH "directory holding SDL3/SDL.h")
set(OPENTONY_FFMPEG_INCLUDE "${_ffmpeg_default}" CACHE PATH "directory holding libavcodec/avcodec.h")
foreach(header "${OPENTONY_SDL3_INCLUDE}/SDL3/SDL.h" "${OPENTONY_FFMPEG_INCLUDE}/libavcodec/avcodec.h")
    if(NOT EXISTS "${header}")
        message(FATAL_ERROR "${header} is missing: install libsdl3-dev and libavcodec-dev/libavformat-dev, or run scripts/linux/fetch-headers.sh")
    endif()
endforeach()

# every target: the Microsoft keywords of the shared sources (C and C++)
add_compile_options("SHELL:-include ${OPENTONY_ROOT}/src/platform/posix_compat.h")
set(OT_WARNINGS -Wall -Wextra)

set(KITS "${OPENTONY_ROOT}/src/hooks/kits")
set(HAL "${OPENTONY_ROOT}/src/hal")

# ---- the platform layer ----
add_library(opentony_platform STATIC "${OPENTONY_ROOT}/src/platform/ot_os_posix.cpp" "${OPENTONY_ROOT}/src/platform/win32_posix.cpp")
target_include_directories(opentony_platform PUBLIC "${OPENTONY_ROOT}/src/platform")
target_compile_options(opentony_platform PRIVATE ${OT_WARNINGS})
target_link_libraries(opentony_platform PUBLIC ${CMAKE_DL_LIBS})

# ---- the reconstruction, production flavour (docs/PORT64_AUDIT.md D3: the arena backend only) ----
set(OPENTONY_KIT_LIBRARY_SOURCES ${OPENTONY_ASSETS_SOURCES} ${OPENTONY_TRG_SOURCES} ${OPENTONY_RUNTIME_SOURCES})
list(TRANSFORM OPENTONY_KIT_LIBRARY_SOURCES PREPEND "${OPENTONY_ROOT}/")
add_library(opentony_kits_runtime STATIC ${OPENTONY_KIT_LIBRARY_SOURCES})
target_compile_definitions(opentony_kits_runtime PUBLIC OPENTONY_GUEST_MEMORY_FLAT=1)
target_include_directories(opentony_kits_runtime PUBLIC
    "${OPENTONY_ROOT}/src/runtime"
    "${OPENTONY_ROOT}/src/trg"
    "${OPENTONY_ROOT}/src/assets"
    "${OPENTONY_ROOT}/src/camera"
    "${OPENTONY_ROOT}/src"
)
target_link_libraries(opentony_kits_runtime PUBLIC ZLIB::ZLIB)
# hidden: the shared object exports only its ot_* entry points (dllexport in posix_compat.h)
set_target_properties(opentony_kits_runtime PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# ---- the movie and music decoder (FFmpeg, dlopen()ed) ----
add_library(opentony_movie_source STATIC "${HAL}/ffmpeg/movie_source.cpp")
target_include_directories(opentony_movie_source PUBLIC "${HAL}" "${HAL}/ffmpeg")
target_include_directories(opentony_movie_source SYSTEM PUBLIC "${OPENTONY_FFMPEG_INCLUDE}")
target_compile_options(opentony_movie_source PRIVATE ${OT_WARNINGS})
target_link_libraries(opentony_movie_source PUBLIC opentony_platform)
set_target_properties(opentony_platform PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
set_target_properties(opentony_movie_source PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# ---- opentony_standalone.so: the Kits over every native by retail address, and the seams ----
add_library(opentony_standalone SHARED
    "${KITS}/kit_dll.cpp"
    "${KITS}/standalone_kits_generated.cpp"
    "${KITS}/guest_arena.cpp"
    "${KITS}/standalone_seams.cpp"
    "${KITS}/standalone_movie.cpp"
    "${KITS}/standalone_files.cpp"
    "${KITS}/standalone_surfaces.cpp"
    "${KITS}/standalone_platform.cpp"
    "${KITS}/standalone_music.cpp"
    "${KITS}/standalone_options.cpp"
    "${KITS}/standalone_scores.cpp"
    "${KITS}/standalone_challenge.cpp"
    "${KITS}/standalone_freelook.cpp"
    "${KITS}/standalone_audio.cpp"
    "${KITS}/standalone_host.cpp"
    "${KITS}/standalone_boot.cpp"
    "${KITS}/standalone_static_init_generated.cpp"
    "${KITS}/standalone_session.cpp"
    "${KITS}/standalone_scenario.cpp"
    "${OPENTONY_ROOT}/src/capture/win32/shared_buffer.cpp"
    "${OPENTONY_ROOT}/src/hooks/mod_overlay.cpp"
)
target_compile_definitions(opentony_standalone PRIVATE OPENTONY_STANDALONE=1)
target_include_directories(opentony_standalone PRIVATE "${KITS}" "${OPENTONY_ROOT}/src/hooks" "${OPENTONY_ROOT}/src/capture/win32")
target_link_libraries(opentony_standalone PRIVATE opentony_kits_runtime opentony_movie_source opentony_platform ZLIB::ZLIB)
target_compile_options(opentony_standalone PRIVATE ${OT_WARNINGS})
set_target_properties(opentony_standalone PROPERTIES PREFIX "" OUTPUT_NAME opentony_standalone CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
# its own definitions first (the spike defines some of the same names, e.g. arena::base()), and no undefined symbol left for run time
target_link_options(opentony_standalone PRIVATE -Wl,-Bsymbolic -Wl,--no-undefined)

# ---- opentony_spike_video.so: the window, input, sound and the draw path's GPU backend (SDL3, dlopen()ed) ----
add_library(opentony_spike_video SHARED
    "${KITS}/standalone_video.cpp"
    "${KITS}/standalone_png.cpp"
    "${KITS}/standalone_texture_dump.cpp"
    "${HAL}/video.cpp"
    "${HAL}/keymap.cpp"
    "${HAL}/sdl/sdl_loader.cpp"
    "${HAL}/sdl/sdl_window.cpp"
    "${HAL}/sdl/sdl_input.cpp"
    "${HAL}/sdl/sdl_audio.cpp"
    "${HAL}/audio.cpp"
    "${HAL}/movie_video.cpp"
    "${HAL}/sdl/sdl_video_gl.cpp"
    "${HAL}/sdl/sdl_video_gpu.cpp"
    "${HAL}/oracle/video_gpu.cpp"
)
target_include_directories(opentony_spike_video PRIVATE "${KITS}" "${OPENTONY_ROOT}/src/hooks" "${OPENTONY_ROOT}/src" "${HAL}")
target_include_directories(opentony_spike_video SYSTEM PRIVATE "${OPENTONY_SDL3_INCLUDE}")
target_compile_options(opentony_spike_video PRIVATE ${OT_WARNINGS})
target_link_libraries(opentony_spike_video PRIVATE opentony_platform ZLIB::ZLIB)
set_target_properties(opentony_spike_video PROPERTIES PREFIX "" OUTPUT_NAME opentony_spike_video CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_options(opentony_spike_video PRIVATE -Wl,-Bsymbolic -Wl,--no-undefined)

# ---- opentony_spike: the program that holds the guest arena and runs the standalone ----
add_executable(opentony_spike "${KITS}/standalone_spike.cpp" "${OPENTONY_ROOT}/src/hooks/oracle_script.cpp")
target_include_directories(opentony_spike PRIVATE "${KITS}" "${OPENTONY_ROOT}/src/hooks")
target_compile_options(opentony_spike PRIVATE ${OT_WARNINGS})
target_link_libraries(opentony_spike PRIVATE opentony_platform Threads::Threads)
add_dependencies(opentony_spike opentony_standalone opentony_spike_video)


# ---- opentony: the one-time reads of the player's files, then the spike ----
add_executable(opentony "${OPENTONY_ROOT}/src/launcher/opentony_launcher.cpp" "${OPENTONY_ROOT}/src/assets/pkr_asset.cpp")
target_include_directories(opentony PRIVATE "${OPENTONY_ROOT}/src")
target_compile_options(opentony PRIVATE ${OT_WARNINGS})
target_link_libraries(opentony PRIVATE opentony_platform ZLIB::ZLIB)
add_dependencies(opentony opentony_spike)

# ---- THPSource2: the player's launcher (docs/LAUNCHER.md), Dear ImGui on SDL3 ----
# The pinned Dear ImGui source (re/config/imgui.yml; scripts/linux/fetch-imgui.sh unpacks it into .tools/imgui/src). SDL3 is linked as usual here: the system's
# libSDL3.so.0 (the Windows launcher delay-loads SDL3.dll from Build\).
set(OPENTONY_IMGUI_SOURCE "${OPENTONY_ROOT}/.tools/imgui/src/imgui-1.92.9b" CACHE PATH "the pinned Dear ImGui source directory")
find_library(OPENTONY_SDL3_LIBRARY NAMES SDL3 libSDL3.so.0)
if(EXISTS "${OPENTONY_IMGUI_SOURCE}/imgui.h" AND OPENTONY_SDL3_LIBRARY)
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

    set(LAUNCHER "${OPENTONY_ROOT}/src/launcher/thpsource2")
    # resources 1 (the window icon) and 2 (the header picture), as launcher.rc links them into the Windows program
    set(LAUNCHER_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/launcher_resources.cpp")
    add_custom_command(OUTPUT "${LAUNCHER_RESOURCES}"
        COMMAND "${CMAKE_COMMAND}" -DOUTPUT=${LAUNCHER_RESOURCES} "-DRESOURCES=1=${LAUNCHER}/app/icon.png\;2=${LAUNCHER}/app/header.png"
                -P "${OPENTONY_ROOT}/cmake/embed_resources.cmake"
        DEPENDS "${LAUNCHER}/app/icon.png" "${LAUNCHER}/app/header.png" "${OPENTONY_ROOT}/cmake/embed_resources.cmake"
        COMMENT "launcher resources")
    add_executable(THPSource2
        "${LAUNCHER}/app/main.cpp"
        "${LAUNCHER}/app/platform_posix.cpp"
        "${LAUNCHER_RESOURCES}"
        "${LAUNCHER}/core/ini.cpp"
        "${LAUNCHER}/core/settings.cpp"
        "${LAUNCHER}/core/bindings.cpp"
        "${LAUNCHER}/core/iso9660.cpp"
        "${LAUNCHER}/core/game_files.cpp"
        "${LAUNCHER}/core/png.cpp"
        "${LAUNCHER}/core/true_colour.cpp"
        "${LAUNCHER}/core/upscale.cpp"
        "${LAUNCHER}/core/upscale_setup.cpp"
        "${LAUNCHER}/core/trick_sets.cpp"
        "${LAUNCHER}/core/update_check.cpp"
        "${OPENTONY_ROOT}/src/runtime/trick_database.cpp"
        "${OPENTONY_ROOT}/src/runtime/trick_combo_tables.cpp"
        "${LAUNCHER}/app/upscale_command.cpp"
        "${LAUNCHER}/app/upscale_task.cpp"
        "${LAUNCHER}/app/update_task.cpp"
        "${HAL}/keymap.cpp"
    )
    target_include_directories(THPSource2 PRIVATE "${OPENTONY_ROOT}/src" "${LAUNCHER}/core" "${LAUNCHER}/app")
    target_compile_options(THPSource2 PRIVATE ${OT_WARNINGS})
    target_link_libraries(THPSource2 PRIVATE opentony_imgui "${OPENTONY_SDL3_LIBRARY}" ZLIB::ZLIB Threads::Threads)
    add_dependencies(THPSource2 opentony)
else()
    message(STATUS "THPSource2 (the launcher) is not built: run scripts/linux/fetch-imgui.sh for Dear ImGui (and have libSDL3.so.0)")
endif()

# ---- the install folder (src/launcher/layout.hpp): THPSource2 in it, the game's programs in Build ----
#   cmake --install build/linux --prefix <install folder>
install(TARGETS opentony opentony_spike RUNTIME DESTINATION Build)
install(TARGETS opentony_standalone opentony_spike_video LIBRARY DESTINATION Build)
if(TARGET THPSource2)
    install(TARGETS THPSource2 RUNTIME DESTINATION .)
endif()
install(FILES "${OPENTONY_ROOT}/THIRD_PARTY_NOTICES.md" "${OPENTONY_ROOT}/LICENSE" DESTINATION Licenses)
