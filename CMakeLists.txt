cmake_minimum_required(VERSION 3.20)
project(midnight
    VERSION 0.1.0
    DESCRIPTION "Midnight SDK - Blockchain-enabled IoT infrastructure library"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(ENABLE_MQTT "Enable MQTT protocol support" ON)
option(ENABLE_COAP "Enable CoAP protocol support" ON)
option(ENABLE_HTTP "Enable HTTP/HTTPS protocol support" ON)
option(ENABLE_WEBSOCKET "Enable WebSocket support" ON)
option(ENABLE_BLOCKCHAIN "Enable blockchain support" ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find required packages
find_package(nlohmann_json 3.2.0 QUIET)
find_package(fmt 7.1.0 QUIET)

# Include FetchContent for header-only libraries
include(FetchContent)

if(NOT nlohmann_json_FOUND)
    message(STATUS "Fetching nlohmann_json...")
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        QUIET
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(NOT fmt_FOUND)
    message(STATUS "Fetching fmt...")
    FetchContent_Declare(
        fmt
        URL https://github.com/fmtlib/fmt/archive/refs/tags/11.1.4.zip
        QUIET
    )
    FetchContent_MakeAvailable(fmt)
endif()

find_package(jsoncpp QUIET)
if(NOT jsoncpp_FOUND)
    message(STATUS "Fetching jsoncpp...")
    FetchContent_Declare(
        jsoncpp
        URL https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/1.9.6.zip
        QUIET
    )
    FetchContent_MakeAvailable(jsoncpp)
endif()

# Fetch cpp-httplib (header-only HTTP client library)
if(NOT DEFINED HTTPLIB_SOURCE_DIR)
    message(STATUS "Fetching cpp-httplib...")
    FetchContent_Declare(
        cpp-httplib
        URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.42.0.zip
        QUIET
    )
    FetchContent_GetProperties(cpp-httplib)
    if(NOT cpp-httplib_POPULATED)
        FetchContent_Populate(cpp-httplib)
    endif()
    set(HTTPLIB_SOURCE_DIR "${cpp-httplib_SOURCE_DIR}")
endif()
message(STATUS "cpp-httplib path: ${HTTPLIB_SOURCE_DIR}")

# Add subdirectories
add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install targets
install(
    DIRECTORY include/midnight
    DESTINATION include
)

# Export targets when dependencies are available in an installable export set.
# Disabled for local/dev builds that fetch third-party dependencies.
