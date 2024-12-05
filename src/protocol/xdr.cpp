#include "xdr.hpp"
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>  // for htonl/ntohl

namespace fused {

// XDREncoder implementation
void XDREncoder::encode_uint32(uint32_t value) {
    pad_to_alignment(4);
    uint32_t net_value = htonl(value);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&net_value);
    buffer_.insert(buffer_.end(), bytes, bytes + 4);
}

void XDREncoder::encode_uint64(uint64_t value) {
    pad_to_alignment(8);
    encode_uint32(value >> 32);
    encode_uint32(value & 0xFFFFFFFF);
}

void XDREncoder::encode_string(const std::string& str) {
    encode_uint32(str.length());
    buffer_.insert(buffer_.end(), str.begin(), str.end());
    pad_to_alignment(4);
}

void XDREncoder::encode_opaque(const std::vector<uint8_t>& data) {
    encode_uint32(data.size());
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    pad_to_alignment(4);
}

void XDREncoder::encode_bool(bool value) {
    encode_uint32(value ? 1 : 0);
}

std::vector<uint8_t> XDREncoder::get_buffer() const {
    return buffer_;
}

void XDREncoder::pad_to_alignment(size_t alignment) {
    size_t padding = (alignment - (buffer_.size() % alignment)) % alignment;
    buffer_.insert(buffer_.end(), padding, 0);
}

// XDRDecoder implementation
XDRDecoder::XDRDecoder(const uint8_t* data, size_t length)
    : data_(data)
    , length_(length)
    , position_(0) {
}

uint32_t XDRDecoder::decode_uint32() {
    check_alignment(4);
    if (position_ + 4 > length_) {
        throw std::runtime_error("Buffer underflow in decode_uint32");
    }
    
    uint32_t net_value;
    std::memcpy(&net_value, data_ + position_, 4);
    position_ += 4;
    return ntohl(net_value);
}

uint64_t XDRDecoder::decode_uint64() {
    check_alignment(8);
    uint64_t high = decode_uint32();
    uint64_t low = decode_uint32();
    return (high << 32) | low;
}

std::string XDRDecoder::decode_string() {
    uint32_t length = decode_uint32();
    if (position_ + length > length_) {
        throw std::runtime_error("Buffer underflow in decode_string");
    }
    
    std::string result(reinterpret_cast<const char*>(data_ + position_), length);
    position_ += length;
    check_alignment(4);
    return result;
}

std::vector<uint8_t> XDRDecoder::decode_opaque() {
    uint32_t length = decode_uint32();
    if (position_ + length > length_) {
        throw std::runtime_error("Buffer underflow in decode_opaque");
    }
    
    std::vector<uint8_t> result(data_ + position_, data_ + position_ + length);
    position_ += length;
    check_alignment(4);
    return result;
}

bool XDRDecoder::decode_bool() {
    return decode_uint32() != 0;
}

bool XDRDecoder::has_more() const {
    return position_ < length_;
}

void XDRDecoder::check_alignment(size_t alignment) {
    size_t padding = (alignment - (position_ % alignment)) % alignment;
    position_ += padding;
}

} // namespace fuse_t 