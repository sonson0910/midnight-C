enable_testing()

include(FetchContent)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        QUIET
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Get all test source files
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

# Create test executable
add_executable(midnight-tests
    ${TEST_SOURCES}
)

target_link_libraries(midnight-tests
    PRIVATE
        midnight-core
        GTest::gtest
        GTest::gtest_main
)

if(DEFINED googletest_SOURCE_DIR)
    target_include_directories(midnight-tests
        BEFORE
        PRIVATE
            ${googletest_SOURCE_DIR}/googletest/include
    )
endif()

target_include_directories(midnight-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Include directories for dependencies
find_path(JSONCPP_INCLUDE_DIR json/json.h
    HINTS
        ${CMAKE_BINARY_DIR}/_deps/jsoncpp-src/include
        /opt/homebrew/include
        /usr/local/include
        /usr/include/jsoncpp
    PATH_SUFFIXES
        jsoncpp
)

if(JSONCPP_INCLUDE_DIR)
    target_include_directories(midnight-tests
        PRIVATE
            ${JSONCPP_INCLUDE_DIR}
    )
endif()

target_compile_definitions(midnight-tests
    PRIVATE
        MIDNIGHT_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        MIDNIGHT_TEST_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

if(TARGET midnight-ledger-ffi)
    add_dependencies(midnight-tests midnight-ledger-ffi)
endif()

if(DEFINED MIDNIGHT_LEDGER_FFI_LIBRARY)
    target_compile_definitions(midnight-tests
        PRIVATE
            MIDNIGHT_TEST_LEDGER_FFI_LIBRARY="${MIDNIGHT_LEDGER_FFI_LIBRARY}"
    )
endif()

# Add tests
add_test(NAME core_tests COMMAND midnight-tests)
