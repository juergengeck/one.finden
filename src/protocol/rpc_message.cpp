#include "rpc_message.hpp"
#include "util/logger.hpp"
#include "xdr.hpp"

namespace fused {

RPCMessageHandler::RPCMessageHandler()
    : auth_manager_(std::make_unique<RPCAuthManager>()) {
}

bool RPCMessageHandler::initialize(const std::string& service_name) {
    if (initialized_) {
        LOG_INFO("RPC message handler already initialized");
        return true;
    }

    LOG_INFO("Initializing RPC message handler");

    if (!auth_manager_->initialize(service_name)) {
        LOG_ERROR("Failed to initialize RPC auth manager");
        return false;
    }

    initialized_ = true;
    LOG_INFO("RPC message handler initialized successfully");
    return true;
}

bool RPCMessageHandler::process_call(const std::vector<uint8_t>& call_data,
                                   std::vector<uint8_t>& reply_data) {
    if (!initialized_) {
        LOG_ERROR("RPC message handler not initialized");
        return false;
    }

    // Decode call header
    RPCCallHeader call_header;
    size_t offset = 0;
    if (!decode_call_header(call_data, offset, call_header)) {
        LOG_ERROR("Failed to decode RPC call header");
        return false;
    }

    // Verify authentication
    std::vector<uint8_t> reply_verifier;
    if (!verify_auth(call_header, reply_verifier)) {
        LOG_ERROR("RPC authentication failed");
        
        // Send auth error reply
        RPCReplyHeader reply_header{
            call_header.xid,
            RPCMessageType::REPLY,
            RPCReplyStatus::MSG_ACCEPTED,
            RPCAcceptStatus::AUTH_ERROR,
            reply_verifier
        };
        encode_reply_header(reply_data, reply_header);
        return true;
    }

    // Create appropriate handler for the program
    auto handler = RPCHandlerFactory::create_handler(call_header.program, server_);
    if (!handler) {
        LOG_ERROR("Unsupported RPC program: {}", call_header.program);
        
        // Send program unavailable error
        RPCReplyHeader reply_header{
            call_header.xid,
            RPCMessageType::REPLY,
            RPCReplyStatus::MSG_ACCEPTED,
            RPCAcceptStatus::PROG_UNAVAIL,
            reply_verifier
        };
        encode_reply_header(reply_data, reply_header);
        return true;
    }

    // Handle the call
    return handler->handle_call(call_header, call_data, offset, reply_data);
}

bool RPCMessageHandler::decode_call_header(const std::vector<uint8_t>& data,
                                         size_t& offset,
                                         RPCCallHeader& header) {
    XDRDecoder decoder(data);
    decoder.set_offset(offset);

    // Decode fixed header fields
    if (!decoder.decode(header.xid) ||
        !decoder.decode(reinterpret_cast<uint32_t&>(header.type)) ||
        !decoder.decode(header.rpc_version) ||
        !decoder.decode(header.program) ||
        !decoder.decode(header.version) ||
        !decoder.decode(header.procedure)) {
        return false;
    }

    // Decode auth header
    uint32_t flavor;
    if (!decoder.decode(flavor)) {
        return false;
    }
    header.auth.flavor = static_cast<RPCAuthFlavor>(flavor);

    // Decode credentials and verifier
    if (!decoder.decode_opaque(header.auth.credentials) ||
        !decoder.decode_opaque(header.auth.verifier)) {
        return false;
    }

    offset = decoder.get_offset();
    return true;
}

void RPCMessageHandler::encode_reply_header(std::vector<uint8_t>& data,
                                          const RPCReplyHeader& header) {
    XDREncoder encoder;

    // Encode fixed header fields
    encoder.encode(header.xid);
    encoder.encode(static_cast<uint32_t>(header.type));
    encoder.encode(static_cast<uint32_t>(header.reply_status));
    encoder.encode(static_cast<uint32_t>(header.accept_status));

    // Encode verifier
    encoder.encode_opaque(header.verifier);

    data = encoder.get_buffer();
}

bool RPCMessageHandler::verify_auth(const RPCCallHeader& call_header,
                                  std::vector<uint8_t>& reply_verifier) {
    return auth_manager_->verify_auth(call_header.auth, reply_verifier);
}

bool RPCMessageHandler::check_operation_auth(const RPCCallHeader& call_header,
                                           const std::string& path,
                                           uint32_t access_mask) {
    return auth_manager_->check_operation_auth(call_header.auth, path, access_mask);
}

} // namespace fuse_t 