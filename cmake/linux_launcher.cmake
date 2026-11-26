set(OPENTONY_IMGUI_SOURCE "" CACHE PATH "Pinned Dear ImGui source directory")
if(NOT EXISTS "${OPENTONY_IMGUI_SOURCE}/imgui.h")
    message(FATAL_ERROR "Set OPENTONY_IMGUI_SOURCE to the result of: python3 -c 'from tony.imgui_source import source_dir; print(source_dir())'")
endif()

set(launcher "${PROJECT_SOURCE_DIR}/src/launcher/thpsource2")
set(imgui "${OPENTONY_IMGUI_SOURCE}")
add_executable(THPSource2
    "${launcher}/app/main.cpp"
    "${launcher}/app/platform_linux.cpp"
    "${launcher}/app/update_task.cpp"
    "${launcher}/app/upscale_command.cpp"
    "${launcher}/app/upscale_task.cpp"
    "${launcher}/core/bindings.cpp"
    "${launcher}/core/game_files.cpp"
    "${launcher}/core/ini.cpp"
    "${launcher}/core/iso9660.cpp"
    "${launcher}/core/png.cpp"
    "${launcher}/core/settings.cpp"
    "${launcher}/core/trick_sets.cpp"
    "${launcher}/core/true_colour.cpp"
    "${launcher}/core/update_check.cpp"
    "${launcher}/core/upscale.cpp"
    "${launcher}/core/upscale_setup.cpp"
    "${imgui}/imgui.cpp"
    "${imgui}/imgui_draw.cpp"
    "${imgui}/imgui_tables.cpp"
    "${imgui}/imgui_widgets.cpp"
    "${imgui}/backends/imgui_impl_sdl3.cpp"
    "${imgui}/backends/imgui_impl_sdlrenderer3.cpp"
)
find_package(Threads REQUIRED)
target_include_directories(THPSource2 PRIVATE src "${launcher}/app" "${launcher}/core" "${imgui}" "${imgui}/backends")
target_compile_definitions(THPSource2 PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS OPENTONY_CHECKOUT="${PROJECT_SOURCE_DIR}")
target_link_libraries(THPSource2 PRIVATE opentony_runtime opentony_hal SDL3::SDL3 Threads::Threads)
configure_file("${launcher}/app/header.png" "${CMAKE_CURRENT_BINARY_DIR}/thpsource2-header.png" COPYONLY)
add_dependencies(THPSource2 opentony_linux_host)
