# Library sources
set(LIB_SOURCES
    nfs_server.cpp
    mount/mount_manager.cpp
    protocol/xdr.cpp
    protocol/rpc.cpp
    protocol/rpc_message.cpp
    protocol/auth.cpp
    protocol/auth_gss.cpp
    protocol/compound.cpp
    protocol/operations.cpp
    protocol/client_session.cpp
    protocol/session_recovery.cpp
    protocol/operation_log.cpp
    protocol/transaction_log.cpp
    security/auth_manager.cpp
    security/encryption_manager.cpp
    util/logger.cpp
)

# Library headers
set(LIB_HEADERS
    nfs_server.hpp
    mount/mount_manager.hpp
    protocol/nfs_protocol.hpp
    protocol/xdr.hpp
    protocol/rpc.hpp
    protocol/rpc_message.hpp
    protocol/auth.hpp
    protocol/auth_gss.hpp
    protocol/compound.hpp
    protocol/operations.hpp
    protocol/client_session.hpp
    protocol/session_recovery.hpp
    protocol/operation_log.hpp
    protocol/transaction_log.hpp
    security/auth_manager.hpp
    security/encryption_manager.hpp
    util/logger.hpp
)

# Create library
add_library(fused-nfs STATIC
    ${LIB_SOURCES}
    ${LIB_HEADERS}
)

# Include directories
target_include_directories(fused-nfs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Link libraries
target_link_libraries(fused-nfs
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
) 