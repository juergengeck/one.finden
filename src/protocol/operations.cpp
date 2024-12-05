#include "operations.hpp"
#include <stdexcept>

namespace fused {

// PutFH operation
void PutFHArgs::encode(XDREncoder& encoder) const {
    handle.encode(encoder);
}

void PutFHResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
}

void PutFHResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
}

// GetFH operation
void GetFHResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
    if (status.status == NFS4_OK) {
        handle.encode(encoder);
    }
}

void GetFHResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
    if (status.status == NFS4_OK) {
        handle.decode(decoder);
    }
}

// GetAttr operation
void GetAttrArgs::encode(XDREncoder& encoder) const {
    encoder.encode_uint32(attr_request);
}

void GetAttrResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
    if (status.status == NFS4_OK) {
        attrs.encode(encoder);
    }
}

void GetAttrResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
    if (status.status == NFS4_OK) {
        attrs.decode(decoder);
    }
}

// Lookup operation
void LookupArgs::encode(XDREncoder& encoder) const {
    encoder.encode_string(name);
}

void LookupResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
}

void LookupResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
}

// CREATE operation
void CreateArgs::encode(XDREncoder& encoder) const {
    encoder.encode_string(name);
    encoder.encode_uint32(static_cast<uint32_t>(type));
    attrs.encode(encoder);
}

void CreateResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
    if (status.status == NFS4_OK) {
        handle.encode(encoder);
        attrs.encode(encoder);
    }
}

void CreateResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
    if (status.status == NFS4_OK) {
        handle.decode(decoder);
        attrs.decode(decoder);
    }
}

// REMOVE operation
void RemoveArgs::encode(XDREncoder& encoder) const {
    encoder.encode_string(name);
}

void RemoveResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
}

void RemoveResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
}

// RENAME operation
void RenameArgs::encode(XDREncoder& encoder) const {
    encoder.encode_string(old_name);
    encoder.encode_string(new_name);
    dst_dir_handle.encode(encoder);
}

void RenameResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
}

void RenameResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
}

// SETATTR operation
void SetAttrArgs::encode(XDREncoder& encoder) const {
    attrs.encode(encoder);
    encoder.encode_uint32(attr_mask);
}

void SetAttrResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
    if (status.status == NFS4_OK) {
        attrs.encode(encoder);
    }
}

void SetAttrResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
    if (status.status == NFS4_OK) {
        attrs.decode(decoder);
    }
}

// READLINK operation
void ReadLinkResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
    if (status.status == NFS4_OK) {
        encoder.encode_string(link_content);
    }
}

void ReadLinkResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
    if (status.status == NFS4_OK) {
        link_content = decoder.decode_string();
    }
}

// SYMLINK operation
void SymLinkArgs::encode(XDREncoder& encoder) const {
    encoder.encode_string(name);
    encoder.encode_string(link_data);
    attrs.encode(encoder);
}

void SymLinkResult::encode(XDREncoder& encoder) const {
    status.encode(encoder);
    if (status.status == NFS4_OK) {
        handle.encode(encoder);
        attrs.encode(encoder);
    }
}

void SymLinkResult::decode(XDRDecoder& decoder) {
    status.decode(decoder);
    if (status.status == NFS4_OK) {
        handle.decode(decoder);
        attrs.decode(decoder);
    }
}

} // namespace fuse_t 