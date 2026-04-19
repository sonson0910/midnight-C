@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM One-click build + transfer for Midnight C++ wallet-generator (undeployed by default)
REM Usage:
REM   run_cpp_transfer_undeployed.bat [TO_ADDRESS] [AMOUNT] [SEED_HEX] [NETWORK] [BUILD_DIR]
REM Example:
REM   run_cpp_transfer_undeployed.bat mn_addr_undeployed1... 1000000

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "TO_ADDRESS=%~1"
if "%TO_ADDRESS%"=="" set "TO_ADDRESS=mn_addr_undeployed1l6ry9unrexar7fskex9mww0c02akudhmcna73ygm7hwhc26f63gqkaa3pz"

set "AMOUNT=%~2"
if "%AMOUNT%"=="" set "AMOUNT=1000000"

set "SEED_HEX=%~3"
if "%SEED_HEX%"=="" set "SEED_HEX=0000000000000000000000000000000000000000000000000000000000000001"

set "NETWORK=%~4"
if "%NETWORK%"=="" set "NETWORK=undeployed"

set "BUILD_DIR=%~5"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build_ninja_cppfix"

set "CXX_ARG="
if exist "C:\msys64\ucrt64\bin\g++.exe" (
  set "CXX_ARG=-DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe"
)

echo [1/3] Configure CMake...
cmake -S "%ROOT_DIR%" -B "%ROOT_DIR%\%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF %CXX_ARG%
if errorlevel 1 (
  echo [ERROR] CMake configure failed.
  exit /b 1
)

echo [2/3] Build wallet-generator...
cmake --build "%ROOT_DIR%\%BUILD_DIR%" --target wallet-generator -j 4
if errorlevel 1 (
  echo [ERROR] Build failed.
  exit /b 1
)

set "EXE=%ROOT_DIR%\%BUILD_DIR%\bin\wallet-generator.exe"
if not exist "%EXE%" (
  echo [ERROR] Executable not found: %EXE%
  exit /b 1
)

echo [3/3] Transfer NIGHT via C++ wallet-generator...
"%EXE%" --official-sdk --network "%NETWORK%" --seed-hex "%SEED_HEX%" --official-transfer-to "%TO_ADDRESS%" --official-transfer-amount "%AMOUNT%"
if errorlevel 1 (
  echo [ERROR] Transfer command failed.
  exit /b 1
)

echo [OK] Completed build + transfer.
exit /b 0
