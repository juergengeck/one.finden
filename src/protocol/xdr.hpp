#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace fused {

class XDREncoder {
public:
    void encode_uint32(uint32_t value);
    void encode_uint64(uint64_t value);
    void encode_string(const std::string& str);
    void encode_opaque(const std::vector<uint8_t>& data);
    void encode_bool(bool value);
    
    std::vector<uint8_t> get_buffer() const;

private:
    std::vector<uint8_t> buffer_;
    void pad_to_alignment(size_t alignment);
};

class XDRDecoder {
public:
    explicit XDRDecoder(const uint8_t* data, size_t length);
    
    uint32_t decode_uint32();
    uint64_t decode_uint64();
    std::string decode_string();
    std::vector<uint8_t> decode_opaque();
    bool decode_bool();
    
    bool has_more() const;

private:
    const uint8_t* data_;
    size_t length_;
    size_t position_;
    void check_alignment(size_t alignment);
};

} // namespace fused 