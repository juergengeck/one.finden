cmake_minimum_required(VERSION 3.12)
project(fuse-t VERSION 1.0.0 LANGUAGES CXX)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/fuse_server.cpp
    src/nfs_server.cpp
    src/mount/mount_manager.cpp
)

# Header files
set(HEADERS
    src/fuse_server.hpp
    src/nfs_server.hpp
    src/mount/mount_manager.hpp
    src/protocol/fuse_protocol.hpp
    src/protocol/nfs_protocol.hpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
)

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Install launchd plist and create directories
if(APPLE)
    # Install post-install script
    install(PROGRAMS scripts/post-install.sh
        DESTINATION /usr/local/lib/fused-nfs
    )

    # Run post-install script
    install(CODE "
        execute_process(
            COMMAND /usr/local/lib/fused-nfs/post-install.sh
        )
    ")

    install(FILES resources/fused-nfs.plist
        DESTINATION /Library/LaunchDaemons
        RENAME com.finden.fused-nfs.plist
    )

    # Create log directory
    install(CODE "
        execute_process(
            COMMAND mkdir -p /var/log/fused-nfs
            COMMAND chmod 755 /var/log/fused-nfs
        )
    ")

    # Create service directories
    install(CODE "
        execute_process(
            COMMAND mkdir -p /var/lib/fused-nfs
            COMMAND chmod 755 /var/lib/fused-nfs
            COMMAND mkdir -p /var/lib/fused-nfs/exports
            COMMAND chmod 755 /var/lib/fused-nfs/exports
        )
    ")
endif()

# Main library and executable
add_subdirectory(src)

# Tests
option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif() 