#pragma once
#include <memory>
#include "rpc_message.hpp"
#include "nfs_protocol.hpp"

namespace fused {

// Base class for RPC procedure handlers
class RPCProcedureHandler {
public:
    virtual ~RPCProcedureHandler() = default;
    
    // Handle RPC procedure call
    virtual bool handle_call(const RPCCallHeader& call_header,
                           const std::vector<uint8_t>& call_data,
                           size_t& offset,
                           std::vector<uint8_t>& reply_data) = 0;

protected:
    // Helper method to send error response
    void send_error_reply(const RPCCallHeader& call_header,
                         RPCAcceptStatus status,
                         std::vector<uint8_t>& reply_data);
};

// NFS procedure handler
class NFSProcedureHandler : public RPCProcedureHandler {
public:
    explicit NFSProcedureHandler(NFSServer& server) : server_(server) {}

    bool handle_call(const RPCCallHeader& call_header,
                    const std::vector<uint8_t>& call_data,
                    size_t& offset,
                    std::vector<uint8_t>& reply_data) override;

private:
    NFSServer& server_;

    // NFS procedure handlers
    bool handle_null(const RPCCallHeader& call_header,
                    const std::vector<uint8_t>& call_data,
                    size_t& offset,
                    std::vector<uint8_t>& reply_data);

    bool handle_getattr(const RPCCallHeader& call_header,
                       const std::vector<uint8_t>& call_data,
                       size_t& offset,
                       std::vector<uint8_t>& reply_data);

    bool handle_lookup(const RPCCallHeader& call_header,
                      const std::vector<uint8_t>& call_data,
                      size_t& offset,
                      std::vector<uint8_t>& reply_data);

    bool handle_read(const RPCCallHeader& call_header,
                    const std::vector<uint8_t>& call_data,
                    size_t& offset,
                    std::vector<uint8_t>& reply_data);

    bool handle_write(const RPCCallHeader& call_header,
                     const std::vector<uint8_t>& call_data,
                     size_t& offset,
                     std::vector<uint8_t>& reply_data);

    bool handle_create(const RPCCallHeader& call_header,
                      const std::vector<uint8_t>& call_data,
                      size_t& offset,
                      std::vector<uint8_t>& reply_data);

    bool handle_remove(const RPCCallHeader& call_header,
                      const std::vector<uint8_t>& call_data,
                      size_t& offset,
                      std::vector<uint8_t>& reply_data);

    // Add more NFS procedure handlers...
};

// Mount procedure handler
class MountProcedureHandler : public RPCProcedureHandler {
public:
    explicit MountProcedureHandler(NFSServer& server) : server_(server) {}

    bool handle_call(const RPCCallHeader& call_header,
                    const std::vector<uint8_t>& call_data,
                    size_t& offset,
                    std::vector<uint8_t>& reply_data) override;

private:
    NFSServer& server_;

    // Mount procedure handlers
    bool handle_null(const RPCCallHeader& call_header,
                    const std::vector<uint8_t>& call_data,
                    size_t& offset,
                    std::vector<uint8_t>& reply_data);

    bool handle_mnt(const RPCCallHeader& call_header,
                   const std::vector<uint8_t>& call_data,
                   size_t& offset,
                   std::vector<uint8_t>& reply_data);

    // Add more mount procedure handlers...
};

// RPC handler factory
class RPCHandlerFactory {
public:
    static std::unique_ptr<RPCProcedureHandler> create_handler(
        uint32_t program, NFSServer& server);
};

} // namespace fuse_t 