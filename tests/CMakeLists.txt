enable_testing()

# Get all test source files
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

# Create test executable
add_executable(midnight-tests
    ${TEST_SOURCES}
)

target_link_libraries(midnight-tests
    PRIVATE
        midnight-core
)

target_include_directories(midnight-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Add tests
add_test(NAME core_tests COMMAND midnight-tests)
