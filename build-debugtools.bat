@echo off
setlocal
cd /d "%~dp0"

set "BUILD_DIR=build-debugtools"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

echo [WikiGOLF] Debug tools build with Dear ImGui (%BUILD_DIR%, %CONFIG%)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [WikiGOLF] Generating CMake build files...
    cmake -S . -B "%BUILD_DIR%" -A x64 -DWIKIGOLF_DEBUG_TOOLS=ON
    if errorlevel 1 (
        echo [ERROR] CMake configure failed.
        pause
        exit /b 1
    )
)

cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo [WikiGOLF] Build finished successfully.
echo Output: %BUILD_DIR%\%CONFIG%\DX_GAME.exe
pause
