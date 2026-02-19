cmake_minimum_required(VERSION 3.20)
project(DX_GAME)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ソースファイルの収集
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h") 

# 実行可能なターゲットの作成
add_executable(DX_GAME WIN32 ${SOURCES} "DX_GAME.cpp")

# アセット（テクスチャ／モデル）コピー用のファイルリスト
file(GLOB TEXTURE_FILES "${CMAKE_SOURCE_DIR}/src/resources/textures/*.png")
file(GLOB ICON_FILES
     "${CMAKE_SOURCE_DIR}/src/resources/textures/Assets/icon_*.png")
set(ALL_TEXTURE_FILES ${TEXTURE_FILES} ${ICON_FILES})

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
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:DX_GAME>/Assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets $<TARGET_FILE_DIR:DX_GAME>/Assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/resources/textures $<TARGET_FILE_DIR:DX_GAME>/Assets/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:DX_GAME>/Assets/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/models $<TARGET_FILE_DIR:DX_GAME>/Assets/models
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/src/resources/textures/Assets/icon_driver.png $<TARGET_FILE_DIR:DX_GAME>/Assets/textures/icon_driver.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/src/resources/textures/Assets/icon_iron.png $<TARGET_FILE_DIR:DX_GAME>/Assets/textures/icon_iron.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/src/resources/textures/Assets/icon_putter.png $<TARGET_FILE_DIR:DX_GAME>/Assets/textures/icon_putter.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/src/resources/textures/Assets/icon_wedge.png $<TARGET_FILE_DIR:DX_GAME>/Assets/textures/icon_wedge.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/libs/assimp-6.0.2/assimp-vc143-mt.dll $<TARGET_FILE_DIR:DX_GAME>/assimp-vc143-mt.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/jawiki_sdow.sqlite $<TARGET_FILE_DIR:DX_GAME>/Assets/data/jawiki_sdow.sqlite
    COMMENT "Deploying assets to output directory..."
)

add_executable(SkyboxGen tools/SkyboxGen.cpp src/graphics/SkyboxTextureGenerator.cpp)
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
