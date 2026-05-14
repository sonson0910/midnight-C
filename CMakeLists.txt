cmake_minimum_required(VERSION 3.20)
project(midnight
    VERSION 0.1.0
    DESCRIPTION "Midnight SDK - Blockchain-enabled IoT infrastructure library"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# In Linux host shells with Conda activated, find_package can incorrectly bind
# against Conda's older runtime libs (libstdc++, OpenSSL, fmt), causing test
# executables to fail at runtime. Keep Conda available for tooling, but ignore
# it for CMake package discovery.
if(UNIX AND NOT WIN32 AND DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_IGNORE_PREFIX_PATH
        "$ENV{CONDA_PREFIX}"
        "$ENV{CONDA_PREFIX}/lib"
        "$ENV{CONDA_PREFIX}/lib/cmake"
    )
    message(STATUS "Ignoring CONDA_PREFIX for package resolution: $ENV{CONDA_PREFIX}")
endif()

# Options
set(_MIDNIGHT_DEFAULT_SHARED_LIBS ON)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # MinGW DLL export discovery is fragile for this codebase; static linking is more reliable.
    set(_MIDNIGHT_DEFAULT_SHARED_LIBS OFF)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ${_MIDNIGHT_DEFAULT_SHARED_LIBS})
unset(_MIDNIGHT_DEFAULT_SHARED_LIBS)
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(ENABLE_MQTT "Enable MQTT protocol support" ON)
option(ENABLE_COAP "Enable CoAP protocol support" ON)
option(ENABLE_HTTP "Enable HTTP/HTTPS protocol support" ON)
option(ENABLE_WEBSOCKET "Enable WebSocket support" ON)
option(ENABLE_BLOCKCHAIN "Enable blockchain support" ON)
option(MIDNIGHT_REQUIRE_OPENSSL_FOR_BLOCKCHAIN "Require OpenSSL when blockchain HTTPS support is enabled" ON)
option(MIDNIGHT_BUILD_LEDGER_FFI "Build the native Midnight ledger FFI backend with Cargo when available" ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find required packages
find_package(nlohmann_json 3.2.0 QUIET)

set(MIDNIGHT_FORCE_FETCH_FMT OFF)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Avoid ABI mismatch with prebuilt MSVC fmt binaries when building with MinGW.
    set(MIDNIGHT_FORCE_FETCH_FMT ON)
endif()

if(NOT MIDNIGHT_FORCE_FETCH_FMT)
    find_package(fmt 7.1.0 QUIET)
endif()

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

if(MIDNIGHT_FORCE_FETCH_FMT OR NOT fmt_FOUND)
    set(_MIDNIGHT_LOCAL_FMT_DIR "")
    foreach(_MIDNIGHT_FMT_CANDIDATE
        "${CMAKE_SOURCE_DIR}/build_ninja_cppfix/_deps/fmt-src"
        "${CMAKE_SOURCE_DIR}/build_ninja/_deps/fmt-src"
        "${CMAKE_SOURCE_DIR}/build/_deps/fmt-src"
    )
        if(EXISTS "${_MIDNIGHT_FMT_CANDIDATE}/CMakeLists.txt")
            set(_MIDNIGHT_LOCAL_FMT_DIR "${_MIDNIGHT_FMT_CANDIDATE}")
            break()
        endif()
    endforeach()

    if(_MIDNIGHT_LOCAL_FMT_DIR)
        message(STATUS "Using cached fmt source: ${_MIDNIGHT_LOCAL_FMT_DIR}")
        if(NOT TARGET fmt)
            add_subdirectory(
                "${_MIDNIGHT_LOCAL_FMT_DIR}"
                "${CMAKE_BINARY_DIR}/_deps/fmt-build"
                EXCLUDE_FROM_ALL
            )
        endif()
    else()
        message(STATUS "Fetching fmt...")
        FetchContent_Declare(
            fmt
            URL https://github.com/fmtlib/fmt/archive/refs/tags/11.1.4.zip
            QUIET
        )
        FetchContent_MakeAvailable(fmt)
    endif()

    unset(_MIDNIGHT_LOCAL_FMT_DIR)
    unset(_MIDNIGHT_FMT_CANDIDATE)
endif()

# jsoncpp (JSON parsing library)
find_package(jsoncpp QUIET)
if(NOT jsoncpp_FOUND AND NOT TARGET jsoncpp_static AND NOT TARGET jsoncpp_lib)
    # Force static library to ensure proper linking for the vendored fallback.
    set(JSONCPP_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(JSONCPP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(JSONCPP_BUILD_OBJECT_LIBS OFF CACHE BOOL "" FORCE)

    message(STATUS "Fetching jsoncpp...")
    FetchContent_Declare(
        jsoncpp
        URL https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/1.9.6.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(jsoncpp)
    if(NOT jsoncpp_POPULATED)
        FetchContent_Populate(jsoncpp)
    endif()
    set(jsoncpp_SOURCE_DIR "${jsoncpp_SOURCE_DIR}")
    set(jsoncpp_POPULATED TRUE)
    add_subdirectory("${jsoncpp_SOURCE_DIR}" "${jsoncpp_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

# cpp-httplib (header-only HTTP client library)
find_package(httplib QUIET)
if(TARGET httplib::httplib)
    get_target_property(_MIDNIGHT_HTTPLIB_INCLUDES httplib::httplib INTERFACE_INCLUDE_DIRECTORIES)
    list(GET _MIDNIGHT_HTTPLIB_INCLUDES 0 HTTPLIB_SOURCE_DIR)
elseif(NOT DEFINED HTTPLIB_SOURCE_DIR)
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

# gRPC + Protobuf (required for gRPC protocol support)
option(ENABLE_GRPC "Enable gRPC protocol support" OFF)

if(ENABLE_GRPC)
    # gRPC depends on abseil; disable unwanted targets
    set(gRPC_BUILD_STRIP_ERRORS OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_DEFAULT_CMP0167 NEW) # FetchContent_MakeAvailable

    FetchContent_Declare(
        grpc
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG v1.68.0
        GIT_SUBMODULES "third_party/zlib third_party/gflags third_party/abseil-cpp third_party/protobuf"
    )

    FetchContent_Declare(
        protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG v28.3
        GIT_SUBMODULES "third_party/utf8_range"
    )

    if(NOT grpc_POPULATED)
        FetchContent_GetProperties(grpc)
        if(NOT grpc_POPULATED)
            message(STATUS "Fetching gRPC v1.68.0...")
            FetchContent_Populate(grpc)
        endif()
    endif()
    add_subdirectory(${grpc_SOURCE_DIR} ${grpc_BINARY_DIR} EXCLUDE_FROM_ALL)

    if(NOT protobuf_POPULATED)
        FetchContent_GetProperties(protobuf)
        if(NOT protobuf_POPULATED)
            message(STATUS "Fetching protobuf v28.3...")
            FetchContent_Populate(protobuf)
        endif()
    endif()
    add_subdirectory(${protobuf_SOURCE_DIR} ${protobuf_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# BLS library (blst) - optional, requires special build setup
# For production BLS12-381 signatures with full performance:
# 1. Install blst: git clone https://github.com/supranational/blst.git && cd blst && ./build.sh
# 2. Use find_package(blst) or link against the pre-built library
# For now, we use libsodium-based BLS12-381 simulation via libsodium's Ristretto255
# This provides wire-compatible signatures for development/testing
option(ENABLE_BLST "Enable blst library for BLS signatures (requires manual blst installation)" OFF)

# Add subdirectories
if(MIDNIGHT_BUILD_LEDGER_FFI)
    find_program(CARGO_EXECUTABLE cargo)
    if(CARGO_EXECUTABLE)
        set(MIDNIGHT_LEDGER_FFI_MANIFEST
            "${CMAKE_SOURCE_DIR}/midnight-research/midnight-node/Cargo.toml")
        set(MIDNIGHT_LEDGER_FFI_TARGET_DIR
            "${CMAKE_BINARY_DIR}/midnight-ledger-ffi")
        if(APPLE)
            set(MIDNIGHT_LEDGER_FFI_LIBRARY
                "${MIDNIGHT_LEDGER_FFI_TARGET_DIR}/release/libmidnight_ledger_ffi.dylib")
        elseif(WIN32)
            set(MIDNIGHT_LEDGER_FFI_LIBRARY
                "${MIDNIGHT_LEDGER_FFI_TARGET_DIR}/release/midnight_ledger_ffi.dll")
        else()
            set(MIDNIGHT_LEDGER_FFI_LIBRARY
                "${MIDNIGHT_LEDGER_FFI_TARGET_DIR}/release/libmidnight_ledger_ffi.so")
        endif()

        add_custom_command(
            OUTPUT "${MIDNIGHT_LEDGER_FFI_LIBRARY}"
            COMMAND "${CARGO_EXECUTABLE}" build
                --manifest-path "${MIDNIGHT_LEDGER_FFI_MANIFEST}"
                --package midnight-ledger-ffi
                --release
                --target-dir "${MIDNIGHT_LEDGER_FFI_TARGET_DIR}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/midnight-research/midnight-node"
            DEPENDS
                "${CMAKE_SOURCE_DIR}/midnight-research/midnight-node/util/ledger-ffi/Cargo.toml"
                "${CMAKE_SOURCE_DIR}/midnight-research/midnight-node/util/ledger-ffi/src/lib.rs"
            COMMENT "Building native Midnight ledger FFI backend"
            VERBATIM
        )
        add_custom_target(midnight-ledger-ffi ALL
            DEPENDS "${MIDNIGHT_LEDGER_FFI_LIBRARY}")
        install(FILES "${MIDNIGHT_LEDGER_FFI_LIBRARY}" DESTINATION lib OPTIONAL)
    else()
        message(WARNING
            "MIDNIGHT_BUILD_LEDGER_FFI is ON but cargo was not found; "
            "C++ will build, but production transaction construction needs libmidnight_ledger_ffi.")
    endif()
endif()

add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install targets
include(CMakePackageConfigHelpers)

install(
    DIRECTORY include/midnight
    DESTINATION include
)

install(
    TARGETS midnight-core
    EXPORT midnightTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(
    EXPORT midnightTargets
    FILE midnightTargets.cmake
    NAMESPACE midnight::
    DESTINATION lib/cmake/midnight
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/midnightConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/midnightConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/midnightConfig.cmake"
    INSTALL_DESTINATION lib/cmake/midnight
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/midnight.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/midnight.pc"
    @ONLY
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/midnightConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/midnightConfigVersion.cmake"
    DESTINATION lib/cmake/midnight
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/midnight.pc"
    DESTINATION lib/pkgconfig
)
