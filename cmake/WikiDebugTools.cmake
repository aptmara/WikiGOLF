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
    target_compile_definitions(${target_name} PRIVATE WIKIGOLF_DEBUG_TOOLS=1)
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
        target_compile_definitions(test_debug_build_config PRIVATE WIKIGOLF_DEBUG_TOOLS=1)
    endif()
    wikigolf_configure_debug_test(test_debug_build_config)

    foreach(test_name IN ITEMS debug_pause debug_frame_step debug_time_scale)
        add_executable(test_${test_name} test_${test_name}.cpp)
        wikigolf_configure_debug_test(test_${test_name})
    endforeach()

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
