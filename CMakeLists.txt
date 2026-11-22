cmake_minimum_required(VERSION 3.20)

project(OpenTonyNative LANGUAGES C CXX)

find_package(ZLIB REQUIRED)

include(cmake/opentony_sources.cmake)

option(OPENTONY_BUILD_TESTS "Build native reconstruction tests" ON)

add_library(opentony_assets STATIC ${OPENTONY_ASSETS_SOURCES})
target_compile_features(opentony_assets PUBLIC cxx_std_20)
target_include_directories(opentony_assets PUBLIC src/assets)
target_compile_options(opentony_assets PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(opentony_assets PUBLIC ZLIB::ZLIB)

add_library(opentony_trg STATIC ${OPENTONY_TRG_SOURCES})
target_compile_features(opentony_trg PUBLIC cxx_std_20)
target_include_directories(opentony_trg PUBLIC src/trg)
target_compile_options(opentony_trg PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(opentony_trg PUBLIC opentony_assets)

add_library(opentony_runtime STATIC ${OPENTONY_RUNTIME_SOURCES})
target_compile_features(opentony_runtime PUBLIC cxx_std_20)
target_include_directories(opentony_runtime PUBLIC src/runtime)
target_compile_options(opentony_runtime PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(opentony_runtime PUBLIC opentony_trg)

add_executable(opentony_native_replay
    src/runtime/native_replay_main.cpp
)
target_link_libraries(opentony_native_replay PRIVATE opentony_runtime)
target_compile_features(opentony_native_replay PRIVATE cxx_std_20)
target_compile_options(opentony_native_replay PRIVATE -Wall -Wextra -Wpedantic)

# Portable part of the platform HAL (src/hal/, docs/HAL.md). The backends and
# hook adapters are Win32-only and built by src/hooks/CMakeLists.txt.
add_library(opentony_hal STATIC
    src/hal/hal.cpp
    src/hal/clock_pacer.cpp
    src/hal/keymap.cpp
    src/hal/video.cpp
    src/hal/movie_video.cpp
    src/hal/audio.cpp
    src/hal/oracle/d3d7_stream.cpp
    src/hal/oracle/ddraw_surface.cpp
    src/hal/oracle/video_gpu.cpp
)
target_compile_features(opentony_hal PUBLIC cxx_std_20)
target_include_directories(opentony_hal PUBLIC src/hal)
target_compile_options(opentony_hal PRIVATE -Wall -Wextra -Wpedantic)

if(OPENTONY_BUILD_TESTS)
    enable_testing()
    # The retail fixtures (re/evidence/fixtures/) hold the retail game's memory: they are captured from the person's own copy and are not in the
    # public repository (re/evidence/fixtures/README.md). A test that replays a fixture that is missing is added disabled, so ctest lists it as not run.
    function(ot_add_test)
        add_test(${ARGV})
        foreach(arg IN LISTS ARGV)
            if(arg MATCHES "/re/evidence/fixtures/" AND NOT EXISTS "${arg}")
                set_tests_properties("${ARGV1}" PROPERTIES DISABLED TRUE)
                break()
            endif()
        endforeach()
    endfunction()
    include_directories(${CMAKE_CURRENT_SOURCE_DIR})
    # The checkout root, for tests/test_assets.hpp: it finds the player's own game files under build/ from here.
    add_compile_definitions(OPENTONY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_executable(opentony_hal_test src/hal/hal_test.cpp)
    target_link_libraries(opentony_hal_test PRIVATE opentony_hal)
    target_compile_options(opentony_hal_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_hal_test COMMAND opentony_hal_test)
    add_executable(opentony_texture_key_test src/hal/texture_key_test.cpp)
    target_link_libraries(opentony_texture_key_test PRIVATE opentony_hal)
    target_compile_options(opentony_texture_key_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_texture_key_test COMMAND opentony_texture_key_test)
    add_executable(opentony_hal_oracle_test src/hal/oracle/d3d7_stream_test.cpp)
    target_link_libraries(opentony_hal_oracle_test PRIVATE opentony_hal)
    target_compile_options(opentony_hal_oracle_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_hal_oracle_test COMMAND opentony_hal_oracle_test)
    add_executable(opentony_hal_ddraw_test src/hal/oracle/ddraw_surface_test.cpp)
    target_link_libraries(opentony_hal_ddraw_test PRIVATE opentony_hal)
    target_compile_options(opentony_hal_ddraw_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_hal_ddraw_test COMMAND opentony_hal_ddraw_test)
    add_executable(opentony_hal_video_gpu_test src/hal/oracle/video_gpu_test.cpp)
    target_link_libraries(opentony_hal_video_gpu_test PRIVATE opentony_hal)
    target_compile_options(opentony_hal_video_gpu_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_hal_video_gpu_test COMMAND opentony_hal_video_gpu_test)

    # Primitive-stream oracle: retail stream vs ordering-table adapter (shadow).
    add_executable(opentony_video_shadow_compare src/hal/oracle/shadow_compare.cpp)
    target_link_libraries(opentony_video_shadow_compare PRIVATE opentony_hal)
    target_compile_options(opentony_video_shadow_compare PRIVATE -Wall -Wextra -Wpedantic)

    # Every HAL header on its own as strict C99, with no platform headers.
    add_executable(opentony_hal_headers_test src/hal/hal_headers_test.c)
    set_target_properties(opentony_hal_headers_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(opentony_hal_headers_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    ot_add_test(NAME opentony_hal_headers_test COMMAND opentony_hal_headers_test)

    add_executable(opentony_physics_state_machine_test
        tests/physics_state_machine_test.cpp
        src/physics_state_machine.cpp
    )
    target_include_directories(opentony_physics_state_machine_test PRIVATE src)
    target_compile_features(opentony_physics_state_machine_test PRIVATE cxx_std_20)
    target_compile_options(opentony_physics_state_machine_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_physics_state_machine_test COMMAND opentony_physics_state_machine_test)

    # The THPSource2 launcher's portable core (docs/LAUNCHER.md): settings, the game's binding rules, disc images, the import.
    # OPENTONY_THAWK2_EXE / OPENTONY_DISC_IMAGE in the environment add the checks on the player's own THawk2.exe and disc image.
    add_executable(opentony_launcher_core_test
        src/launcher/thpsource2/core/launcher_core_test.cpp
        src/launcher/thpsource2/core/ini.cpp
        src/launcher/thpsource2/core/settings.cpp
        src/launcher/thpsource2/core/bindings.cpp
        src/launcher/thpsource2/core/iso9660.cpp
        src/launcher/thpsource2/core/game_files.cpp
        src/launcher/thpsource2/core/trick_sets.cpp
        src/launcher/thpsource2/core/update_check.cpp
        src/runtime/trick_database.cpp
        src/runtime/trick_combo_tables.cpp
    )
    target_include_directories(opentony_launcher_core_test PRIVATE src src/launcher/thpsource2/core)
    target_link_libraries(opentony_launcher_core_test PRIVATE opentony_hal)
    target_compile_features(opentony_launcher_core_test PRIVATE cxx_std_20)
    target_compile_options(opentony_launcher_core_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_launcher_core_test COMMAND opentony_launcher_core_test ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/TRICKS.BIN)

    # The launcher's texture upscaler pipeline and PNG codec (texture-upscaling plan T4), with a stub model: no GPU, no tool
    add_executable(opentony_upscale_test
        src/launcher/thpsource2/core/upscale_test.cpp
        src/launcher/thpsource2/core/upscale.cpp
        src/launcher/thpsource2/core/upscale_setup.cpp
        src/launcher/thpsource2/core/game_files.cpp
        src/launcher/thpsource2/core/iso9660.cpp
        src/launcher/thpsource2/core/ini.cpp
        src/launcher/thpsource2/core/png.cpp
        src/launcher/thpsource2/core/true_colour.cpp
    )
    target_include_directories(opentony_upscale_test PRIVATE src src/launcher/thpsource2/core)
    target_link_libraries(opentony_upscale_test PRIVATE ZLIB::ZLIB)
    target_compile_features(opentony_upscale_test PRIVATE cxx_std_20)
    target_compile_options(opentony_upscale_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_upscale_test COMMAND opentony_upscale_test)

    # The score file's recorder (docs/LAUNCHER.md, "Scores"): which finished runs are written and when, read back through the launcher's reader
    add_executable(opentony_score_recorder_test src/hooks/kits/score_recorder_test.cpp)
    target_compile_features(opentony_score_recorder_test PRIVATE cxx_std_20)
    target_compile_options(opentony_score_recorder_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_score_recorder_test COMMAND opentony_score_recorder_test)

    add_executable(opentony_frame_pacing_test src/runtime/frame_pacing_test.cpp)
    target_compile_features(opentony_frame_pacing_test PRIVATE cxx_std_20)
    target_compile_options(opentony_frame_pacing_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_frame_pacing_test COMMAND opentony_frame_pacing_test)

    add_executable(opentony_free_look_test src/runtime/free_look_test.cpp)
    target_compile_features(opentony_free_look_test PRIVATE cxx_std_20)
    target_compile_options(opentony_free_look_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_free_look_test COMMAND opentony_free_look_test)

    add_executable(opentony_host_options_test src/runtime/host_options_test.cpp)
    target_compile_features(opentony_host_options_test PRIVATE cxx_std_20)
    target_compile_options(opentony_host_options_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_host_options_test COMMAND opentony_host_options_test)

    add_executable(opentony_collision_reference_test
        re/evidence/collision_reference_test.cpp
    )
    target_include_directories(opentony_collision_reference_test PRIVATE re/evidence)
    target_compile_features(opentony_collision_reference_test PRIVATE cxx_std_20)
    target_compile_options(opentony_collision_reference_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_collision_reference_test COMMAND opentony_collision_reference_test)

    add_executable(opentony_psx_scene_test
        src/collision/psx_scene_test.cpp
    )
    target_include_directories(opentony_psx_scene_test PRIVATE src re/evidence)
    target_compile_features(opentony_psx_scene_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_scene_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_scene_test COMMAND opentony_psx_scene_test)

    add_executable(opentony_trg_test
        src/trg/trg_runtime_test.cpp
    )
    target_link_libraries(opentony_trg_test PRIVATE opentony_trg)
    target_compile_features(opentony_trg_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trg_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trg_test COMMAND opentony_trg_test)

    add_executable(opentony_object_script_test
        src/trg/object_script_test.cpp
    )
    target_link_libraries(opentony_object_script_test PRIVATE opentony_trg)
    target_compile_features(opentony_object_script_test PRIVATE cxx_std_20)
    target_compile_options(opentony_object_script_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_object_script_test COMMAND opentony_object_script_test)

    add_executable(opentony_world_objects_test
        src/trg/world_objects_test.cpp
    )
    target_link_libraries(opentony_world_objects_test PRIVATE opentony_trg)
    target_compile_features(opentony_world_objects_test PRIVATE cxx_std_20)
    target_compile_options(opentony_world_objects_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_world_objects_test COMMAND opentony_world_objects_test)

    add_executable(opentony_object_script_retail_trace_test
        src/trg/object_script_retail_trace_test.cpp
    )
    target_link_libraries(opentony_object_script_retail_trace_test PRIVATE opentony_trg)
    target_compile_features(opentony_object_script_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_object_script_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_object_script_retail_trace_test
        COMMAND opentony_object_script_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/object-script-warehouse.txt
            ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/SKWARE_T.TRG)
    set_tests_properties(opentony_object_script_retail_trace_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_retail_random_test
        src/trg/retail_random_test.cpp
    )
    target_link_libraries(opentony_retail_random_test PRIVATE opentony_trg)
    target_compile_features(opentony_retail_random_test PRIVATE cxx_std_20)
    target_compile_options(opentony_retail_random_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_retail_random_test COMMAND opentony_retail_random_test)

    add_executable(opentony_platform_object_test
        src/trg/platform_object_test.cpp
    )
    target_link_libraries(opentony_platform_object_test PRIVATE opentony_trg)
    target_compile_features(opentony_platform_object_test PRIVATE cxx_std_20)
    target_compile_options(opentony_platform_object_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_platform_object_test COMMAND opentony_platform_object_test)

    add_executable(opentony_platform_object_retail_trace_test
        src/trg/platform_object_retail_trace_test.cpp
    )
    target_link_libraries(opentony_platform_object_retail_trace_test PRIVATE opentony_trg)
    target_compile_features(opentony_platform_object_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_platform_object_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_platform_object_retail_trace_test
        COMMAND opentony_platform_object_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/platform-object-warehouse.txt
            ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/SKWARE_T.TRG)
    set_tests_properties(opentony_platform_object_retail_trace_test PROPERTIES SKIP_RETURN_CODE 77)
    ot_add_test(NAME opentony_platform_object_retail_trace_hangar_test
        COMMAND opentony_platform_object_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/platform-object-hangar.txt
            ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/SKHAN_T.TRG)
    set_tests_properties(opentony_platform_object_retail_trace_hangar_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_traffic_object_test
        src/trg/traffic_object_test.cpp
    )
    target_link_libraries(opentony_traffic_object_test PRIVATE opentony_trg)
    target_compile_features(opentony_traffic_object_test PRIVATE cxx_std_20)
    target_compile_options(opentony_traffic_object_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_traffic_object_test COMMAND opentony_traffic_object_test)

    add_executable(opentony_traffic_object_retail_trace_test
        src/trg/traffic_object_retail_trace_test.cpp
    )
    target_link_libraries(opentony_traffic_object_retail_trace_test PRIVATE opentony_trg)
    target_compile_features(opentony_traffic_object_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_traffic_object_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_traffic_object_retail_trace_test
        COMMAND opentony_traffic_object_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/traffic-object-ny.txt
            ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/SKNY_T.TRG)
    set_tests_properties(opentony_traffic_object_retail_trace_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_powerup_object_test
        src/trg/powerup_object_test.cpp
    )
    target_link_libraries(opentony_powerup_object_test PRIVATE opentony_trg)
    target_compile_features(opentony_powerup_object_test PRIVATE cxx_std_20)
    target_compile_options(opentony_powerup_object_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_powerup_object_test COMMAND opentony_powerup_object_test)

    add_executable(opentony_powerup_object_retail_trace_test
        src/trg/powerup_object_retail_trace_test.cpp
    )
    target_link_libraries(opentony_powerup_object_retail_trace_test PRIVATE opentony_trg)
    target_compile_features(opentony_powerup_object_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_powerup_object_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_powerup_object_retail_trace_test
        COMMAND opentony_powerup_object_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/powerup-object-hangar.txt
            ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/SKHAN_T.TRG)
    set_tests_properties(opentony_powerup_object_retail_trace_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_camera_point_registry_test
        src/trg/camera_point_registry_test.cpp
    )
    target_link_libraries(opentony_camera_point_registry_test PRIVATE opentony_trg)
    target_compile_features(opentony_camera_point_registry_test PRIVATE cxx_std_20)
    target_compile_options(opentony_camera_point_registry_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_camera_point_registry_test COMMAND opentony_camera_point_registry_test)

    add_executable(opentony_rail_runtime_test
        src/trg/rail_runtime_test.cpp
    )
    target_link_libraries(opentony_rail_runtime_test PRIVATE opentony_trg)
    target_compile_features(opentony_rail_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_rail_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_rail_runtime_test COMMAND opentony_rail_runtime_test)

    add_executable(opentony_powerup_runtime_test
        src/trg/powerup_runtime_test.cpp
    )
    target_link_libraries(opentony_powerup_runtime_test PRIVATE opentony_trg)
    target_compile_features(opentony_powerup_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_powerup_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_powerup_runtime_test COMMAND opentony_powerup_runtime_test)

    add_executable(opentony_traffic_runtime_test
        src/trg/traffic_runtime_test.cpp
    )
    target_link_libraries(opentony_traffic_runtime_test PRIVATE opentony_trg)
    target_compile_features(opentony_traffic_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_traffic_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_traffic_runtime_test COMMAND opentony_traffic_runtime_test)

    add_executable(opentony_trigger_factory_runtime_test
        src/trg/trigger_factory_runtime_test.cpp
    )
    target_link_libraries(opentony_trigger_factory_runtime_test PRIVATE opentony_trg)
    target_compile_features(opentony_trigger_factory_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trigger_factory_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trigger_factory_runtime_test COMMAND opentony_trigger_factory_runtime_test)

    add_executable(opentony_trg_state_test
        src/trg/level_trigger_state_test.cpp
    )
    target_link_libraries(opentony_trg_state_test PRIVATE opentony_trg)
    target_compile_features(opentony_trg_state_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trg_state_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trg_state_test COMMAND opentony_trg_state_test)

    add_executable(opentony_level_runtime_test
        src/trg/level_runtime_test.cpp
    )
    target_link_libraries(opentony_level_runtime_test PRIVATE opentony_runtime)
    target_compile_features(opentony_level_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_level_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_level_runtime_test COMMAND opentony_level_runtime_test)

    add_executable(opentony_level_world_test
        src/trg/level_world_test.cpp
    )
    target_link_libraries(opentony_level_world_test PRIVATE opentony_runtime)
    target_compile_features(opentony_level_world_test PRIVATE cxx_std_20)
    target_compile_options(opentony_level_world_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_level_world_test
        COMMAND opentony_level_world_test ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data)
    set_tests_properties(opentony_level_world_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_level_render_snapshot_test
        src/trg/level_render_snapshot_test.cpp
    )
    target_link_libraries(opentony_level_render_snapshot_test PRIVATE opentony_trg)
    target_compile_features(opentony_level_render_snapshot_test PRIVATE cxx_std_20)
    target_compile_options(opentony_level_render_snapshot_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_level_render_snapshot_test COMMAND opentony_level_render_snapshot_test)

    add_executable(opentony_render_packet_builder_test
        src/trg/render_packet_builder_test.cpp
    )
    target_link_libraries(opentony_render_packet_builder_test PRIVATE opentony_trg)
    target_compile_features(opentony_render_packet_builder_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_packet_builder_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_render_packet_builder_test COMMAND opentony_render_packet_builder_test)

    add_executable(opentony_render_polygon_bucket_test
        src/trg/render_polygon_bucket_test.cpp
    )
    target_link_libraries(opentony_render_polygon_bucket_test PRIVATE opentony_trg)
    target_compile_features(opentony_render_polygon_bucket_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_polygon_bucket_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_render_polygon_bucket_test COMMAND opentony_render_polygon_bucket_test)

    add_executable(opentony_render_command_dispatch_test
        src/trg/render_command_dispatch_test.cpp
    )
    target_link_libraries(opentony_render_command_dispatch_test PRIVATE opentony_trg)
    target_compile_features(opentony_render_command_dispatch_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_command_dispatch_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_render_command_dispatch_test COMMAND opentony_render_command_dispatch_test)

    add_executable(opentony_render_model_submit_test
        src/trg/render_model_submit_test.cpp
    )
    target_link_libraries(opentony_render_model_submit_test PRIVATE opentony_trg)
    target_compile_features(opentony_render_model_submit_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_model_submit_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_render_model_submit_test COMMAND opentony_render_model_submit_test)

    add_executable(opentony_render_model_retail_trace_test
        src/trg/render_model_retail_trace_test.cpp
    )
    target_link_libraries(opentony_render_model_retail_trace_test PRIVATE opentony_trg)
    target_compile_features(opentony_render_model_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_model_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_render_model_retail_trace_warehouse_test
        COMMAND opentony_render_model_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-model-warehouse.txt)
    ot_add_test(NAME opentony_render_model_retail_trace_hangar_test
        COMMAND opentony_render_model_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-model-hangar.txt)

    add_executable(opentony_trg_interpreter_retail_trace_test
        src/trg/trg_interpreter_retail_trace_test.cpp
    )
    target_link_libraries(opentony_trg_interpreter_retail_trace_test PRIVATE opentony_trg)
    target_compile_features(opentony_trg_interpreter_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trg_interpreter_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trg_interpreter_retail_trace_hangar_test
        COMMAND opentony_trg_interpreter_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-interpreter-hangar.txt)
    ot_add_test(NAME opentony_trg_interpreter_retail_trace_warehouse_test
        COMMAND opentony_trg_interpreter_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-interpreter-warehouse.txt)
    ot_add_test(NAME opentony_trg_interpreter_retail_trace_levels_test
        COMMAND opentony_trg_interpreter_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-interpreter-levels.txt)
    ot_add_test(NAME opentony_trg_interpreter_retail_trace_mutation_test
        COMMAND opentony_trg_interpreter_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-interpreter-hangar.txt --mutate)
    set_tests_properties(opentony_trg_interpreter_retail_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    add_executable(opentony_render_order_retail_trace_test
        src/trg/render_order_retail_trace_test.cpp
    )
    target_link_libraries(opentony_render_order_retail_trace_test PRIVATE opentony_trg)
    target_compile_features(opentony_render_order_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_order_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_render_order_retail_trace_warehouse_test
        COMMAND opentony_render_order_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-order-warehouse.txt)
    ot_add_test(NAME opentony_render_order_retail_trace_mutation_test
        COMMAND opentony_render_order_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-order-warehouse.txt --mutate)
    set_tests_properties(opentony_render_order_retail_trace_mutation_test PROPERTIES WILL_FAIL TRUE)
    ot_add_test(NAME opentony_render_model_retail_trace_mutation_test
        COMMAND opentony_render_model_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-model-warehouse.txt --mutate)
    set_tests_properties(opentony_render_model_retail_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    add_executable(opentony_input_state_test
        src/runtime/input_state_test.cpp
    )
    target_link_libraries(opentony_input_state_test PRIVATE opentony_runtime)
    target_compile_features(opentony_input_state_test PRIVATE cxx_std_20)
    target_compile_options(opentony_input_state_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_input_state_test COMMAND opentony_input_state_test)

    add_executable(opentony_action_profile_test
        src/runtime/action_profile_test.cpp
    )
    target_link_libraries(opentony_action_profile_test PRIVATE opentony_runtime)
    target_compile_features(opentony_action_profile_test PRIVATE cxx_std_20)
    target_compile_options(opentony_action_profile_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_action_profile_test COMMAND opentony_action_profile_test)

    add_executable(opentony_input_bindings_test
        src/runtime/input_bindings_test.cpp
    )
    target_link_libraries(opentony_input_bindings_test PRIVATE opentony_runtime)
    target_compile_features(opentony_input_bindings_test PRIVATE cxx_std_20)
    target_compile_options(opentony_input_bindings_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_input_bindings_test COMMAND opentony_input_bindings_test)

    add_executable(opentony_gameplay_frame_test
        src/runtime/gameplay_frame_test.cpp
    )
    target_link_libraries(opentony_gameplay_frame_test PRIVATE opentony_runtime)
    target_compile_features(opentony_gameplay_frame_test PRIVATE cxx_std_20)
    target_compile_options(opentony_gameplay_frame_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_gameplay_frame_test COMMAND opentony_gameplay_frame_test)

    add_executable(opentony_player_frame_integration_test
        src/runtime/player_frame_integration_test.cpp
    )
    target_link_libraries(opentony_player_frame_integration_test PRIVATE opentony_runtime)
    target_compile_features(opentony_player_frame_integration_test PRIVATE cxx_std_20)
    target_compile_options(opentony_player_frame_integration_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_player_frame_integration_test COMMAND opentony_player_frame_integration_test)

    add_executable(opentony_fixed_step_driver_test
        src/runtime/fixed_step_driver_test.cpp
    )
    target_link_libraries(opentony_fixed_step_driver_test PRIVATE opentony_runtime)
    target_compile_features(opentony_fixed_step_driver_test PRIVATE cxx_std_20)
    target_compile_options(opentony_fixed_step_driver_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_fixed_step_driver_test COMMAND opentony_fixed_step_driver_test)

    add_executable(opentony_gameplay_session_test
        src/runtime/gameplay_session_test.cpp
    )
    target_link_libraries(opentony_gameplay_session_test PRIVATE opentony_runtime)
    target_compile_features(opentony_gameplay_session_test PRIVATE cxx_std_20)
    target_compile_options(opentony_gameplay_session_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_gameplay_session_test COMMAND opentony_gameplay_session_test)

    add_executable(opentony_level_event_owner_test
        src/runtime/level_event_owner_test.cpp
    )
    target_link_libraries(opentony_level_event_owner_test PRIVATE opentony_runtime)
    target_compile_features(opentony_level_event_owner_test PRIVATE cxx_std_20)
    target_compile_options(opentony_level_event_owner_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_level_event_owner_test COMMAND opentony_level_event_owner_test)

    add_executable(opentony_resource_kit_test
        src/runtime/resource_kit_test.cpp
    )
    target_link_libraries(opentony_resource_kit_test PRIVATE opentony_runtime)
    target_compile_features(opentony_resource_kit_test PRIVATE cxx_std_20)
    target_compile_options(opentony_resource_kit_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_resource_kit_test COMMAND opentony_resource_kit_test)

    add_executable(opentony_texture_kit_test
        src/runtime/texture_kit_test.cpp
    )
    target_link_libraries(opentony_texture_kit_test PRIVATE opentony_runtime)
    target_compile_features(opentony_texture_kit_test PRIVATE cxx_std_20)
    target_compile_options(opentony_texture_kit_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_texture_kit_test COMMAND opentony_texture_kit_test)

    add_executable(opentony_hal_texture_cache_test
        src/runtime/hal_texture_cache_test.cpp
    )
    target_link_libraries(opentony_hal_texture_cache_test PRIVATE opentony_runtime)
    target_compile_features(opentony_hal_texture_cache_test PRIVATE cxx_std_20)
    target_compile_options(opentony_hal_texture_cache_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_hal_texture_cache_test COMMAND opentony_hal_texture_cache_test)

    add_executable(opentony_psx_env_kit_test
        src/runtime/psx_env_kit_test.cpp
    )
    target_link_libraries(opentony_psx_env_kit_test PRIVATE opentony_runtime)
    target_compile_features(opentony_psx_env_kit_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_env_kit_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_env_kit_test COMMAND opentony_psx_env_kit_test)

    add_executable(opentony_draw_kit_test
        src/runtime/draw_kit_test.cpp
    )
    target_link_libraries(opentony_draw_kit_test PRIVATE opentony_runtime)
    target_compile_features(opentony_draw_kit_test PRIVATE cxx_std_20)
    target_compile_options(opentony_draw_kit_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_draw_kit_test COMMAND opentony_draw_kit_test)

    add_executable(opentony_render_model_kit_trace_test
        src/runtime/render_model_kit_trace_test.cpp
    )
    target_link_libraries(opentony_render_model_kit_trace_test PRIVATE opentony_runtime)
    target_compile_features(opentony_render_model_kit_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_model_kit_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_render_model_kit_trace_warehouse_test
        COMMAND opentony_render_model_kit_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-model-warehouse.txt)
    ot_add_test(NAME opentony_render_model_kit_trace_hangar_test
        COMMAND opentony_render_model_kit_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-model-hangar.txt)
    ot_add_test(NAME opentony_render_model_kit_trace_mutation_test
        COMMAND opentony_render_model_kit_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-model-warehouse.txt --mutate)
    set_tests_properties(opentony_render_model_kit_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    add_executable(opentony_world_trig_commands_test
        src/runtime/world_trig_commands_test.cpp
    )
    target_link_libraries(opentony_world_trig_commands_test PRIVATE opentony_runtime)
    target_compile_features(opentony_world_trig_commands_test PRIVATE cxx_std_20)
    target_compile_options(opentony_world_trig_commands_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_world_trig_commands_test COMMAND opentony_world_trig_commands_test)

    add_executable(opentony_world_trig_commands_trace_test
        src/runtime/world_trig_commands_trace_test.cpp
    )
    target_link_libraries(opentony_world_trig_commands_trace_test PRIVATE opentony_runtime)
    target_compile_features(opentony_world_trig_commands_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_world_trig_commands_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    foreach(trg_fixture hangar warehouse levels)
        ot_add_test(NAME opentony_world_trig_commands_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_commands_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-interpreter-${trg_fixture}.txt)
    endforeach()
    ot_add_test(NAME opentony_world_trig_commands_trace_mutation_test
        COMMAND opentony_world_trig_commands_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-interpreter-hangar.txt --mutate)
    set_tests_properties(opentony_world_trig_commands_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    add_executable(opentony_world_trig_nodes_trace_test
        src/runtime/world_trig_nodes_trace_test.cpp
    )
    target_link_libraries(opentony_world_trig_nodes_trace_test PRIVATE opentony_runtime)
    target_compile_features(opentony_world_trig_nodes_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_world_trig_nodes_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    foreach(trg_fixture hangar warehouse levels)
        ot_add_test(NAME opentony_world_trig_nodes_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-parse-nodes-${trg_fixture}.txt)
    endforeach()
    ot_add_test(NAME opentony_world_trig_nodes_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-parse-nodes-hangar.txt --mutate)
    set_tests_properties(opentony_world_trig_nodes_trace_mutation_test PROPERTIES WILL_FAIL TRUE)
    foreach(trg_fixture hangar warehouse levels)
        ot_add_test(NAME opentony_world_trig_create_object_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-create-object-${trg_fixture}.txt --create)
    endforeach()
    ot_add_test(NAME opentony_world_trig_create_object_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-create-object-hangar.txt --create --mutate)
    set_tests_properties(opentony_world_trig_create_object_trace_mutation_test PROPERTIES WILL_FAIL TRUE)
    foreach(trg_fixture hangar warehouse levels)
        ot_add_test(NAME opentony_world_object_lists_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/object-lists-${trg_fixture}.txt --lists)
    endforeach()
    ot_add_test(NAME opentony_world_object_lists_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/object-lists-hangar.txt --lists --mutate)
    set_tests_properties(opentony_world_object_lists_trace_mutation_test PROPERTIES WILL_FAIL TRUE)
    foreach(trg_fixture hangar warehouse levels)
        ot_add_test(NAME opentony_trick_table_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/tricktable-${trg_fixture}.txt --tricks)
    endforeach()
    ot_add_test(NAME opentony_trick_table_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/tricktable-hangar.txt --tricks --mutate)
    set_tests_properties(opentony_trick_table_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    foreach(trg_fixture hangar warehouse)
        ot_add_test(NAME opentony_spool_find_enviro_item_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/enviroitem-${trg_fixture}.txt --enviro)
    endforeach()
    ot_add_test(NAME opentony_spool_find_enviro_item_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/enviroitem-hangar.txt --enviro --mutate)
    set_tests_properties(opentony_spool_find_enviro_item_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    foreach(trg_fixture hangar warehouse)
        ot_add_test(NAME opentony_trg_construct_0192_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-construct-0192-${trg_fixture}.txt --construct)
    endforeach()
    ot_add_test(NAME opentony_trg_construct_0192_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-construct-0192-hangar.txt --construct --mutate)
    set_tests_properties(opentony_trg_construct_0192_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    ot_add_test(NAME opentony_trg_script_object_trace_levels_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-script-object-levels.txt --scriptobject)
    ot_add_test(NAME opentony_trg_script_object_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-script-object-levels.txt --scriptobject --mutate)
    set_tests_properties(opentony_trg_script_object_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    ot_add_test(NAME opentony_trg_traffic_trace_levels_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-traffic-levels.txt --traffic)
    ot_add_test(NAME opentony_trg_traffic_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-traffic-levels.txt --traffic --mutate)
    set_tests_properties(opentony_trg_traffic_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    ot_add_test(NAME opentony_trg_traffic_update_trace_levels_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-traffic-update-levels.txt --trafficupdate)
    ot_add_test(NAME opentony_trg_traffic_update_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-traffic-update-levels.txt --trafficupdate --mutate)
    set_tests_properties(opentony_trg_traffic_update_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    foreach(trg_fixture hangar warehouse levels)
        ot_add_test(NAME opentony_trg_powerup_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-powerup-${trg_fixture}.txt --powerup)
    endforeach()
    ot_add_test(NAME opentony_trg_powerup_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-powerup-hangar.txt --powerup --mutate)
    set_tests_properties(opentony_trg_powerup_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    foreach(trg_fixture hangar warehouse levels)
        ot_add_test(NAME opentony_trg_obai_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-obai-${trg_fixture}.txt --obai)
        ot_add_test(NAME opentony_trg_animstart_trace_${trg_fixture}_test
            COMMAND opentony_world_trig_nodes_trace_test
                ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-animstart-${trg_fixture}.txt --animstart)
    endforeach()
    ot_add_test(NAME opentony_trg_obai_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-obai-hangar.txt --obai --mutate)
    set_tests_properties(opentony_trg_obai_trace_mutation_test PROPERTIES WILL_FAIL TRUE)
    ot_add_test(NAME opentony_trg_animstart_trace_mutation_test
        COMMAND opentony_world_trig_nodes_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trg-animstart-hangar.txt --animstart --mutate)
    set_tests_properties(opentony_trg_animstart_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    add_executable(opentony_trick_tips_memory_test
        src/runtime/trick_tips_memory_test.cpp
    )
    target_link_libraries(opentony_trick_tips_memory_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_tips_memory_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_tips_memory_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_tips_memory_test COMMAND opentony_trick_tips_memory_test)
    ot_add_test(NAME opentony_trick_tips_memory_mutation_test COMMAND opentony_trick_tips_memory_test --mutate)
    set_tests_properties(opentony_trick_tips_memory_mutation_test PROPERTIES WILL_FAIL TRUE)

    add_executable(opentony_trick_script_size_trace_test
        src/runtime/trick_script_size_trace_test.cpp
    )
    target_link_libraries(opentony_trick_script_size_trace_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_script_size_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_script_size_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_script_size_trace_test
        COMMAND opentony_trick_script_size_trace_test ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trickscript-size-levels.txt)
    ot_add_test(NAME opentony_trick_script_size_trace_mutation_test
        COMMAND opentony_trick_script_size_trace_test ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trickscript-size-levels.txt --mutate)
    set_tests_properties(opentony_trick_script_size_trace_mutation_test PROPERTIES WILL_FAIL TRUE)

    add_executable(opentony_ground_brake_test
        src/runtime/ground_brake_test.cpp
    )
    target_link_libraries(opentony_ground_brake_test PRIVATE opentony_runtime)
    target_compile_features(opentony_ground_brake_test PRIVATE cxx_std_20)
    target_compile_options(opentony_ground_brake_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_ground_brake_test COMMAND opentony_ground_brake_test)

    add_executable(opentony_ground_physics_test
        src/runtime/ground_physics_test.cpp
    )
    target_link_libraries(opentony_ground_physics_test PRIVATE opentony_runtime)
    target_compile_features(opentony_ground_physics_test PRIVATE cxx_std_20)
    target_compile_options(opentony_ground_physics_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_ground_physics_test COMMAND opentony_ground_physics_test)

    add_executable(opentony_position_commit_test
        src/runtime/position_commit_test.cpp
    )
    target_link_libraries(opentony_position_commit_test PRIVATE opentony_runtime)
    target_compile_features(opentony_position_commit_test PRIVATE cxx_std_20)
    target_compile_options(opentony_position_commit_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_position_commit_test COMMAND opentony_position_commit_test)

    add_executable(opentony_player_state_test
        src/runtime/player_state_test.cpp
    )
    target_link_libraries(opentony_player_state_test PRIVATE opentony_runtime)
    target_compile_features(opentony_player_state_test PRIVATE cxx_std_20)
    target_compile_options(opentony_player_state_test PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(opentony_trick_script_test
        src/runtime/trick_script_test.cpp
    )
    target_link_libraries(opentony_trick_script_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_script_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_script_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_script_test COMMAND opentony_trick_script_test)

    add_executable(opentony_trick_retail_trace_test
        src/runtime/trick_retail_trace_test.cpp
    )
    target_link_libraries(opentony_trick_retail_trace_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_retail_trace_test
        COMMAND opentony_trick_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trick-warehouse.txt
            ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/TRICKS.BIN)
    set_tests_properties(opentony_trick_retail_trace_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_trick_database_test
        src/runtime/trick_database_test.cpp
    )
    target_link_libraries(opentony_trick_database_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_database_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_database_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_database_test COMMAND opentony_trick_database_test)

    add_executable(opentony_trick_database_retail_trace_test
        src/runtime/trick_database_retail_trace_test.cpp
    )
    target_link_libraries(opentony_trick_database_retail_trace_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_database_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_database_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_database_retail_trace_test
        COMMAND opentony_trick_database_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trickdb-warehouse.txt
            ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/TRICKS.BIN)
    set_tests_properties(opentony_trick_database_retail_trace_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_career_retail_calls_test
        src/runtime/career_retail_calls_test.cpp
    )
    target_link_libraries(opentony_career_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_career_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_career_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB CAREER_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/career-calls-*.txt)
    if(NOT CAREER_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(CAREER_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/career-calls-*.txt)
    endif()
    ot_add_test(NAME opentony_career_retail_calls_test
        COMMAND opentony_career_retail_calls_test ${CAREER_CALL_FIXTURES})

    add_executable(opentony_career_test
        src/runtime/career_test.cpp
    )
    target_link_libraries(opentony_career_test PRIVATE opentony_runtime)
    target_compile_features(opentony_career_test PRIVATE cxx_std_20)
    target_compile_options(opentony_career_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_career_test COMMAND opentony_career_test)

    add_executable(opentony_create_retail_calls_test
        src/runtime/create_retail_calls_test.cpp
    )
    target_link_libraries(opentony_create_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_create_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_create_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB CREATE_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/create-calls-*.txt)
    if(NOT CREATE_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(CREATE_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/create-calls-*.txt)
    endif()
    ot_add_test(NAME opentony_create_retail_calls_test
        COMMAND opentony_create_retail_calls_test ${CREATE_CALL_FIXTURES})

    add_executable(opentony_create_test
        src/runtime/create_test.cpp
    )
    target_link_libraries(opentony_create_test PRIVATE opentony_runtime)
    target_compile_features(opentony_create_test PRIVATE cxx_std_20)
    target_compile_options(opentony_create_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_create_test
        COMMAND opentony_create_test ${CMAKE_SOURCE_DIR}/build/assets/all-pkr/files/data/CRETEX.BIN)

    add_executable(opentony_frontend_retail_calls_test
        src/runtime/frontend_retail_calls_test.cpp
    )
    target_link_libraries(opentony_frontend_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_frontend_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_frontend_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB FRONTEND_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/frontend-calls-*.txt)
    if(NOT FRONTEND_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(FRONTEND_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/frontend-calls-*.txt)
    endif()
    ot_add_test(NAME opentony_frontend_retail_calls_test
        COMMAND opentony_frontend_retail_calls_test ${FRONTEND_CALL_FIXTURES})

    add_executable(opentony_frontend_test
        src/runtime/frontend_test.cpp
    )
    target_link_libraries(opentony_frontend_test PRIVATE opentony_runtime)
    target_compile_features(opentony_frontend_test PRIVATE cxx_std_20)
    target_compile_options(opentony_frontend_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_frontend_test COMMAND opentony_frontend_test)

    add_executable(opentony_hud_retail_calls_test
        src/runtime/hud_retail_calls_test.cpp
    )
    target_link_libraries(opentony_hud_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_hud_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_hud_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB HUD_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/hud-calls-*.txt)
    if(NOT HUD_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(HUD_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/hud-calls-*.txt)
    endif()
    ot_add_test(NAME opentony_hud_retail_calls_test
        COMMAND opentony_hud_retail_calls_test ${HUD_CALL_FIXTURES})

    add_executable(opentony_menu_retail_calls_test
        src/runtime/menu_retail_calls_test.cpp
    )
    target_link_libraries(opentony_menu_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_menu_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_menu_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB MENU_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/menu-calls-*.txt)
    if(NOT MENU_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(MENU_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/menu-calls-*.txt)
    endif()
    if(MENU_CALL_FIXTURES)
        ot_add_test(NAME opentony_menu_retail_calls_test
            COMMAND opentony_menu_retail_calls_test ${MENU_CALL_FIXTURES})
    endif()

    add_executable(opentony_skater_windows_retail_calls_test
        src/runtime/skater_windows_retail_calls_test.cpp
    )
    target_link_libraries(opentony_skater_windows_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_skater_windows_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_skater_windows_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB SKWIN_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/skwin-calls-*.txt)
    if(NOT SKWIN_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(SKWIN_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/skwin-calls-*.txt)
    endif()
    if(SKWIN_CALL_FIXTURES)
        ot_add_test(NAME opentony_skater_windows_retail_calls_test
            COMMAND opentony_skater_windows_retail_calls_test ${SKWIN_CALL_FIXTURES})
    endif()

    add_executable(opentony_create_screens_retail_calls_test
        src/runtime/create_screens_retail_calls_test.cpp
    )
    target_link_libraries(opentony_create_screens_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_create_screens_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_create_screens_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB CRSCREEN_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/crscreen-calls-*.txt)
    if(NOT CRSCREEN_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(CRSCREEN_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/crscreen-calls-*.txt)
    endif()
    if(CRSCREEN_CALL_FIXTURES)
        ot_add_test(NAME opentony_create_screens_retail_calls_test
            COMMAND opentony_create_screens_retail_calls_test ${CRSCREEN_CALL_FIXTURES})
    endif()

    add_executable(opentony_player_select_retail_calls_test
        src/runtime/player_select_retail_calls_test.cpp
    )
    target_link_libraries(opentony_player_select_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_player_select_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_player_select_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB PLSEL_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/plsel-calls-*.txt)
    if(NOT PLSEL_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(PLSEL_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/plsel-calls-*.txt)
    endif()
    if(PLSEL_CALL_FIXTURES)
        ot_add_test(NAME opentony_player_select_retail_calls_test
            COMMAND opentony_player_select_retail_calls_test ${PLSEL_CALL_FIXTURES})
    endif()

    add_executable(opentony_player_select_test
        src/runtime/player_select_test.cpp
    )
    target_link_libraries(opentony_player_select_test PRIVATE opentony_runtime)
    target_compile_features(opentony_player_select_test PRIVATE cxx_std_20)
    target_compile_options(opentony_player_select_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_player_select_test COMMAND opentony_player_select_test)

    add_executable(opentony_equip_select_retail_calls_test
        src/runtime/equip_select_retail_calls_test.cpp
    )
    target_link_libraries(opentony_equip_select_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_equip_select_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_equip_select_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB EQSEL_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/eqsel-calls-*.txt)
    if(NOT EQSEL_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(EQSEL_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/eqsel-calls-*.txt)
    endif()
    if(EQSEL_CALL_FIXTURES)
        ot_add_test(NAME opentony_equip_select_retail_calls_test
            COMMAND opentony_equip_select_retail_calls_test ${EQSEL_CALL_FIXTURES})
    endif()

    add_executable(opentony_equip_select_test
        src/runtime/equip_select_test.cpp
    )
    target_link_libraries(opentony_equip_select_test PRIVATE opentony_runtime)
    target_compile_features(opentony_equip_select_test PRIVATE cxx_std_20)
    target_compile_options(opentony_equip_select_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_equip_select_test COMMAND opentony_equip_select_test)

    add_executable(opentony_select_screens_retail_calls_test
        src/runtime/select_screens_retail_calls_test.cpp
    )
    target_link_libraries(opentony_select_screens_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_select_screens_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_select_screens_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB SELSCR_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/carsel-calls-*.txt
        ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/handi-calls-*.txt
        ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/multi-calls-*.txt
        ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/levsel-calls-*.txt)
    if(NOT SELSCR_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(SELSCR_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/carsel-calls-*.txt
        ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/handi-calls-*.txt
        ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/multi-calls-*.txt
        ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/levsel-calls-*.txt)
    endif()
    if(SELSCR_CALL_FIXTURES)
        ot_add_test(NAME opentony_select_screens_retail_calls_test
            COMMAND opentony_select_screens_retail_calls_test ${SELSCR_CALL_FIXTURES})
    endif()

    add_executable(opentony_options_retail_calls_test
        src/runtime/options_retail_calls_test.cpp
    )
    target_link_libraries(opentony_options_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_options_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_options_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB OPTIONS_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/opt-calls-*.txt)
    if(NOT OPTIONS_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(OPTIONS_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/opt-calls-*.txt)
    endif()
    if(OPTIONS_CALL_FIXTURES)
        ot_add_test(NAME opentony_options_retail_calls_test
            COMMAND opentony_options_retail_calls_test ${OPTIONS_CALL_FIXTURES})
    endif()

    add_executable(opentony_options_test
        src/runtime/options_test.cpp
    )
    target_link_libraries(opentony_options_test PRIVATE opentony_runtime)
    target_compile_features(opentony_options_test PRIVATE cxx_std_20)
    target_compile_options(opentony_options_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_options_test COMMAND opentony_options_test)

    add_executable(opentony_goals_retail_calls_test
        src/runtime/goals_retail_calls_test.cpp
    )
    target_link_libraries(opentony_goals_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_goals_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_goals_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB GOALS_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/goal-calls-*.txt)
    if(NOT GOALS_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(GOALS_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/goal-calls-*.txt)
    endif()
    if(GOALS_CALL_FIXTURES)
        ot_add_test(NAME opentony_goals_retail_calls_test
            COMMAND opentony_goals_retail_calls_test ${GOALS_CALL_FIXTURES})
    endif()

    add_executable(opentony_goals_test
        src/runtime/goals_test.cpp
    )
    target_link_libraries(opentony_goals_test PRIVATE opentony_runtime)
    target_compile_features(opentony_goals_test PRIVATE cxx_std_20)
    target_compile_options(opentony_goals_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_goals_test COMMAND opentony_goals_test)

    add_executable(opentony_front_retail_calls_test
        src/runtime/front_retail_calls_test.cpp
    )
    target_link_libraries(opentony_front_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_front_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_front_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB FRONT_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/front-calls-*.txt)
    if(NOT FRONT_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(FRONT_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/front-calls-*.txt)
    endif()
    if(FRONT_CALL_FIXTURES)
        ot_add_test(NAME opentony_front_retail_calls_test
            COMMAND opentony_front_retail_calls_test ${FRONT_CALL_FIXTURES})
    endif()

    add_executable(opentony_psx_spool_retail_calls_test
        src/runtime/psx_spool_retail_calls_test.cpp
    )
    target_link_libraries(opentony_psx_spool_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_psx_spool_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_spool_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB PSX_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/psx-calls-*.txt)
    if(NOT PSX_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(PSX_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/psx-calls-*.txt)
    endif()
    if(PSX_CALL_FIXTURES)
        ot_add_test(NAME opentony_psx_spool_retail_calls_test
            COMMAND opentony_psx_spool_retail_calls_test ${PSX_CALL_FIXTURES})
    endif()

    add_executable(opentony_player_spool_retail_calls_test
        src/runtime/player_spool_retail_calls_test.cpp
    )
    target_link_libraries(opentony_player_spool_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_player_spool_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_player_spool_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB PLAYER_SPOOL_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/pspool-calls-*.txt)
    if(NOT PLAYER_SPOOL_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(PLAYER_SPOOL_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/pspool-calls-*.txt)
    endif()
    if(PLAYER_SPOOL_CALL_FIXTURES)
        ot_add_test(NAME opentony_player_spool_retail_calls_test
            COMMAND opentony_player_spool_retail_calls_test ${PLAYER_SPOOL_CALL_FIXTURES})
    endif()

    add_executable(opentony_gen_objs_retail_calls_test
        src/runtime/gen_objs_retail_calls_test.cpp
    )
    target_link_libraries(opentony_gen_objs_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_gen_objs_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_gen_objs_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB GEN_OBJS_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/gen-calls-*.txt)
    if(NOT GEN_OBJS_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(GEN_OBJS_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/gen-calls-*.txt)
    endif()
    if(GEN_OBJS_CALL_FIXTURES)
        ot_add_test(NAME opentony_gen_objs_retail_calls_test
            COMMAND opentony_gen_objs_retail_calls_test ${GEN_OBJS_CALL_FIXTURES})
    endif()

    add_executable(opentony_level_gen_retail_calls_test
        src/runtime/level_gen_retail_calls_test.cpp
    )
    target_link_libraries(opentony_level_gen_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_level_gen_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_level_gen_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB LEVEL_GEN_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/levelgen-calls-*.txt)
    if(NOT LEVEL_GEN_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(LEVEL_GEN_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/levelgen-calls-*.txt)
    endif()
    if(LEVEL_GEN_CALL_FIXTURES)
        ot_add_test(NAME opentony_level_gen_retail_calls_test
            COMMAND opentony_level_gen_retail_calls_test ${LEVEL_GEN_CALL_FIXTURES})
    endif()

    add_executable(opentony_level_editor_retail_calls_test
        src/runtime/level_editor_retail_calls_test.cpp
    )
    target_link_libraries(opentony_level_editor_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_level_editor_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_level_editor_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB LEVEL_EDITOR_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/leveleditor-calls-*.txt)
    if(NOT LEVEL_EDITOR_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(LEVEL_EDITOR_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/leveleditor-calls-*.txt)
    endif()
    if(LEVEL_EDITOR_CALL_FIXTURES)
        ot_add_test(NAME opentony_level_editor_retail_calls_test
            COMMAND opentony_level_editor_retail_calls_test ${LEVEL_EDITOR_CALL_FIXTURES})
    endif()

    add_executable(opentony_platform_retail_calls_test
        src/runtime/platform_retail_calls_test.cpp
    )
    target_link_libraries(opentony_platform_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_platform_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_platform_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB PLATFORM_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/platform-calls-*.txt)
    if(NOT PLATFORM_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(PLATFORM_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/platform-calls-*.txt)
    endif()
    if(PLATFORM_CALL_FIXTURES)
        ot_add_test(NAME opentony_platform_retail_calls_test
            COMMAND opentony_platform_retail_calls_test ${PLATFORM_CALL_FIXTURES})
    endif()

    add_executable(opentony_audio_retail_calls_test
        src/runtime/audio_retail_calls_test.cpp
    )
    target_link_libraries(opentony_audio_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_audio_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_audio_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB AUDIO_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/audio-calls-*.txt)
    if(NOT AUDIO_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(AUDIO_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/audio-calls-*.txt)
    endif()
    if(AUDIO_CALL_FIXTURES)
        ot_add_test(NAME opentony_audio_retail_calls_test
            COMMAND opentony_audio_retail_calls_test ${AUDIO_CALL_FIXTURES})
    endif()

    add_executable(opentony_net_retail_calls_test
        src/runtime/net_retail_calls_test.cpp
    )
    target_link_libraries(opentony_net_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_net_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_net_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB NET_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/net-calls-*.txt)
    if(NOT NET_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(NET_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/net-calls-*.txt)
    endif()
    if(NET_CALL_FIXTURES)
        ot_add_test(NAME opentony_net_retail_calls_test
            COMMAND opentony_net_retail_calls_test ${NET_CALL_FIXTURES})
    endif()

    add_executable(opentony_math_retail_calls_test
        src/runtime/math_retail_calls_test.cpp
    )
    target_link_libraries(opentony_math_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_math_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_math_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB MATH_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/math-calls-*.txt)
    if(NOT MATH_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(MATH_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/math-calls-*.txt)
    endif()
    if(MATH_CALL_FIXTURES)
        ot_add_test(NAME opentony_math_retail_calls_test
            COMMAND opentony_math_retail_calls_test ${MATH_CALL_FIXTURES})
    endif()

    add_executable(opentony_core_retail_calls_test
        src/runtime/core_retail_calls_test.cpp
    )
    target_link_libraries(opentony_core_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_core_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_core_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB CORE_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/core-calls-*.txt)
    if(NOT CORE_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(CORE_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/core-calls-*.txt)
    endif()
    if(CORE_CALL_FIXTURES)
        ot_add_test(NAME opentony_core_retail_calls_test
            COMMAND opentony_core_retail_calls_test ${CORE_CALL_FIXTURES})
    endif()

    add_executable(opentony_rail_retail_calls_test
        src/runtime/rail_retail_calls_test.cpp
    )
    target_link_libraries(opentony_rail_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_rail_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_rail_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB RAIL_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/rail-calls-*.txt)
    if(NOT RAIL_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(RAIL_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/rail-calls-*.txt)
    endif()
    if(RAIL_CALL_FIXTURES)
        ot_add_test(NAME opentony_rail_retail_calls_test
            COMMAND opentony_rail_retail_calls_test ${RAIL_CALL_FIXTURES})
    endif()

    add_executable(opentony_skater_retail_calls_test
        src/runtime/skater_retail_calls_test.cpp
    )
    target_link_libraries(opentony_skater_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_skater_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_skater_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB SKATER_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/skater-calls-*.txt)
    if(NOT SKATER_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(SKATER_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/skater-calls-*.txt)
    endif()
    if(SKATER_CALL_FIXTURES)
        ot_add_test(NAME opentony_skater_retail_calls_test
            COMMAND opentony_skater_retail_calls_test ${SKATER_CALL_FIXTURES})
    endif()

    add_executable(opentony_m3d_retail_calls_test
        src/runtime/m3d_retail_calls_test.cpp
    )
    target_link_libraries(opentony_m3d_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_m3d_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_m3d_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB M3D_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/m3d-calls-*.txt)
    if(NOT M3D_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(M3D_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/m3d-calls-*.txt)
    endif()
    if(M3D_CALL_FIXTURES)
        ot_add_test(NAME opentony_m3d_retail_calls_test
            COMMAND opentony_m3d_retail_calls_test ${M3D_CALL_FIXTURES})
    endif()

    add_executable(opentony_video_retail_calls_test
        src/runtime/video_retail_calls_test.cpp
    )
    target_link_libraries(opentony_video_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_video_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_video_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB VIDEO_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/video-calls-*.txt)
    if(NOT VIDEO_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(VIDEO_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/video-calls-*.txt)
    endif()
    if(VIDEO_CALL_FIXTURES)
        ot_add_test(NAME opentony_video_retail_calls_test
            COMMAND opentony_video_retail_calls_test ${VIDEO_CALL_FIXTURES})
    endif()

    add_executable(opentony_camera_retail_calls_test
        src/runtime/camera_retail_calls_test.cpp
    )
    target_link_libraries(opentony_camera_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_camera_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_camera_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB CAMERA_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/camera-calls-*.txt)
    if(NOT CAMERA_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(CAMERA_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/camera-calls-*.txt)
    endif()
    if(CAMERA_CALL_FIXTURES)
        ot_add_test(NAME opentony_camera_retail_calls_test
            COMMAND opentony_camera_retail_calls_test ${CAMERA_CALL_FIXTURES})
    endif()

    # the free look's read sites on the retail camera states (free_look.hpp): the fixtures with follow, mode 2, smoothing and update cases
    add_executable(opentony_free_look_camera_test
        src/runtime/free_look_camera_test.cpp
    )
    target_link_libraries(opentony_free_look_camera_test PRIVATE opentony_runtime)
    target_compile_features(opentony_free_look_camera_test PRIVATE cxx_std_20)
    target_compile_options(opentony_free_look_camera_test PRIVATE -Wall -Wextra -Wpedantic)
    if(EXISTS ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/camera-calls-cmmA.txt AND EXISTS ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/camera-calls-cmmB.txt
       AND EXISTS ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/camera-calls-cmmD.txt)
        ot_add_test(NAME opentony_free_look_camera_test
            COMMAND opentony_free_look_camera_test ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/camera-calls-cmmA.txt
                    ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/camera-calls-cmmB.txt ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/camera-calls-cmmD.txt)
    endif()

    add_executable(opentony_world_retail_calls_test
        src/runtime/world_retail_calls_test.cpp
    )
    target_link_libraries(opentony_world_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_world_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_world_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB WORLD_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/world-calls-*.txt)
    if(NOT WORLD_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(WORLD_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/world-calls-*.txt)
    endif()
    if(WORLD_CALL_FIXTURES)
        ot_add_test(NAME opentony_world_retail_calls_test
            COMMAND opentony_world_retail_calls_test ${WORLD_CALL_FIXTURES})
    endif()

    add_executable(opentony_bit_retail_calls_test
        src/runtime/bit_retail_calls_test.cpp
    )
    target_link_libraries(opentony_bit_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_bit_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_bit_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB BIT_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/bit-calls-*.txt)
    if(NOT BIT_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(BIT_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/bit-calls-*.txt)
    endif()
    if(BIT_CALL_FIXTURES)
        ot_add_test(NAME opentony_bit_retail_calls_test
            COMMAND opentony_bit_retail_calls_test ${BIT_CALL_FIXTURES})
    endif()

    add_executable(opentony_render_display_test src/runtime/render_display_test.cpp)
    target_link_libraries(opentony_render_display_test PRIVATE opentony_runtime)
    target_compile_features(opentony_render_display_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_display_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_render_display_test COMMAND opentony_render_display_test)

    add_executable(opentony_render_retail_calls_test
        src/runtime/render_retail_calls_test.cpp
    )
    target_link_libraries(opentony_render_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_render_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_render_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB RENDER_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-calls-*.txt)
    if(NOT RENDER_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(RENDER_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/render-calls-*.txt)
    endif()
    if(RENDER_CALL_FIXTURES)
        ot_add_test(NAME opentony_render_retail_calls_test
            COMMAND opentony_render_retail_calls_test ${RENDER_CALL_FIXTURES})
    endif()

    add_executable(opentony_createobj_retail_calls_test
        src/runtime/createobj_retail_calls_test.cpp
    )
    target_link_libraries(opentony_createobj_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_createobj_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_createobj_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB CREATEOBJ_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/createobj-calls-*.txt)
    if(NOT CREATEOBJ_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(CREATEOBJ_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/createobj-calls-*.txt)
    endif()
    if(CREATEOBJ_CALL_FIXTURES)
        ot_add_test(NAME opentony_createobj_retail_calls_test
            COMMAND opentony_createobj_retail_calls_test ${CREATEOBJ_CALL_FIXTURES})
    endif()

    add_executable(opentony_heap_retail_calls_test
        src/runtime/heap_retail_calls_test.cpp
    )
    target_link_libraries(opentony_heap_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_heap_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_heap_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB HEAP_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/heap-calls-*.txt)
    if(NOT HEAP_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(HEAP_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/heap-calls-*.txt)
    endif()
    if(HEAP_CALL_FIXTURES)
        ot_add_test(NAME opentony_heap_retail_calls_test
            COMMAND opentony_heap_retail_calls_test ${HEAP_CALL_FIXTURES})
    endif()

    add_executable(opentony_mmu_retail_calls_test
        src/runtime/mmu_retail_calls_test.cpp
    )
    target_link_libraries(opentony_mmu_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_mmu_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_mmu_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB MMU_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/mmu-calls-*.txt)
    if(NOT MMU_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(MMU_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/mmu-calls-*.txt)
    endif()
    if(MMU_CALL_FIXTURES)
        ot_add_test(NAME opentony_mmu_retail_calls_test
            COMMAND opentony_mmu_retail_calls_test ${MMU_CALL_FIXTURES})
    endif()

    add_executable(opentony_mmu_screens_retail_calls_test
        src/runtime/mmu_screens_retail_calls_test.cpp
    )
    target_link_libraries(opentony_mmu_screens_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_mmu_screens_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_mmu_screens_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB MMU_SCREENS_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/mmu-screens-calls-*.txt)
    if(NOT MMU_SCREENS_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(MMU_SCREENS_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/mmu-screens-calls-*.txt)
    endif()
    if(MMU_SCREENS_CALL_FIXTURES)
        ot_add_test(NAME opentony_mmu_screens_retail_calls_test
            COMMAND opentony_mmu_screens_retail_calls_test ${MMU_SCREENS_CALL_FIXTURES})
    endif()

    add_executable(opentony_pc_platform_test
        src/runtime/pc_platform_test.cpp
    )
    target_link_libraries(opentony_pc_platform_test PRIVATE opentony_runtime)
    target_compile_features(opentony_pc_platform_test PRIVATE cxx_std_20)
    target_compile_options(opentony_pc_platform_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_pc_platform_test COMMAND opentony_pc_platform_test)

    add_executable(opentony_front_test
        src/runtime/front_test.cpp
    )
    target_link_libraries(opentony_front_test PRIVATE opentony_runtime)
    target_compile_features(opentony_front_test PRIVATE cxx_std_20)
    target_compile_options(opentony_front_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_front_test COMMAND opentony_front_test)

    add_executable(opentony_shell_test
        src/runtime/shell_test.cpp
    )
    target_link_libraries(opentony_shell_test PRIVATE opentony_runtime)
    target_compile_features(opentony_shell_test PRIVATE cxx_std_20)
    target_compile_options(opentony_shell_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_shell_test COMMAND opentony_shell_test)

    add_executable(opentony_select_screens_test
        src/runtime/select_screens_test.cpp
    )
    target_link_libraries(opentony_select_screens_test PRIVATE opentony_runtime)
    target_compile_features(opentony_select_screens_test PRIVATE cxx_std_20)
    target_compile_options(opentony_select_screens_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_select_screens_test COMMAND opentony_select_screens_test)

    add_executable(opentony_trick_screens_retail_calls_test
        src/runtime/trick_screens_retail_calls_test.cpp
    )
    target_link_libraries(opentony_trick_screens_retail_calls_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_screens_retail_calls_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_screens_retail_calls_test PRIVATE -Wall -Wextra -Wpedantic)
    file(GLOB TRKSCR_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trkscr-calls-*.txt)
    if(NOT TRKSCR_CALL_FIXTURES)  # none captured: the pattern names a missing fixture, so ot_add_test disables the test
        set(TRKSCR_CALL_FIXTURES ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/trkscr-calls-*.txt)
    endif()
    if(TRKSCR_CALL_FIXTURES)
        ot_add_test(NAME opentony_trick_screens_retail_calls_test
            COMMAND opentony_trick_screens_retail_calls_test ${TRKSCR_CALL_FIXTURES})
    endif()

    add_executable(opentony_trick_screens_test
        src/runtime/trick_screens_test.cpp
    )
    target_link_libraries(opentony_trick_screens_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_screens_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_screens_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_screens_test COMMAND opentony_trick_screens_test)

    add_executable(opentony_create_screens_test
        src/runtime/create_screens_test.cpp
    )
    target_link_libraries(opentony_create_screens_test PRIVATE opentony_runtime)
    target_compile_features(opentony_create_screens_test PRIVATE cxx_std_20)
    target_compile_options(opentony_create_screens_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_create_screens_test COMMAND opentony_create_screens_test)

    add_executable(opentony_skater_windows_test
        src/runtime/skater_windows_test.cpp
    )
    target_link_libraries(opentony_skater_windows_test PRIVATE opentony_runtime)
    target_compile_features(opentony_skater_windows_test PRIVATE cxx_std_20)
    target_compile_options(opentony_skater_windows_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_skater_windows_test COMMAND opentony_skater_windows_test)

    add_executable(opentony_menu_test
        src/runtime/menu_test.cpp
    )
    target_link_libraries(opentony_menu_test PRIVATE opentony_runtime)
    target_compile_features(opentony_menu_test PRIVATE cxx_std_20)
    target_compile_options(opentony_menu_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_menu_test COMMAND opentony_menu_test)

    add_executable(opentony_hud_test
        src/runtime/hud_test.cpp
    )
    target_link_libraries(opentony_hud_test PRIVATE opentony_runtime)
    target_compile_features(opentony_hud_test PRIVATE cxx_std_20)
    target_compile_options(opentony_hud_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_hud_test COMMAND opentony_hud_test)

    add_executable(opentony_trick_menu_test
        src/runtime/trick_menu_test.cpp
    )
    target_link_libraries(opentony_trick_menu_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_menu_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_menu_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_menu_test COMMAND opentony_trick_menu_test)

    add_executable(opentony_trick_tips_test
        src/runtime/trick_tips_test.cpp
    )
    target_link_libraries(opentony_trick_tips_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_tips_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_tips_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_tips_test COMMAND opentony_trick_tips_test)

    add_executable(opentony_trick_tips_retail_trace_test
        src/runtime/trick_tips_retail_trace_test.cpp
    )
    target_link_libraries(opentony_trick_tips_retail_trace_test PRIVATE opentony_runtime)
    target_compile_features(opentony_trick_tips_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_trick_tips_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_trick_tips_retail_trace_test
        COMMAND opentony_trick_tips_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/tricktips-warehouse.txt)

    add_executable(opentony_action_commands_test
        src/runtime/action_commands_test.cpp
    )
    target_link_libraries(opentony_action_commands_test PRIVATE opentony_runtime)
    target_compile_features(opentony_action_commands_test PRIVATE cxx_std_20)
    target_compile_options(opentony_action_commands_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_action_commands_test COMMAND opentony_action_commands_test)
    ot_add_test(NAME opentony_player_state_test COMMAND opentony_player_state_test)

    add_executable(opentony_action_sequence_test
        src/runtime/action_sequence_test.cpp
    )
    target_link_libraries(opentony_action_sequence_test PRIVATE opentony_runtime)
    target_compile_features(opentony_action_sequence_test PRIVATE cxx_std_20)
    target_compile_options(opentony_action_sequence_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_action_sequence_test COMMAND opentony_action_sequence_test)

    add_executable(opentony_action_sequence_builder_test
        src/runtime/action_sequence_builder_test.cpp
    )
    target_link_libraries(opentony_action_sequence_builder_test PRIVATE opentony_runtime)
    target_compile_features(opentony_action_sequence_builder_test PRIVATE cxx_std_20)
    target_compile_options(opentony_action_sequence_builder_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_action_sequence_builder_test COMMAND opentony_action_sequence_builder_test)

    add_executable(opentony_ollie_test
        src/runtime/ollie_test.cpp
    )
    target_link_libraries(opentony_ollie_test PRIVATE opentony_runtime)
    target_compile_features(opentony_ollie_test PRIVATE cxx_std_20)
    target_compile_options(opentony_ollie_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_ollie_test COMMAND opentony_ollie_test)

    add_executable(opentony_velocity_damping_test
        src/runtime/velocity_damping_test.cpp
    )
    target_link_libraries(opentony_velocity_damping_test PRIVATE opentony_runtime)
    target_compile_features(opentony_velocity_damping_test PRIVATE cxx_std_20)
    target_compile_options(opentony_velocity_damping_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_velocity_damping_test COMMAND opentony_velocity_damping_test)

    add_executable(opentony_velocity_projection_test
        src/runtime/velocity_projection_test.cpp
    )
    target_link_libraries(opentony_velocity_projection_test PRIVATE opentony_runtime)
    target_compile_features(opentony_velocity_projection_test PRIVATE cxx_std_20)
    target_compile_options(opentony_velocity_projection_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_velocity_projection_test COMMAND opentony_velocity_projection_test)

    add_executable(opentony_air_motion_test
        src/runtime/air_motion_test.cpp
    )
    target_link_libraries(opentony_air_motion_test PRIVATE opentony_runtime)
    target_compile_features(opentony_air_motion_test PRIVATE cxx_std_20)
    target_compile_options(opentony_air_motion_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_air_motion_test COMMAND opentony_air_motion_test)

    add_executable(opentony_ground_turn_test
        src/runtime/ground_turn_test.cpp
    )
    target_link_libraries(opentony_ground_turn_test PRIVATE opentony_runtime)
    target_compile_features(opentony_ground_turn_test PRIVATE cxx_std_20)
    target_compile_options(opentony_ground_turn_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_ground_turn_test COMMAND opentony_ground_turn_test)

    add_executable(opentony_ground_motion_test
        src/runtime/ground_motion_test.cpp
    )
    target_link_libraries(opentony_ground_motion_test PRIVATE opentony_runtime)
    target_compile_features(opentony_ground_motion_test PRIVATE cxx_std_20)
    target_compile_options(opentony_ground_motion_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_ground_motion_test COMMAND opentony_ground_motion_test)

    add_executable(opentony_ground_animation_test
        src/runtime/ground_animation_test.cpp
    )
    target_link_libraries(opentony_ground_animation_test PRIVATE opentony_runtime)
    target_compile_features(opentony_ground_animation_test PRIVATE cxx_std_20)
    target_compile_options(opentony_ground_animation_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_ground_animation_test COMMAND opentony_ground_animation_test)

    add_executable(opentony_animation_cursor_test
        src/runtime/animation_cursor_test.cpp
    )
    target_link_libraries(opentony_animation_cursor_test PRIVATE opentony_runtime)
    target_compile_features(opentony_animation_cursor_test PRIVATE cxx_std_20)
    target_compile_options(opentony_animation_cursor_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_animation_cursor_test COMMAND opentony_animation_cursor_test)

    add_executable(opentony_ground_motion_threshold_test
        src/runtime/ground_motion_threshold_test.cpp
    )
    target_link_libraries(opentony_ground_motion_threshold_test PRIVATE opentony_runtime)
    target_compile_features(opentony_ground_motion_threshold_test PRIVATE cxx_std_20)
    target_compile_options(opentony_ground_motion_threshold_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_ground_motion_threshold_test COMMAND opentony_ground_motion_threshold_test)

    add_executable(opentony_fixed_matrix_test
        src/runtime/fixed_matrix_test.cpp
    )
    target_link_libraries(opentony_fixed_matrix_test PRIVATE opentony_runtime)
    target_compile_features(opentony_fixed_matrix_test PRIVATE cxx_std_20)
    target_compile_options(opentony_fixed_matrix_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_fixed_matrix_test COMMAND opentony_fixed_matrix_test)

    add_executable(opentony_retail_vector3_test
        src/runtime/retail_vector3_test.cpp
    )
    target_include_directories(opentony_retail_vector3_test PRIVATE src/runtime)
    target_compile_features(opentony_retail_vector3_test PRIVATE cxx_std_20)
    target_compile_options(opentony_retail_vector3_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_retail_vector3_test COMMAND opentony_retail_vector3_test)

    add_executable(opentony_mod_overlay_core_test
        src/hooks/mod_overlay_core_test.cpp
    )
    target_include_directories(opentony_mod_overlay_core_test PRIVATE src/hooks ${CMAKE_SOURCE_DIR})
    target_compile_features(opentony_mod_overlay_core_test PRIVATE cxx_std_20)
    target_compile_options(opentony_mod_overlay_core_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_mod_overlay_core_test COMMAND opentony_mod_overlay_core_test)

    add_executable(opentony_retail_live_test
        src/runtime/retail_live_test.cpp
    )
    target_link_libraries(opentony_retail_live_test PRIVATE opentony_runtime)
    target_include_directories(opentony_retail_live_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_features(opentony_retail_live_test PRIVATE cxx_std_20)
    target_compile_options(opentony_retail_live_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_retail_live_test COMMAND opentony_retail_live_test)

    add_executable(opentony_guest_heap_test
        src/runtime/guest_heap_test.cpp
    )
    target_link_libraries(opentony_guest_heap_test PRIVATE opentony_runtime)
    target_include_directories(opentony_guest_heap_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_features(opentony_guest_heap_test PRIVATE cxx_std_20)
    target_compile_options(opentony_guest_heap_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_guest_heap_test COMMAND opentony_guest_heap_test)

    add_executable(opentony_guest_format_test
        src/runtime/guest_format_test.cpp
    )
    target_link_libraries(opentony_guest_format_test PRIVATE opentony_runtime)
    target_include_directories(opentony_guest_format_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_features(opentony_guest_format_test PRIVATE cxx_std_20)
    target_compile_options(opentony_guest_format_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_guest_format_test COMMAND opentony_guest_format_test)

    add_executable(opentony_collision_recovery_test
        src/runtime/collision_recovery_test.cpp
    )
    target_link_libraries(opentony_collision_recovery_test PRIVATE opentony_runtime)
    target_compile_features(opentony_collision_recovery_test PRIVATE cxx_std_20)
    target_compile_options(opentony_collision_recovery_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_collision_recovery_test COMMAND opentony_collision_recovery_test)

    add_executable(opentony_physics_dispatch_test
        src/runtime/physics_dispatch_test.cpp
    )
    target_link_libraries(opentony_physics_dispatch_test PRIVATE opentony_runtime)
    target_compile_features(opentony_physics_dispatch_test PRIVATE cxx_std_20)
    target_compile_options(opentony_physics_dispatch_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_physics_dispatch_test COMMAND opentony_physics_dispatch_test)

    add_executable(opentony_physics_frame_test
        src/runtime/physics_frame_test.cpp
    )
    target_link_libraries(opentony_physics_frame_test PRIVATE opentony_runtime)
    target_compile_features(opentony_physics_frame_test PRIVATE cxx_std_20)
    target_compile_options(opentony_physics_frame_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_physics_frame_test COMMAND opentony_physics_frame_test)

    add_executable(opentony_psx_collision_probe_test
        src/runtime/psx_collision_probe_test.cpp
    )
    target_link_libraries(opentony_psx_collision_probe_test PRIVATE opentony_runtime)
    target_compile_features(opentony_psx_collision_probe_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_collision_probe_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_collision_probe_test COMMAND opentony_psx_collision_probe_test)

    add_executable(opentony_skater_runtime_test
        src/runtime/skater_runtime_test.cpp
    )
    target_link_libraries(opentony_skater_runtime_test PRIVATE opentony_runtime)
    target_compile_features(opentony_skater_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_skater_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_skater_runtime_test COMMAND opentony_skater_runtime_test)

    add_executable(opentony_skater_asset_runtime_test
        src/runtime/skater_asset_runtime_test.cpp
    )
    target_link_libraries(opentony_skater_asset_runtime_test PRIVATE opentony_runtime)
    target_compile_features(opentony_skater_asset_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_skater_asset_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_skater_asset_runtime_test COMMAND opentony_skater_asset_runtime_test)

    add_executable(opentony_player_resource_spool_test
        src/runtime/player_resource_spool_test.cpp
    )
    target_link_libraries(opentony_player_resource_spool_test PRIVATE opentony_runtime)
    target_compile_features(opentony_player_resource_spool_test PRIVATE cxx_std_20)
    target_compile_options(opentony_player_resource_spool_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_player_resource_spool_test COMMAND opentony_player_resource_spool_test)

    add_executable(opentony_camera_runtime_test
        src/camera/camera_runtime_test.cpp
    )
    target_link_libraries(opentony_camera_runtime_test PRIVATE opentony_runtime)
    target_compile_features(opentony_camera_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_camera_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_camera_runtime_test COMMAND opentony_camera_runtime_test)

    add_executable(opentony_camera_retail_trace_test
        src/camera/camera_retail_trace_test.cpp
    )
    target_compile_features(opentony_camera_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_camera_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    foreach(camera_scenario idle straight turn turn-ollie ollie-land)
        ot_add_test(NAME opentony_camera_retail_trace_test_${camera_scenario}
            COMMAND opentony_camera_retail_trace_test
                ${CMAKE_CURRENT_SOURCE_DIR}/re/evidence/fixtures/camera-update-warehouse-${camera_scenario}.txt)
    endforeach()

    add_executable(opentony_air_contact_test
        src/runtime/air_contact_test.cpp
    )
    target_link_libraries(opentony_air_contact_test PRIVATE opentony_runtime)
    target_compile_features(opentony_air_contact_test PRIVATE cxx_std_20)
    target_compile_options(opentony_air_contact_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_air_contact_test COMMAND opentony_air_contact_test)

    add_executable(opentony_psx_asset_test
        src/assets/psx_asset_test.cpp
    )
    target_link_libraries(opentony_psx_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_psx_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_asset_test COMMAND opentony_psx_asset_test)

    add_executable(opentony_psx_animation_test
        src/assets/psx_animation_test.cpp
    )
    target_link_libraries(opentony_psx_animation_test PRIVATE opentony_assets)
    target_compile_features(opentony_psx_animation_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_animation_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_animation_test COMMAND opentony_psx_animation_test)

    add_executable(opentony_psx_runtime_test
        src/assets/psx_runtime_test.cpp
    )
    target_link_libraries(opentony_psx_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_psx_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_runtime_test COMMAND opentony_psx_runtime_test)

    add_executable(opentony_psx_bits_runtime_test
        src/assets/psx_bits_runtime_test.cpp
    )
    target_link_libraries(opentony_psx_bits_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_psx_bits_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_bits_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_bits_runtime_test COMMAND opentony_psx_bits_runtime_test)

    add_executable(opentony_psx_animation_runtime_test
        src/assets/psx_animation_runtime_test.cpp
    )
    target_link_libraries(opentony_psx_animation_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_psx_animation_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_animation_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_animation_runtime_test COMMAND opentony_psx_animation_runtime_test)

    add_executable(opentony_sound_runtime_test
        src/assets/sound_runtime_test.cpp
    )
    target_link_libraries(opentony_sound_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_sound_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_sound_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_sound_runtime_test COMMAND opentony_sound_runtime_test)

    add_executable(opentony_wav_asset_test
        src/assets/wav_asset_test.cpp
    )
    target_link_libraries(opentony_wav_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_wav_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_wav_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_wav_asset_test COMMAND opentony_wav_asset_test)

    add_executable(opentony_pkr_asset_test
        src/assets/pkr_asset_test.cpp
    )
    target_link_libraries(opentony_pkr_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_pkr_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_pkr_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_pkr_asset_test COMMAND opentony_pkr_asset_test)

    add_executable(opentony_resource_runtime_test
        src/assets/resource_runtime_test.cpp
    )
    target_link_libraries(opentony_resource_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_resource_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_resource_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_resource_runtime_test COMMAND opentony_resource_runtime_test)

    add_executable(opentony_resource_retail_trace_test
        src/assets/resource_retail_trace_test.cpp
    )
    target_link_libraries(opentony_resource_retail_trace_test PRIVATE opentony_assets)
    target_compile_features(opentony_resource_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_resource_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_resource_retail_trace_warehouse_test
        COMMAND opentony_resource_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/fileio-warehouse.txt
            ${CMAKE_SOURCE_DIR}/build/disc/files/SETUP/data/ALL.PKR)
    set_tests_properties(opentony_resource_retail_trace_warehouse_test PROPERTIES SKIP_RETURN_CODE 77)
    ot_add_test(NAME opentony_resource_retail_trace_hangar_test
        COMMAND opentony_resource_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/fileio-hangar.txt
            ${CMAKE_SOURCE_DIR}/build/disc/files/SETUP/data/ALL.PKR)
    set_tests_properties(opentony_resource_retail_trace_hangar_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_fnt_retail_trace_test
        src/assets/fnt_retail_trace_test.cpp
    )
    target_link_libraries(opentony_fnt_retail_trace_test PRIVATE opentony_assets)
    target_compile_features(opentony_fnt_retail_trace_test PRIVATE cxx_std_20)
    target_compile_options(opentony_fnt_retail_trace_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_fnt_retail_trace_warehouse_test
        COMMAND opentony_fnt_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/fnt-warehouse.txt
            ${CMAKE_SOURCE_DIR}/build/disc/files/SETUP/data/ALL.PKR)
    set_tests_properties(opentony_fnt_retail_trace_warehouse_test PROPERTIES SKIP_RETURN_CODE 77)
    ot_add_test(NAME opentony_fnt_retail_trace_hangar_test
        COMMAND opentony_fnt_retail_trace_test
            ${CMAKE_SOURCE_DIR}/re/evidence/fixtures/fnt-hangar.txt
            ${CMAKE_SOURCE_DIR}/build/disc/files/SETUP/data/ALL.PKR)
    set_tests_properties(opentony_fnt_retail_trace_hangar_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(opentony_custom_park_asset_test
        src/assets/custom_park_asset_test.cpp
    )
    target_link_libraries(opentony_custom_park_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_custom_park_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_custom_park_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_custom_park_asset_test COMMAND opentony_custom_park_asset_test)

    add_executable(opentony_custom_park_runtime_test
        src/assets/custom_park_runtime_test.cpp
    )
    target_link_libraries(opentony_custom_park_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_custom_park_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_custom_park_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_custom_park_runtime_test COMMAND opentony_custom_park_runtime_test)

    add_executable(opentony_replay_asset_test
        src/assets/replay_asset_test.cpp
    )
    target_link_libraries(opentony_replay_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_replay_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_replay_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_replay_asset_test COMMAND opentony_replay_asset_test)

    add_executable(opentony_save_asset_test
        src/assets/save_asset_test.cpp
    )
    target_link_libraries(opentony_save_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_save_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_save_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_save_asset_test COMMAND opentony_save_asset_test)

    add_executable(opentony_save_runtime_test
        src/assets/save_runtime_test.cpp
    )
    target_link_libraries(opentony_save_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_save_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_save_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_save_runtime_test COMMAND opentony_save_runtime_test)

    add_executable(opentony_bmp_asset_test
        src/assets/bmp_asset_test.cpp
    )
    target_link_libraries(opentony_bmp_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_bmp_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_bmp_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_bmp_asset_test COMMAND opentony_bmp_asset_test)

    add_executable(opentony_pc_texture_runtime_test
        src/assets/pc_texture_runtime_test.cpp
    )
    target_link_libraries(opentony_pc_texture_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_pc_texture_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_pc_texture_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_pc_texture_runtime_test COMMAND opentony_pc_texture_runtime_test)

    add_executable(opentony_text_asset_test
        src/assets/text_asset_test.cpp
    )
    target_link_libraries(opentony_text_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_text_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_text_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_text_asset_test COMMAND opentony_text_asset_test)

    add_executable(opentony_psh_asset_test
        src/assets/psh_asset_test.cpp
    )
    target_link_libraries(opentony_psh_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_psh_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psh_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psh_asset_test COMMAND opentony_psh_asset_test)

    add_executable(opentony_fnt_asset_test
        src/assets/fnt_asset_test.cpp
    )
    target_link_libraries(opentony_fnt_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_fnt_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_fnt_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_fnt_asset_test COMMAND opentony_fnt_asset_test)

    add_executable(opentony_psx_texture_test
        src/assets/psx_texture_test.cpp
    )
    target_link_libraries(opentony_psx_texture_test PRIVATE opentony_assets)
    target_compile_features(opentony_psx_texture_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_texture_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_texture_test COMMAND opentony_psx_texture_test)

    add_executable(opentony_pre_asset_test
        src/assets/pre_asset_test.cpp
    )
    target_link_libraries(opentony_pre_asset_test PRIVATE opentony_assets)
    target_compile_features(opentony_pre_asset_test PRIVATE cxx_std_20)
    target_compile_options(opentony_pre_asset_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_pre_asset_test COMMAND opentony_pre_asset_test)

    add_executable(opentony_pre_runtime_test
        src/assets/pre_runtime_test.cpp
    )
    target_link_libraries(opentony_pre_runtime_test PRIVATE opentony_assets)
    target_compile_features(opentony_pre_runtime_test PRIVATE cxx_std_20)
    target_compile_options(opentony_pre_runtime_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_pre_runtime_test COMMAND opentony_pre_runtime_test)

    add_executable(opentony_pre_catalog_test
        src/assets/pre_catalog_test.cpp
    )
    target_link_libraries(opentony_pre_catalog_test PRIVATE opentony_assets)
    target_compile_features(opentony_pre_catalog_test PRIVATE cxx_std_20)
    target_compile_options(opentony_pre_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_pre_catalog_test COMMAND opentony_pre_catalog_test)

    add_executable(opentony_psx_catalog_test
        src/assets/psx_catalog_test.cpp
    )
    target_link_libraries(opentony_psx_catalog_test PRIVATE opentony_assets)
    target_compile_features(opentony_psx_catalog_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_catalog_test COMMAND opentony_psx_catalog_test)

    add_executable(opentony_psx_collision_test
        src/assets/psx_collision_test.cpp
    )
    target_link_libraries(opentony_psx_collision_test PRIVATE opentony_assets)
    target_compile_features(opentony_psx_collision_test PRIVATE cxx_std_20)
    target_compile_options(opentony_psx_collision_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_psx_collision_test COMMAND opentony_psx_collision_test)

    add_executable(opentony_tricks_bin_inspect
        src/assets/tricks_bin_inspect.cpp
    )
    target_link_libraries(opentony_tricks_bin_inspect PRIVATE opentony_assets)
    target_compile_features(opentony_tricks_bin_inspect PRIVATE cxx_std_20)
    target_compile_options(opentony_tricks_bin_inspect PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(opentony_tricks_bin_test
        src/assets/tricks_bin_test.cpp
    )
    target_link_libraries(opentony_tricks_bin_test PRIVATE opentony_assets)
    target_compile_features(opentony_tricks_bin_test PRIVATE cxx_std_20)
    target_compile_options(opentony_tricks_bin_test PRIVATE -Wall -Wextra -Wpedantic)
    ot_add_test(NAME opentony_tricks_bin_test COMMAND opentony_tricks_bin_test)

    # These tests read the player's own game files (tests/test_assets.hpp). When a file is not there they return 77 and ctest lists them as
    # skipped, not passed: a clean clone has none of the files, and "passed" would claim coverage that did not run.
    set_tests_properties(
        opentony_traffic_runtime_test
        opentony_level_runtime_test
        opentony_level_render_snapshot_test
        opentony_gameplay_session_test
        opentony_skater_asset_runtime_test
        opentony_gameplay_frame_test
        opentony_player_resource_spool_test
        opentony_fixed_step_driver_test
        opentony_pc_texture_runtime_test
        opentony_bmp_asset_test
        opentony_custom_park_asset_test
        opentony_pkr_asset_test
        opentony_psh_asset_test
        opentony_fnt_asset_test
        opentony_psx_texture_test
        opentony_text_asset_test
        opentony_replay_asset_test
        opentony_wav_asset_test
        opentony_psx_animation_test
        PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_executable(opentony_trg_inspect
    src/trg/trg_inspect.cpp
)
target_link_libraries(opentony_trg_inspect PRIVATE opentony_trg)
target_compile_features(opentony_trg_inspect PRIVATE cxx_std_20)
target_compile_options(opentony_trg_inspect PRIVATE -Wall -Wextra -Wpedantic)

add_executable(opentony_trg_scripts
    src/trg/trg_script_inspect.cpp
)
target_link_libraries(opentony_trg_scripts PRIVATE opentony_trg)
target_compile_features(opentony_trg_scripts PRIVATE cxx_std_20)
target_compile_options(opentony_trg_scripts PRIVATE -Wall -Wextra -Wpedantic)

add_executable(opentony_trg_scene_inspect
    src/trg/trg_scene_inspect.cpp
)
target_link_libraries(opentony_trg_scene_inspect PRIVATE opentony_trg)
target_compile_features(opentony_trg_scene_inspect PRIVATE cxx_std_20)
target_compile_options(opentony_trg_scene_inspect PRIVATE -Wall -Wextra -Wpedantic)

add_executable(opentony_psx_inspect
    src/assets/psx_inspect.cpp
)
target_link_libraries(opentony_psx_inspect PRIVATE opentony_assets)
target_compile_features(opentony_psx_inspect PRIVATE cxx_std_20)
target_compile_options(opentony_psx_inspect PRIVATE -Wall -Wextra -Wpedantic)

add_executable(opentony_psx_catalog_inspect
    src/assets/psx_catalog_inspect.cpp
)
target_link_libraries(opentony_psx_catalog_inspect PRIVATE opentony_assets)
target_compile_features(opentony_psx_catalog_inspect PRIVATE cxx_std_20)
target_compile_options(opentony_psx_catalog_inspect PRIVATE -Wall -Wextra -Wpedantic)

add_executable(opentony_pre_inspect
    src/assets/pre_inspect.cpp
)
target_link_libraries(opentony_pre_inspect PRIVATE opentony_assets)
target_compile_features(opentony_pre_inspect PRIVATE cxx_std_20)
target_compile_options(opentony_pre_inspect PRIVATE -Wall -Wextra -Wpedantic)
