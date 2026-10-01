@echo off
setlocal
cd /d "%~dp0"

echo ========================================
echo Chair Shop - Build and Run
echo ========================================
echo.

set "MINGW_BIN=C:\ProgramData\mingw64\mingw64\bin"
set "GCC=%MINGW_BIN%\gcc.exe"
set "GXX=%MINGW_BIN%\g++.exe"
set "MAKE=%MINGW_BIN%\mingw32-make.exe"

if not exist "%GCC%" (
    echo ERROR: gcc.exe was not found at:
    echo %GCC%
    echo.
    echo Edit MINGW_BIN near the top of this file if your MinGW location changes.
    pause
    exit /b 1
)

if not exist "%GXX%" (
    echo ERROR: g++.exe was not found at:
    echo %GXX%
    pause
    exit /b 1
)

if not exist "%MAKE%" (
    echo ERROR: mingw32-make.exe was not found at:
    echo %MAKE%
    pause
    exit /b 1
)

if not exist "raylib\lib" mkdir "raylib\lib"
if not exist "lib" mkdir "lib"

if not exist "raylib\lib\libraylib.a" (
    echo [1/5] Building raylib...
    pushd "raylib\src"
    "%MAKE%" PLATFORM=PLATFORM_DESKTOP
    if errorlevel 1 (
        popd
        echo.
        echo ERROR: Raylib build failed.
        pause
        exit /b 1
    )
    popd

    if not exist "raylib\src\libraylib.a" (
        echo ERROR: raylib built, but libraylib.a was not found.
        pause
        exit /b 1
    )
    copy /Y "raylib\src\libraylib.a" "raylib\lib\libraylib.a" >nul
) else (
    echo [1/5] Raylib already built.
)

if not exist "lib\cimgui.o" (
    echo [2/5] Compiling Dear ImGui bindings...
    "%GXX%" -I./imgui -I./cimgui -c imgui_single_file.cpp -o ./lib/cimgui.o
    if errorlevel 1 goto :build_fail
) else (
    echo [2/5] Dear ImGui bindings already built.
)

if not exist "lib\rlimgui.o" (
    echo [3/5] Compiling Raylib ImGui backend...
    "%GXX%" -I./imgui -I./raylib/src -c ./raylib/rlImGui.cpp -o ./lib/rlimgui.o
    if errorlevel 1 goto :build_fail
) else (
    echo [3/5] Raylib ImGui backend already built.
)

if not exist "lib\sqlite3.o" (
    echo [4/5] Compiling SQLite...
    "%GCC%" -c ./sqlite3/sqlite3.c -o ./lib/sqlite3.o
    if errorlevel 1 goto :build_fail
) else (
    echo [4/5] SQLite already built.
)

echo [5/5] Building Chair Shop...
"%GCC%" ^
-I./imgui ^
-I./cimgui ^
-I./raylib ^
-I./raylib/src ^
-I./sqlite3 ^
./lib/sqlite3.o ^
./lib/cimgui.o ^
./lib/rlimgui.o ^
./src/shop.c ^
-L./raylib/lib ^
-lraylib ^
-lstdc++ ^
-lopengl32 ^
-lgdi32 ^
-lwinmm ^
-o shop.exe

if errorlevel 1 goto :build_fail

echo.
echo ========================================
echo Build successful. Starting Chair Shop...
echo ========================================
echo.
start "" "%~dp0shop.exe"
exit /b 0

:build_fail
echo.
echo ========================================
echo BUILD FAILED
 echo Copy the error messages above and send them to me.
echo ========================================
pause
exit /b 1
