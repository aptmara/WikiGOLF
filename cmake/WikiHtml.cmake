include(FetchContent)
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LITEHTML_ENABLE_LINT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(litehtml
    GIT_REPOSITORY https://github.com/litehtml/litehtml.git
    GIT_TAG c287df656a8b29bc63a6fe8b05e463034f44f7f2
)
FetchContent_MakeAvailable(litehtml)
add_library(wiki_html STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/graphics/html/CourseHtml.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/graphics/html/CourseHtmlContainer.cpp)
target_include_directories(wiki_html PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src)
target_link_libraries(wiki_html PUBLIC litehtml)
if(MSVC)
    target_compile_options(wiki_html PRIVATE /utf-8)
endif()
add_executable(test_course_html ${CMAKE_CURRENT_LIST_DIR}/../test_course_html.cpp)
target_link_libraries(test_course_html PRIVATE wiki_html)
if(MSVC)
    target_compile_options(test_course_html PRIVATE /utf-8)
endif()
add_test(NAME course_html COMMAND test_course_html)
if(WIN32)
    add_executable(test_course_html_d3d
        ${CMAKE_CURRENT_LIST_DIR}/../test_course_html_d3d.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/graphics/WikiTextureGenerator.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/graphics/WikiTextureGeneratorHtml.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/graphics/WikiTextureGeneratorDecode.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/graphics/WikiTextureGeneratorLayout.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/graphics/WikiTextureGeneratorRendering.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/core/Logger.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/core/StringUtils.cpp)
    target_compile_definitions(test_course_html_d3d PRIVATE WIKIGOLF_HTML_COURSES=1
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(test_course_html_d3d PRIVATE /utf-8 /Zc:__cplusplus)
    target_link_libraries(test_course_html_d3d PRIVATE wiki_html d3d11 d2d1 dwrite dxguid windowscodecs ole32)
    add_test(NAME course_html_d3d COMMAND test_course_html_d3d)
endif()
