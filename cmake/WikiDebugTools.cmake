function(wikigolf_filter_debug_sources source_variable)
    if(NOT WIKIGOLF_DEBUG_TOOLS)
        set(filtered_sources "${${source_variable}}")
        list(FILTER filtered_sources EXCLUDE REGEX "/game/devtools/.*")
        set(${source_variable} "${filtered_sources}" PARENT_SCOPE)
    endif()
endfunction()

function(wikigolf_enable_debug_target target_name)
    if(NOT WIKIGOLF_DEBUG_TOOLS)
        return()
    endif()

    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(wikigolf_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    )
    target_include_directories(wikigolf_imgui PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
    )
    target_compile_definitions(${target_name} PRIVATE
        WIKIGOLF_DEBUG_TOOLS=1
        WIKIGOLF_PROFILING=1)
    target_link_libraries(${target_name} wikigolf_imgui)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${imgui_SOURCE_DIR}/LICENSE.txt"
                "$<TARGET_FILE_DIR:${target_name}>/licenses/Dear-ImGui-LICENSE.txt"
        VERBATIM
    )
endfunction()

function(wikigolf_configure_debug_test target_name)
    target_include_directories(${target_name} PRIVATE src)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8 /Zc:__cplusplus)
    endif()
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

function(wikigolf_add_debug_tests)
    add_executable(test_debug_build_config test_debug_build_config.cpp)
    if(WIKIGOLF_DEBUG_TOOLS)
        target_compile_definitions(test_debug_build_config PRIVATE
            WIKIGOLF_DEBUG_TOOLS=1
            WIKIGOLF_PROFILING=1)
    elseif(WIKIGOLF_PROFILING)
        target_compile_definitions(test_debug_build_config PRIVATE
            WIKIGOLF_PROFILING=1)
    endif()
    wikigolf_configure_debug_test(test_debug_build_config)

    foreach(test_name IN ITEMS debug_pause debug_frame_step debug_time_scale
                               debug_collision_info)
        add_executable(test_${test_name} test_${test_name}.cpp)
        wikigolf_configure_debug_test(test_${test_name})
    endforeach()

    add_executable(test_debug_input_capture test_debug_input_capture.cpp)
    target_compile_definitions(test_debug_input_capture PRIVATE
        -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    wikigolf_configure_debug_test(test_debug_input_capture)

    add_executable(test_debug_scene_rules test_debug_scene_rules.cpp)
    wikigolf_configure_debug_test(test_debug_scene_rules)

    add_executable(test_debug_gameplay_snapshot
        test_debug_gameplay_snapshot.cpp
        src/game/devtools/DebugGameplaySnapshot.cpp
        src/core/Logger.cpp)
    target_precompile_headers(test_debug_gameplay_snapshot PRIVATE src/pch.h)
    if(MSVC)
        target_compile_definitions(test_debug_gameplay_snapshot PRIVATE
            -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    endif()
    wikigolf_configure_debug_test(test_debug_gameplay_snapshot)

    add_executable(test_debug_slope_rules test_debug_slope_rules.cpp)
    wikigolf_configure_debug_test(test_debug_slope_rules)

    add_executable(test_debug_terrain_visibility
        test_debug_terrain_visibility.cpp
        src/game/devtools/DebugTerrainVisibility.cpp
        src/core/Logger.cpp)
    target_precompile_headers(test_debug_terrain_visibility PRIVATE src/pch.h)
    if(MSVC)
        target_compile_definitions(test_debug_terrain_visibility PRIVATE
            -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    endif()
    wikigolf_configure_debug_test(test_debug_terrain_visibility)

    add_executable(test_debug_cup_in_status
        test_debug_cup_in_status.cpp
        src/game/devtools/DebugCupInStatus.cpp
        src/core/Logger.cpp)
    target_precompile_headers(test_debug_cup_in_status PRIVATE src/pch.h)
    if(MSVC)
        target_compile_definitions(test_debug_cup_in_status PRIVATE
            -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    endif()
    wikigolf_configure_debug_test(test_debug_cup_in_status)

    add_executable(test_debug_ball_trail
        test_debug_ball_trail.cpp
        src/game/devtools/DebugBallTrailHistory.cpp)
    wikigolf_configure_debug_test(test_debug_ball_trail)

    add_executable(test_debug_ball_teleport
        test_debug_ball_teleport.cpp
        src/game/devtools/DebugBallTeleport.cpp
        src/core/Logger.cpp)
    target_precompile_headers(test_debug_ball_teleport PRIVATE src/pch.h)
    if(MSVC)
        target_compile_definitions(test_debug_ball_teleport PRIVATE
            -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    endif()
    wikigolf_configure_debug_test(test_debug_ball_teleport)

    add_executable(test_debug_collision_history
        test_debug_collision_history.cpp
        src/game/devtools/DebugCollisionHistory.cpp)
    wikigolf_configure_debug_test(test_debug_collision_history)

    if(WIKIGOLF_DEBUG_TOOLS)
        add_executable(test_debug_profiler_history
            test_debug_profiler_history.cpp
            src/game/devtools/DebugProfilerHistory.cpp)
        target_compile_definitions(test_debug_profiler_history PRIVATE
            WIKIGOLF_DEBUG_TOOLS=1
            WIKIGOLF_PROFILING=1)
        wikigolf_configure_debug_test(test_debug_profiler_history)
    endif()

    if(WIKIGOLF_DEBUG_TOOLS)
        add_executable(test_debug_japanese_font
            test_debug_japanese_font.cpp
            src/game/devtools/DebugFontLoader.cpp)
        target_compile_definitions(test_debug_japanese_font PRIVATE
            WIKIGOLF_TEST_FONT_PATH="${CMAKE_SOURCE_DIR}/Assets/Fonts/Mamelon-5-Hi-Regular.otf")
        target_link_libraries(test_debug_japanese_font PRIVATE wikigolf_imgui)
        wikigolf_configure_debug_test(test_debug_japanese_font)
    endif()

    add_executable(test_debug_collider_geometry
        test_debug_collider_geometry.cpp
        src/game/devtools/DebugColliderGeometry.cpp)
    wikigolf_configure_debug_test(test_debug_collider_geometry)

    add_executable(test_debug_logger_buffer test_debug_logger_buffer.cpp
                                            src/core/Logger.cpp)
    target_precompile_headers(test_debug_logger_buffer PRIVATE src/pch.h)
    target_compile_definitions(test_debug_logger_buffer PRIVATE WIKIGOLF_DEBUG_TOOLS=1)
    if(MSVC)
        target_compile_definitions(test_debug_logger_buffer PRIVATE
            -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    endif()
    wikigolf_configure_debug_test(test_debug_logger_buffer)
endfunction()
