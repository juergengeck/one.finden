#include "rpc_session.hpp"

namespace fused {

bool SessionAwareRPCHandler::process_call(const std::vector<uint8_t>& call_data,
                                        std::vector<uint8_t>& reply_data) {
    // Decode call header
    RPCCallHeader header;
    size_t offset = 0;
    if (!decode_call_header(call_data, offset, header)) {
        LOG_ERROR("Failed to decode RPC call header");
        return false;
    }

    // Skip session validation for session management operations
    if (header.procedure != NFSProcedure::CREATE_SESSION &&
        header.procedure != NFSProcedure::DESTROY_SESSION) {
        
        // Validate session
        if (!validate_session(header, reply_data)) {
            return true;  // Error response already sent
        }

        // Check sequence ID
        if (!check_sequence(header, reply_data)) {
            return true;  // Error response already sent
        }
    }

    // Process the call
    bool result = handle_call(header, call_data, offset, reply_data);

    // Update sequence ID if call was successful
    if (result && header.session_id != 0) {
        session_manager_.update_sequence(header.session_id, header.sequence_id);
    }

    return result;
}

bool SessionAwareRPCHandler::validate_session(const RPCCallHeader& header,
                                            std::vector<uint8_t>& reply_data) {
    if (header.session_id == 0) {
        LOG_ERROR("Missing session ID in RPC call");
        send_session_error(header, RPCAcceptStatus::AUTH_ERROR, reply_data);
        return false;
    }

    if (!session_manager_.is_session_valid(header.session_id)) {
        LOG_ERROR("Invalid session ID: {}", header.session_id);
        send_session_error(header, RPCAcceptStatus::AUTH_ERROR, reply_data);
        return false;
    }

    if (!session_manager_.is_session_confirmed(header.session_id)) {
        LOG_ERROR("Unconfirmed session ID: {}", header.session_id);
        send_session_error(header, RPCAcceptStatus::AUTH_ERROR, reply_data);
        return false;
    }

    return true;
}

bool SessionAwareRPCHandler::check_sequence(const RPCCallHeader& header,
                                          std::vector<uint8_t>& reply_data) {
    if (!session_manager_.check_sequence(header.session_id, header.sequence_id)) {
        LOG_ERROR("Invalid sequence ID for session {}", header.session_id);
        send_session_error(header, RPCAcceptStatus::AUTH_ERROR, reply_data);
        return false;
    }

    return true;
}

void SessionAwareRPCHandler::send_session_error(const RPCCallHeader& header,
                                              RPCAcceptStatus status,
                                              std::vector<uint8_t>& reply_data) {
    RPCReplyHeader reply_header{
        header.xid,
        RPCMessageType::REPLY,
        RPCReplyStatus::MSG_ACCEPTED,
        status,
        {}  // Empty verifier
    };

    XDREncoder encoder;
    encoder.encode(reply_header.xid);
    encoder.encode(static_cast<uint32_t>(reply_header.type));
    encoder.encode(static_cast<uint32_t>(reply_header.reply_status));
    encoder.encode(static_cast<uint32_t>(reply_header.accept_status));
    encoder.encode_opaque(reply_header.verifier);

    reply_data = encoder.get_buffer();
}

} // namespace fuse_t 