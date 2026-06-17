cmake_minimum_required(VERSION 3.20)
project(DX_GAME)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# テスト有効化
enable_testing()

# ソースファイルの収集
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")

# 実行可能なターゲットの作成
add_executable(DX_GAME WIN32 ${SOURCES} "DX_GAME.cpp")
target_precompile_headers(DX_GAME PRIVATE src/pch.h)

# インクルードディレクトリの設定
target_include_directories(DX_GAME PRIVATE 
    src 
    src/thirdparty/sqlite3
    libs/assimp-6.0.2/include
)

# SQLite3 amalgamation（ソースとして直接コンパイル）
target_sources(DX_GAME PRIVATE src/thirdparty/sqlite3/sqlite3.c)

# Assimpライブラリディレクトリ
target_link_directories(DX_GAME PRIVATE libs/assimp-6.0.2)

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
    assimp-vc143-mt
)

# 文字セットをUnicodeに設定、C++20機能有効化、エンコーディング指定
if(MSVC)
    target_compile_definitions(DX_GAME PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    target_compile_options(DX_GAME PRIVATE /utf-8 /Zc:__cplusplus)
endif()

# Post-build: Copy Assets
add_custom_command(TARGET DX_GAME POST_BUILD
    # Assets フォルダをそのままコピー
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets $<TARGET_FILE_DIR:DX_GAME>/Assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/libs/assimp-6.0.2/assimp-vc143-mt.dll $<TARGET_FILE_DIR:DX_GAME>/assimp-vc143-mt.dll
    COMMENT "Deploying assets to output directory..."
)

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
                                       src/core/Logger.cpp)
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
add_executable(test_physics_friction test_physics_friction.cpp)
target_precompile_headers(test_physics_friction PRIVATE src/pch.h)
target_include_directories(test_physics_friction PRIVATE src)
if(MSVC)
    target_compile_definitions(test_physics_friction PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                              -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_physics_friction PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME physics_friction COMMAND test_physics_friction)

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

add_executable(test_terrain_generation test_terrain_generation.cpp
                                       src/game/systems/TerrainGenerator.cpp
                                       src/graphics/TangentGenerator.cpp)
target_precompile_headers(test_terrain_generation PRIVATE src/pch.h)
target_include_directories(test_terrain_generation PRIVATE src)
if(MSVC)
    target_compile_definitions(test_terrain_generation PRIVATE -DUNICODE -D_UNICODE -DNOMINMAX
                                                               -DWIN32_LEAN_AND_MEAN)
    target_compile_options(test_terrain_generation PRIVATE /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME terrain_generation COMMAND test_terrain_generation)
