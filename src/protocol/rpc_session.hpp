#pragma once
#include "rpc_message.hpp"
#include "session.hpp"
#include "util/logger.hpp"

namespace fused {

class SessionAwareRPCHandler {
public:
    explicit SessionAwareRPCHandler(SessionManager& session_manager)
        : session_manager_(session_manager) {}

    // Process RPC call with session validation
    bool process_call(const std::vector<uint8_t>& call_data,
                     std::vector<uint8_t>& reply_data);

protected:
    // Session validation helpers
    bool validate_session(const RPCCallHeader& header,
                        std::vector<uint8_t>& reply_data);
    
    bool check_sequence(const RPCCallHeader& header,
                       std::vector<uint8_t>& reply_data);

    // Session error responses
    void send_session_error(const RPCCallHeader& header,
                          RPCAcceptStatus status,
                          std::vector<uint8_t>& reply_data);

private:
    SessionManager& session_manager_;
};

} // namespace fuse_t 