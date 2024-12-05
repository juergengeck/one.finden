#include "rpc.hpp"
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace fused {

// RPCCredentials implementation
void RPCCredentials::encode(XDREncoder& encoder) const {
    encoder.encode_uint32(static_cast<uint32_t>(flavor));
    encoder.encode_opaque(body);
}

RPCCredentials RPCCredentials::decode(XDRDecoder& decoder) {
    RPCCredentials cred;
    cred.flavor = static_cast<RPCAuthFlavor>(decoder.decode_uint32());
    cred.body = decoder.decode_opaque();
    return cred;
}

// RPCHeader implementation
void RPCHeader::encode(XDREncoder& encoder) const {
    encoder.encode_uint32(xid);
    encoder.encode_uint32(static_cast<uint32_t>(msg_type));
    
    if (msg_type == RPCMessageType::CALL) {
        encoder.encode_uint32(rpc_version);
        encoder.encode_uint32(program);
        encoder.encode_uint32(version);
        encoder.encode_uint32(procedure);
        cred.encode(encoder);
        verf.encode(encoder);
    } else {
        encoder.encode_uint32(static_cast<uint32_t>(reply_status));
        if (reply_status == RPCReplyStatus::MSG_DENIED) {
            encoder.encode_uint32(static_cast<uint32_t>(auth_status));
        }
    }
}

RPCHeader RPCHeader::decode(XDRDecoder& decoder) {
    RPCHeader header;
    header.xid = decoder.decode_uint32();
    header.msg_type = static_cast<RPCMessageType>(decoder.decode_uint32());
    
    if (header.msg_type == RPCMessageType::CALL) {
        header.rpc_version = decoder.decode_uint32();
        header.program = decoder.decode_uint32();
        header.version = decoder.decode_uint32();
        header.procedure = decoder.decode_uint32();
        header.cred = RPCCredentials::decode(decoder);
        header.verf = RPCCredentials::decode(decoder);
    } else {
        header.reply_status = static_cast<RPCReplyStatus>(decoder.decode_uint32());
        if (header.reply_status == RPCReplyStatus::MSG_DENIED) {
            header.auth_status = static_cast<RPCAuthStatus>(decoder.decode_uint32());
        }
    }
    
    return header;
}

// RPCConnection implementation
RPCConnection::RPCConnection(int sock) : sock_(sock) {}

bool RPCConnection::send_message(const RPCHeader& header, const std::vector<uint8_t>& body) {
    // Encode header
    XDREncoder encoder;
    header.encode(encoder);
    
    // Get encoded header
    auto header_buffer = encoder.get_buffer();
    
    // Send header size
    uint32_t header_size = htonl(header_buffer.size());
    if (write(sock_, &header_size, sizeof(header_size)) != sizeof(header_size)) {
        return false;
    }
    
    // Send header
    if (write(sock_, header_buffer.data(), header_buffer.size()) != 
        static_cast<ssize_t>(header_buffer.size())) {
        return false;
    }
    
    // Send body size
    uint32_t body_size = htonl(body.size());
    if (write(sock_, &body_size, sizeof(body_size)) != sizeof(body_size)) {
        return false;
    }
    
    // Send body
    if (!body.empty() && write(sock_, body.data(), body.size()) != 
        static_cast<ssize_t>(body.size())) {
        return false;
    }
    
    return true;
}

bool RPCConnection::receive_message(RPCHeader& header, std::vector<uint8_t>& body) {
    // Read header size
    uint32_t header_size;
    if (read(sock_, &header_size, sizeof(header_size)) != sizeof(header_size)) {
        return false;
    }
    header_size = ntohl(header_size);
    
    // Read header data
    std::vector<uint8_t> header_buffer(header_size);
    if (read(sock_, header_buffer.data(), header_size) != static_cast<ssize_t>(header_size)) {
        return false;
    }
    
    // Decode header
    XDRDecoder decoder(header_buffer.data(), header_size);
    header = RPCHeader::decode(decoder);
    
    // Read body size
    uint32_t body_size;
    if (read(sock_, &body_size, sizeof(body_size)) != sizeof(body_size)) {
        return false;
    }
    body_size = ntohl(body_size);
    
    // Read body data
    body.resize(body_size);
    if (body_size > 0 && read(sock_, body.data(), body_size) != static_cast<ssize_t>(body_size)) {
        return false;
    }
    
    return true;
}

} // namespace fuse_t 