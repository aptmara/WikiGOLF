cmake_minimum_required(VERSION 3.20)
project(DX_GAME)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ライブラリ (.lib, .exp) の出力先を lib/ に分離
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Doxygen が利用可能な環境では、API ドキュメント生成用ターゲットを追加する。
# 通常のゲームビルドには含めず、必要なときだけ明示的に実行する。
find_package(Doxygen QUIET)
if(Doxygen_FOUND)
    add_custom_target(doxygen-docs
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_SOURCE_DIR}/docs/doxygen"
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating WikiGOLF API documentation..."
        VERBATIM
    )

    if(WIN32)
        find_program(XELATEX_EXECUTABLE
            NAMES xelatex
            HINTS "$ENV{LOCALAPPDATA}/Programs/MiKTeX/miktex/bin/x64"
        )
        if(XELATEX_EXECUTABLE)
            add_custom_target(doxygen-pdf
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${CMAKE_SOURCE_DIR}/output/pdf"
                COMMAND ${CMAKE_COMMAND} -E chdir
                        "${CMAKE_SOURCE_DIR}/docs/doxygen/latex"
                        cmd /c make.bat
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_SOURCE_DIR}/docs/doxygen/latex/refman.pdf"
                        "${CMAKE_SOURCE_DIR}/output/pdf/WikiGOLF-API-Reference.pdf"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                COMMENT "Generating WikiGOLF API documentation PDF..."
                VERBATIM
            )
            add_dependencies(doxygen-pdf doxygen-docs)
        else()
            message(STATUS "XeLaTeX was not found; the doxygen-pdf target is disabled.")
        endif()
    endif()
else()
    message(STATUS "Doxygen was not found; the doxygen-docs target is disabled.")
endif()

# テスト有効化
enable_testing()

option(WIKIGOLF_HTML_COURSES "Render course articles with litehtml" ON)
option(WIKIGOLF_HTML_TESTS_ONLY "Build portable HTML tests without DirectX" OFF)
option(WIKIGOLF_DEBUG_TOOLS "Build the dedicated Dear ImGui debug tools" OFF)
option(WIKIGOLF_PROFILING "Enable CPU/GPU profiling and report output" OFF)
include(cmake/WikiDebugTools.cmake)

if(CMAKE_BINARY_DIR MATCHES "static" OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "MultiThreaded$")
    set(WIKIGOLF_STATIC_BUILD_DEFAULT ON)
else()
    set(WIKIGOLF_STATIC_BUILD_DEFAULT OFF)
endif()
option(WIKIGOLF_STATIC_BUILD "Link static libraries instead of dynamic libraries" ${WIKIGOLF_STATIC_BUILD_DEFAULT})

if(WIKIGOLF_STATIC_BUILD)
    set(ASSIMP_LIBRARIES 
        "${CMAKE_SOURCE_DIR}/libs/assimp-6.0.2/static/assimp-static-mt.lib"
        "${CMAKE_SOURCE_DIR}/libs/assimp-6.0.2/static/zlibstatic.lib"
    )
else()
    set(ASSIMP_LIBRARIES assimp-vc143-mt)
endif()

if(WIKIGOLF_HTML_COURSES OR WIKIGOLF_HTML_TESTS_ONLY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    include(cmake/WikiHtml.cmake)
endif()
if(WIKIGOLF_HTML_TESTS_ONLY)
    return()
endif()

# ソースファイルの収集
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")
wikigolf_filter_debug_sources(SOURCES)

# ゲーム本体は Release/ 直下に出力
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
list(FILTER SOURCES EXCLUDE REGEX "/graphics/html/.*")
add_executable(DX_GAME WIN32 ${SOURCES} "DX_GAME.cpp")
wikigolf_enable_debug_target(DX_GAME)
if(WIKIGOLF_PROFILING)
    target_compile_definitions(DX_GAME PRIVATE WIKIGOLF_PROFILING=1)
endif()
set_target_properties(DX_GAME PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
if(WIKIGOLF_HTML_COURSES)
    target_link_libraries(DX_GAME wiki_html)
    target_compile_definitions(DX_GAME PRIVATE WIKIGOLF_HTML_COURSES=1)
    add_custom_command(TARGET DX_GAME POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DX_GAME>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${litehtml_SOURCE_DIR}/LICENSE"
                "$<TARGET_FILE_DIR:DX_GAME>/licenses/litehtml-LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${litehtml_SOURCE_DIR}/src/gumbo/LICENSE"
                "$<TARGET_FILE_DIR:DX_GAME>/licenses/gumbo-LICENSE"
        VERBATIM)

endif()

# サードパーティ素材・データのライセンス表記をビルド出力へ同梱
add_custom_command(TARGET DX_GAME POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DX_GAME>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/licenses/WIKIPEDIA-LICENSE"
            "$<TARGET_FILE_DIR:DX_GAME>/licenses/WIKIPEDIA-LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/licenses/Kenney-LICENSE"
            "$<TARGET_FILE_DIR:DX_GAME>/licenses/Kenney-LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/licenses/assimp-LICENSE"
            "$<TARGET_FILE_DIR:DX_GAME>/licenses/assimp-LICENSE"
    COMMENT "Deploying third-party license notices (Wikipedia/Kenney/assimp)..."
    VERBATIM)
target_precompile_headers(DX_GAME PRIVATE src/pch.h)

# データベースの分割ファイルを自動的に結合するターゲット
if(WIN32)
    add_custom_command(
        OUTPUT "${CMAKE_SOURCE_DIR}/Assets/data/jawiki_sdow-001.sqlite"
        COMMAND cmd /c copy /b jawiki_sdow-001_part1.sqlite+jawiki_sdow-001_part2.sqlite+jawiki_sdow-001_part3.sqlite jawiki_sdow-001.sqlite
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/Assets/data"
        DEPENDS "${CMAKE_SOURCE_DIR}/Assets/data/jawiki_sdow-001_part1.sqlite"
                "${CMAKE_SOURCE_DIR}/Assets/data/jawiki_sdow-001_part2.sqlite"
                "${CMAKE_SOURCE_DIR}/Assets/data/jawiki_sdow-001_part3.sqlite"
        COMMENT "Combining database part files into jawiki_sdow-001.sqlite..."
    )
    add_custom_target(combine_db ALL
        DEPENDS "${CMAKE_SOURCE_DIR}/Assets/data/jawiki_sdow-001.sqlite"
    )
    add_dependencies(DX_GAME combine_db)
endif()

# インクルードディレクトリの設定
target_include_directories(DX_GAME PRIVATE 
    src 
    src/thirdparty/sqlite3
    libs/assimp-6.0.2/include
)

# SQLite3 amalgamation（ソースとして直接コンパイル）
target_sources(DX_GAME PRIVATE src/thirdparty/sqlite3/sqlite3.c)

# Assimpライブラリディレクトリ
target_link_directories(DX_GAME PRIVATE libs/assimp-6.0.2 libs/assimp-6.0.2/static)

# リンクするライブラリの設定
target_link_libraries(DX_GAME 
    d3d11
    d2d1
    dwrite
    windowscodecs
    dxguid
    dinput8
    user32
    gdi32
    ole32
    shlwapi
    winmm
    mfplat
    mfreadwrite
    mfuuid
    ${ASSIMP_LIBRARIES}
)

# 文字セットをUnicodeに設定、C++20機能有効化、エンコーディング指定
if(MSVC)
    target_compile_definitions(DX_GAME PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(DX_GAME PRIVATE /utf-8 /Zc:__cplusplus)
endif()

# Post-build: Copy Assets
if(WIKIGOLF_STATIC_BUILD)
    add_custom_command(TARGET DX_GAME POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/Fonts $<TARGET_FILE_DIR:DX_GAME>/Assets/Fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/kenney_input-images_1.5 $<TARGET_FILE_DIR:DX_GAME>/Assets/kenney_input-images_1.5
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/models $<TARGET_FILE_DIR:DX_GAME>/Assets/models
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/shaders $<TARGET_FILE_DIR:DX_GAME>/Assets/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/sounds $<TARGET_FILE_DIR:DX_GAME>/Assets/sounds
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/textures $<TARGET_FILE_DIR:DX_GAME>/Assets/textures
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/ui $<TARGET_FILE_DIR:DX_GAME>/Assets/ui
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/videos $<TARGET_FILE_DIR:DX_GAME>/Assets/videos
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:DX_GAME>/Assets/data
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/Assets/data/jawiki_sdow-001.sqlite $<TARGET_FILE_DIR:DX_GAME>/Assets/data/jawiki_sdow-001.sqlite
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:DX_GAME>/Assets/data/jawiki_sdow-001_part1.sqlite"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:DX_GAME>/Assets/data/jawiki_sdow-001_part2.sqlite"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:DX_GAME>/Assets/data/jawiki_sdow-001_part3.sqlite"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:DX_GAME>/Assets/data/jawiki_sdow.sqlite"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:DX_GAME>/Assets/data/backup_old_db"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:DX_GAME>/DX_GAME.lib" "$<TARGET_FILE_DIR:DX_GAME>/DX_GAME.exp"
        COMMENT "Deploying filtered assets for static release..."
    )
else()
    add_custom_command(TARGET DX_GAME POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets $<TARGET_FILE_DIR:DX_GAME>/Assets
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:DX_GAME>/DX_GAME.lib" "$<TARGET_FILE_DIR:DX_GAME>/DX_GAME.exp"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/libs/assimp-6.0.2/assimp-vc143-mt.dll $<TARGET_FILE_DIR:DX_GAME>/assimp-vc143-mt.dll
        COMMENT "Deploying assets to output directory..."
    )
endif()

# テスト・開発ツール等の実行ファイルは tests/ に出力
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/SkyboxGen.cpp")
    add_executable(SkyboxGen tools/SkyboxGen.cpp src/graphics/SkyboxTextureGenerator.cpp)
    target_precompile_headers(SkyboxGen PRIVATE src/pch.h)
    target_include_directories(SkyboxGen PRIVATE src)
    target_link_libraries(SkyboxGen
        d3d11
        dxguid
        windowscodecs
    )
    if(MSVC)
        target_compile_definitions(SkyboxGen PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                         -DWIN32_LEAN_AND_MEAN)
        target_compile_options(SkyboxGen PRIVATE /utf-8 /Zc:__cplusplus)
    endif()

    add_custom_command(TARGET SkyboxGen POST_BUILD
        COMMAND SkyboxGen "${CMAKE_SOURCE_DIR}/Assets/textures/runtime_skybox"
        COMMENT "Generating Skybox textures..."
    )
else()
    message(WARNING "tools/SkyboxGen.cpp not found; skipping SkyboxGen target.")
endif()

# Wiki記事の画像float+wrapレイアウトを実データで確認するプレビューツール（手動実行用）
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/WikiImagePreview.cpp")
    add_executable(WikiImagePreview
        tools/WikiImagePreview.cpp
        src/graphics/WikiTextureGenerator.cpp
        src/graphics/WikiTextureGeneratorHtml.cpp
        src/graphics/WikiTextureGeneratorDecode.cpp
        src/graphics/WikiTextureGeneratorLayout.cpp
        src/graphics/WikiTextureGeneratorRendering.cpp
        src/graphics/Shader.cpp
        src/graphics/Mesh.cpp
        src/graphics/MeshPrimitives.cpp
        src/graphics/MeshPrimitivesGrass.cpp
        src/graphics/MeshPrimitivesTerrain.cpp
        src/graphics/TangentGenerator.cpp
        src/game/systems/WikiClient.cpp
        src/game/systems/WikiClientContent.cpp
        src/game/systems/WikiClientJson.cpp
        src/game/systems/WikiClientJsonHtml.cpp
        src/game/systems/WikiClientLinks.cpp
        src/core/Logger.cpp
        src/core/StringUtils.cpp
    )
    target_include_directories(WikiImagePreview PRIVATE src)
    target_link_libraries(WikiImagePreview
        d3d11
        d2d1
        dwrite
        dxguid
        windowscodecs
        winhttp
        d3dcompiler
    )
    if(MSVC)
        target_compile_definitions(WikiImagePreview PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                              -DWIN32_LEAN_AND_MEAN)
        target_compile_options(WikiImagePreview PRIVATE /utf-8 /Zc:__cplusplus)
    endif()
endif()

# シンプルなユニットテスト（CupIn判定）
add_executable(test_cupin_utils test_cupin_utils.cpp)
target_precompile_headers(test_cupin_utils PRIVATE src/pch.h)
target_include_directories(test_cupin_utils PRIVATE src)
if(MSVC)
    target_compile_definitions(test_cupin_utils PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                        -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_cupin_utils PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME cupin_utils COMMAND test_cupin_utils)

# ローディングシーンのフェード計算テスト
add_executable(test_loading_scene_fade test_loading_scene_fade.cpp)
target_precompile_headers(test_loading_scene_fade PRIVATE src/pch.h)
target_include_directories(test_loading_scene_fade PRIVATE src)
if(MSVC)
    target_compile_definitions(test_loading_scene_fade PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                               -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_loading_scene_fade PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME loading_scene_fade COMMAND test_loading_scene_fade)

# ローディングシーンの進捗合成テスト
add_executable(test_loading_scene_progress test_loading_scene_progress.cpp)
target_precompile_headers(test_loading_scene_progress PRIVATE src/pch.h)
target_include_directories(test_loading_scene_progress PRIVATE src)
if(MSVC)
    target_compile_definitions(test_loading_scene_progress PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                                   -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_loading_scene_progress PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME loading_scene_progress COMMAND test_loading_scene_progress)

# コース紹介の概要整形・注目ホール選定テスト
add_executable(test_course_introduction_rules test_course_introduction_rules.cpp)
target_precompile_headers(test_course_introduction_rules PRIVATE src/pch.h)
target_include_directories(test_course_introduction_rules PRIVATE src)
if(MSVC)
    target_compile_definitions(test_course_introduction_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                                       -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_course_introduction_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME course_introduction_rules COMMAND test_course_introduction_rules)

# 文字列トリミングテスト
add_executable(test_string_utils_trim test_string_utils_trim.cpp
                                        src/core/StringUtils.cpp)
target_precompile_headers(test_string_utils_trim PRIVATE src/pch.h)
target_include_directories(test_string_utils_trim PRIVATE src)
if(MSVC)
    target_compile_definitions(test_string_utils_trim PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                              -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_string_utils_trim PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME string_utils_trim COMMAND test_string_utils_trim)

# マップビューのナビゲーション純粋関数テスト
add_executable(test_map_view_utils test_map_view_utils.cpp)
target_precompile_headers(test_map_view_utils PRIVATE src/pch.h)
target_include_directories(test_map_view_utils PRIVATE src)
if(MSVC)
    target_compile_definitions(test_map_view_utils PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                              -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_map_view_utils PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME map_view_utils COMMAND test_map_view_utils)

# UIImage のテクスチャ有無チェック
add_executable(test_ui_image_has_texture test_ui_image_has_texture.cpp)
target_precompile_headers(test_ui_image_has_texture PRIVATE src/pch.h)
target_include_directories(test_ui_image_has_texture PRIVATE src)
if(MSVC)
    target_compile_definitions(test_ui_image_has_texture PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                                 -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_ui_image_has_texture PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME ui_image_has_texture COMMAND test_ui_image_has_texture)

add_executable(test_post_process_readability test_post_process_readability.cpp)
target_precompile_headers(test_post_process_readability PRIVATE src/pch.h)
target_include_directories(test_post_process_readability PRIVATE src)
if(MSVC)
    target_compile_definitions(test_post_process_readability PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                                    -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_post_process_readability PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME post_process_readability COMMAND test_post_process_readability)

# WikiTextureGeneratorのリンク生成テスト
add_executable(test_wiki_texture_links test_wiki_texture_links.cpp
                                       src/graphics/WikiTextureGenerator.cpp
                                       src/graphics/WikiTextureGeneratorHtml.cpp
                                       src/graphics/WikiTextureGeneratorDecode.cpp
                                       src/graphics/WikiTextureGeneratorLayout.cpp
                                       src/graphics/WikiTextureGeneratorRendering.cpp
                                       src/core/Logger.cpp
                                       src/core/StringUtils.cpp)
target_precompile_headers(test_wiki_texture_links PRIVATE src/pch.h)
target_include_directories(test_wiki_texture_links PRIVATE src)
target_link_libraries(test_wiki_texture_links
    d3d11
    d2d1
    dwrite
    dxguid
)
if(MSVC)
    target_compile_definitions(test_wiki_texture_links PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                                -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_wiki_texture_links PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME wiki_texture_links COMMAND test_wiki_texture_links)

add_executable(test_physics_friction test_physics_friction.cpp)
target_precompile_headers(test_physics_friction PRIVATE src/pch.h)
target_include_directories(test_physics_friction PRIVATE src)
if(MSVC)
    target_compile_definitions(test_physics_friction PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                              -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_physics_friction PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME physics_friction COMMAND test_physics_friction)

add_executable(test_trajectory_simulation test_trajectory_simulation.cpp)
target_precompile_headers(test_trajectory_simulation PRIVATE src/pch.h)
target_include_directories(test_trajectory_simulation PRIVATE src)
if(MSVC)
    target_compile_definitions(test_trajectory_simulation PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                              -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_trajectory_simulation PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME trajectory_simulation COMMAND test_trajectory_simulation)

add_executable(test_shot_gauge_rules test_shot_gauge_rules.cpp)
target_precompile_headers(test_shot_gauge_rules PRIVATE src/pch.h)
target_include_directories(test_shot_gauge_rules PRIVATE src)
if(MSVC)
    target_compile_definitions(test_shot_gauge_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                              -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_shot_gauge_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME shot_gauge_rules COMMAND test_shot_gauge_rules)

add_executable(test_par_rules test_par_rules.cpp)
target_precompile_headers(test_par_rules PRIVATE src/pch.h)
target_include_directories(test_par_rules PRIVATE src)
if(MSVC)
    target_compile_definitions(test_par_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                        -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_par_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME par_rules COMMAND test_par_rules)

add_executable(test_page_history_utils test_page_history_utils.cpp
                                       src/game/utils/PageHistoryUtils.cpp)
target_precompile_headers(test_page_history_utils PRIVATE src/pch.h)
target_include_directories(test_page_history_utils PRIVATE src)
if(MSVC)
    target_compile_definitions(test_page_history_utils PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                               -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_page_history_utils PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME page_history_utils COMMAND test_page_history_utils)

add_executable(test_terrain_generation test_terrain_generation.cpp
                                       src/game/systems/HtmlTerrainRules.cpp
                                       src/game/systems/TerrainGenerator.cpp
                                       src/game/systems/TerrainGeneratorMesh.cpp
                                       src/game/systems/TerrainGeneratorNoise.cpp
                                       src/game/systems/TerrainGeneratorPlatforms.cpp
                                       src/game/systems/TerrainGeneratorPostProcess.cpp
                                       src/graphics/TangentGenerator.cpp)
target_precompile_headers(test_terrain_generation PRIVATE src/pch.h)
target_include_directories(test_terrain_generation PRIVATE src)
if(MSVC)
    target_compile_definitions(test_terrain_generation PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                               -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_terrain_generation PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME terrain_generation COMMAND test_terrain_generation)

add_executable(test_grass_streaming_rules test_grass_streaming_rules.cpp)
target_precompile_headers(test_grass_streaming_rules PRIVATE src/pch.h)
target_include_directories(test_grass_streaming_rules PRIVATE src)
if(MSVC)
    target_compile_definitions(test_grass_streaming_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                               -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_grass_streaming_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME grass_streaming_rules COMMAND test_grass_streaming_rules)

add_executable(test_graphics_preset test_graphics_preset.cpp)
target_precompile_headers(test_graphics_preset PRIVATE src/pch.h)
target_include_directories(test_graphics_preset PRIVATE src)
if(MSVC)
    target_compile_definitions(test_graphics_preset PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                               -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_graphics_preset PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME graphics_preset COMMAND test_graphics_preset)

add_executable(test_logger_warning_output test_logger_warning_output.cpp
                                          src/core/Logger.cpp)
target_precompile_headers(test_logger_warning_output PRIVATE src/pch.h)
target_include_directories(test_logger_warning_output PRIVATE src)
if(MSVC)
    target_compile_definitions(test_logger_warning_output PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                                  -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_logger_warning_output PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME logger_warning_output COMMAND test_logger_warning_output)

# --- Additional Refactored Unit Tests ---

add_executable(test_wiki_texture_generation
    test_wiki_texture_generation.cpp
    src/graphics/WikiTextureGenerator.cpp
    src/graphics/WikiTextureGeneratorHtml.cpp
    src/graphics/WikiTextureGeneratorDecode.cpp
    src/graphics/WikiTextureGeneratorLayout.cpp
    src/graphics/WikiTextureGeneratorRendering.cpp
    src/core/Logger.cpp)
target_precompile_headers(test_wiki_texture_generation PRIVATE src/pch.h)
target_include_directories(test_wiki_texture_generation PRIVATE src)
target_link_libraries(test_wiki_texture_generation d3d11 d2d1 dwrite dxguid)
if(MSVC)
    target_compile_definitions(test_wiki_texture_generation PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_wiki_texture_generation PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME wiki_texture_generation COMMAND test_wiki_texture_generation)

add_executable(test_text_renderer_lifecycle
    test_text_renderer_lifecycle.cpp
    src/graphics/TextRenderer.cpp
    src/graphics/TextRendererImage.cpp
    src/graphics/TextRendererText.cpp
    src/graphics/TextRendererRaster.cpp
    src/core/Logger.cpp)
target_precompile_headers(test_text_renderer_lifecycle PRIVATE src/pch.h)
target_include_directories(test_text_renderer_lifecycle PRIVATE src)
target_link_libraries(test_text_renderer_lifecycle d3d11 d2d1 dwrite dxguid windowscodecs ole32)
if(MSVC)
    target_compile_definitions(test_text_renderer_lifecycle PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_text_renderer_lifecycle PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME text_renderer_lifecycle COMMAND test_text_renderer_lifecycle)

add_executable(test_resource_manager_rules
    test_resource_manager_rules.cpp
    src/resources/ResourceManager.cpp
    src/resources/ResourceManagerTextures.cpp
    src/resources/ResourceManagerMeshes.cpp
    src/resources/ResourceManagerShaders.cpp
    src/core/Logger.cpp
    src/core/StringUtils.cpp
    src/graphics/FbxLoader.cpp
    src/graphics/ObjLoader.cpp
    src/graphics/Mesh.cpp
    src/graphics/MeshPrimitives.cpp
    src/graphics/MeshPrimitivesGrass.cpp
    src/graphics/MeshPrimitivesTerrain.cpp
    src/graphics/TangentGenerator.cpp
    src/graphics/Shader.cpp)
target_precompile_headers(test_resource_manager_rules PRIVATE src/pch.h)
target_include_directories(test_resource_manager_rules PRIVATE src libs/assimp-6.0.2/include)
target_link_directories(test_resource_manager_rules PRIVATE libs/assimp-6.0.2 libs/assimp-6.0.2/static)
target_link_libraries(test_resource_manager_rules d3d11 dxguid windowscodecs d3dcompiler ${ASSIMP_LIBRARIES})
if(MSVC)
    target_compile_definitions(test_resource_manager_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_resource_manager_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME resource_manager_rules COMMAND test_resource_manager_rules)

add_executable(test_mesh_primitives_rules
    test_mesh_primitives_rules.cpp
    src/graphics/Mesh.cpp
    src/graphics/MeshPrimitives.cpp
    src/graphics/MeshPrimitivesGrass.cpp
    src/graphics/MeshPrimitivesTerrain.cpp
    src/graphics/TangentGenerator.cpp)
target_precompile_headers(test_mesh_primitives_rules PRIVATE src/pch.h)
target_include_directories(test_mesh_primitives_rules PRIVATE src)
if(MSVC)
    target_compile_definitions(test_mesh_primitives_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_mesh_primitives_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME mesh_primitives_rules COMMAND test_mesh_primitives_rules)

add_executable(test_terrain_layout_rules
    test_terrain_layout_rules.cpp
    src/game/systems/TerrainLayoutRules.cpp
    src/game/systems/TerrainObstacleLayout.cpp)
target_precompile_headers(test_terrain_layout_rules PRIVATE src/pch.h)
target_include_directories(test_terrain_layout_rules PRIVATE src)
if(MSVC)
    target_compile_definitions(test_terrain_layout_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_terrain_layout_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME terrain_layout_rules COMMAND test_terrain_layout_rules)

add_executable(test_scene_entity_lifecycle
    test_scene_entity_lifecycle.cpp
    src/core/Scene.cpp
    src/ecs/EntityOwner.cpp
    src/core/Logger.cpp)
target_precompile_headers(test_scene_entity_lifecycle PRIVATE src/pch.h)
target_include_directories(test_scene_entity_lifecycle PRIVATE src)
if(MSVC)
    target_compile_definitions(test_scene_entity_lifecycle PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_scene_entity_lifecycle PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME scene_entity_lifecycle COMMAND test_scene_entity_lifecycle)

add_executable(test_wiki_golf_hud_lifecycle
    test_wiki_golf_hud_lifecycle.cpp
    src/game/controllers/WikiGolfHUD.cpp
    src/game/controllers/hud/AimDistancePanel.cpp
    src/game/controllers/hud/ClubSelectionPanel.cpp
    src/game/controllers/hud/CourseInfoPanel.cpp
    src/game/controllers/hud/GameplayControlsPanel.cpp
    src/game/controllers/hud/HudStyles.cpp
    src/game/controllers/hud/LiePanel.cpp
    src/game/controllers/hud/MinimapDecorationPanel.cpp
    src/game/controllers/hud/ShotGaugePanel.cpp
    src/game/controllers/hud/ShotGaugePanelUpdate.cpp
    src/game/controllers/hud/ShotGaugePanelVisibility.cpp
    src/game/controllers/hud/WindPanel.cpp
    src/core/StringUtils.cpp
    src/ecs/EntityOwner.cpp
    src/core/Logger.cpp)
target_precompile_headers(test_wiki_golf_hud_lifecycle PRIVATE src/pch.h)
target_include_directories(test_wiki_golf_hud_lifecycle PRIVATE src)
if(MSVC)
    target_compile_definitions(test_wiki_golf_hud_lifecycle PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_wiki_golf_hud_lifecycle PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME wiki_golf_hud_lifecycle COMMAND test_wiki_golf_hud_lifecycle)

add_executable(test_wiki_page_layout
    test_wiki_page_layout.cpp
    src/game/scenes/AsyncPathEvaluator.cpp
    src/game/scenes/HolePlacementPlanner.cpp
    src/game/scenes/HoleVisualRules.cpp
    src/game/scenes/PageLinkSelector.cpp
    src/game/scenes/TutorialCourseLayout.cpp
    src/game/systems/WikiShortestPath.cpp
    src/game/systems/WikiShortestPathSearch.cpp
    src/game/systems/WikiShortestPathDistances.cpp
    src/game/systems/WikiShortestPathPopular.cpp
    src/core/Logger.cpp
    src/core/StringUtils.cpp
    src/thirdparty/sqlite3/sqlite3.c)
target_precompile_headers(test_wiki_page_layout PRIVATE src/pch.h)
target_include_directories(test_wiki_page_layout PRIVATE src src/thirdparty/sqlite3)
if(MSVC)
    target_compile_definitions(test_wiki_page_layout PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_wiki_page_layout PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME wiki_page_layout COMMAND test_wiki_page_layout)

add_executable(test_wiki_client_json
    test_wiki_client_json.cpp
    src/game/systems/WikiClientJson.cpp
    src/game/systems/WikiClientJsonHtml.cpp
    src/core/Logger.cpp
    src/core/StringUtils.cpp)
target_precompile_headers(test_wiki_client_json PRIVATE src/pch.h)
target_include_directories(test_wiki_client_json PRIVATE src)
if(MSVC)
    target_compile_definitions(test_wiki_client_json PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_wiki_client_json PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME wiki_client_json COMMAND test_wiki_client_json)

add_library(game_juice_time_impl STATIC
    src/game/systems/GameJuiceSystem.cpp
    src/game/systems/GameJuiceSystemCelebration.cpp
    src/game/systems/GameJuiceSystemEnvironment.cpp
    src/game/systems/GameJuiceSystemEnvironmentUpdate.cpp
    src/game/systems/GameJuiceSystemImpact.cpp
    src/game/systems/GameJuiceSystemSurface.cpp
    src/game/systems/GameJuiceSystemTrail.cpp
    src/ecs/EntityOwner.cpp
    src/core/Logger.cpp)
target_include_directories(game_juice_time_impl PRIVATE src)
if(MSVC)
    target_compile_definitions(game_juice_time_impl PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(game_juice_time_impl PRIVATE /utf-8 /Zc:__cplusplus)
endif()

add_executable(test_game_juice_time test_game_juice_time.cpp)
target_precompile_headers(test_game_juice_time PRIVATE src/pch.h)
target_include_directories(test_game_juice_time PRIVATE src)
if(MSVC)
    target_compile_definitions(test_game_juice_time PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_game_juice_time PRIVATE /utf-8 /Zc:__cplusplus)
endif()
target_link_libraries(test_game_juice_time d3d11 d2d1 dwrite windowscodecs dxguid dinput8 user32 gdi32 ole32 shlwapi winmm game_juice_time_impl)
add_test(NAME game_juice_time COMMAND test_game_juice_time)

add_executable(test_physics_system_rules
    test_physics_system_rules.cpp
    src/game/systems/PhysicsSystemCollision.cpp
    src/game/systems/PhysicsSystemSpatial.cpp)
target_precompile_headers(test_physics_system_rules PRIVATE src/pch.h)
target_include_directories(test_physics_system_rules PRIVATE src)
if(MSVC)
    target_compile_definitions(test_physics_system_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_physics_system_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME physics_system_rules COMMAND test_physics_system_rules)

add_executable(test_graphics_device_settings test_graphics_device_settings.cpp)
target_precompile_headers(test_graphics_device_settings PRIVATE src/pch.h)
target_include_directories(test_graphics_device_settings PRIVATE src)
if(MSVC)
    target_compile_definitions(test_graphics_device_settings PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_graphics_device_settings PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME graphics_device_settings COMMAND test_graphics_device_settings)

add_executable(test_display_settings_rules
    test_display_settings_rules.cpp
    src/core/DisplaySettings.cpp
    src/core/DisplaySettingsPersistence.cpp
    src/core/DisplaySettingsQuality.cpp
    src/core/DisplaySettingsWindow.cpp
    src/core/Logger.cpp
    src/core/StringUtils.cpp
    src/graphics/GraphicsDevice.cpp
    src/graphics/GraphicsDeviceAdapter.cpp
    src/graphics/GraphicsDeviceState.cpp
    src/graphics/GraphicsDeviceTargets.cpp
    src/graphics/Shader.cpp)
target_precompile_headers(test_display_settings_rules PRIVATE src/pch.h)
target_include_directories(test_display_settings_rules PRIVATE src)
if(MSVC)
    target_compile_definitions(test_display_settings_rules PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_display_settings_rules PRIVATE /utf-8 /Zc:__cplusplus)
endif()
target_link_libraries(test_display_settings_rules d3d11 d2d1 dwrite windowscodecs dxguid dinput8 user32 gdi32 ole32 shlwapi winmm)
add_test(NAME display_settings_rules COMMAND test_display_settings_rules)

add_executable(test_wiki_terrain_height
    test_wiki_terrain_height.cpp
    src/game/systems/WikiTerrainHeight.cpp)
target_precompile_headers(test_wiki_terrain_height PRIVATE src/pch.h)
target_include_directories(test_wiki_terrain_height PRIVATE src)
if(MSVC)
    target_compile_definitions(test_wiki_terrain_height PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_wiki_terrain_height PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME wiki_terrain_height COMMAND test_wiki_terrain_height)

wikigolf_add_debug_tests()
