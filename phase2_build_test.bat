@echo off
REM Phase 2 Build Verification Script
REM Tests that the real HTTP implementation compiles correctly

setlocal enabledelayedexpansion

echo.
echo ========================================
echo Phase 2: Build Verification
echo ========================================
echo.

cd /d d:\venera\midnight\night_fund

REM Check if build directory exists
if not exist build mkdir build

REM Configure CMake with cpp-httplib dependency
echo [1/4] Configuring CMake project...
cmake -B build -G "Visual Studio 17 2022" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_EXAMPLES=ON ^
    -DBUILD_TESTS=OFF ^
    -DENABLE_MQTT=ON ^
    -DENABLE_COAP=OFF ^
    -DENABLE_HTTP=ON

if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    exit /b 1
)

echo.
echo [2/4] Downloading cpp-httplib dependency...
echo NOTE: FetchContent will download from GitHub

echo.
echo [3/4] Building project...
cmake --build build --config Release -- /m:4

if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    exit /b 1
)

echo.
echo [4/4] Build verification complete!
echo.
echo ========================================
echo Phase 2 Build Status: SUCCESS ✓
echo ========================================
echo.
echo Built executables:
echo   - bin\Release\blockchain_example.exe
echo   - bin\Release\http_connectivity_test.exe
echo.
echo Next steps:
echo   1. Run: .\build\bin\Release\http_connectivity_test.exe
echo      (Tests HTTP connectivity to Midnight network)
echo.
echo   2. Run: .\build\bin\Release\blockchain_example.exe
echo      (End-to-end blockchain example with real RPC)
echo.
echo Note: These tests require internet connection for:
echo   - Midnight Preprod testnet (https://preprod.midnight.network/api)
echo   - OpenSSL for HTTPS support
echo.
