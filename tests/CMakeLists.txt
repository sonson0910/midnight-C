enable_testing()

include(FetchContent)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    QUIET
)
FetchContent_MakeAvailable(googletest)

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

target_include_directories(midnight-tests
    BEFORE
    PRIVATE
        ${googletest_SOURCE_DIR}/googletest/include
)

target_include_directories(midnight-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Add tests
add_test(NAME core_tests COMMAND midnight-tests)
