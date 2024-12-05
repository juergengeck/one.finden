# Test configuration
set(TEST_SOURCES
    main.cpp
    test_framework.cpp
    transaction_tests.cpp
    fuse_server_tests.cpp
    nfs_server_tests.cpp
)

# Create test executable
add_executable(fused-tests ${TEST_SOURCES})

# Link with main library
target_link_libraries(fused-tests
    PRIVATE
        fused-nfs
)

# Include directories
target_include_directories(fused-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Enable testing
enable_testing()

# Add test
add_test(
    NAME fused-tests
    COMMAND fused-tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Set test properties
set_tests_properties(fused-tests PROPERTIES
    ENVIRONMENT "FUSED_TEST_DIR=${CMAKE_BINARY_DIR}/test-data"
)

# Create test data directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-data) 