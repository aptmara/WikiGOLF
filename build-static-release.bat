@echo off
setlocal
cd /d "%~dp0"

set "BUILD_DIR=build-static-release"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

echo [WikiGOLF] Static runtime / static link build (%BUILD_DIR%, %CONFIG%)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [WikiGOLF] Generating CMake build files...
    cmake -S . -B "%BUILD_DIR%" -A x64 -DWIKIGOLF_STATIC_BUILD=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
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
echo Output: %BUILD_DIR%\%CONFIG%\WikiGolf.exe
pause
