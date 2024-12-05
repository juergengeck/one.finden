#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include "util/logger.hpp"

namespace fused {

enum class EncryptionMode {
    NONE,       // No encryption
    AES_256_GCM,// AES-256 in GCM mode
    AES_256_CBC,// AES-256 in CBC mode
    CHACHA20   // ChaCha20-Poly1305
};

struct EncryptionKey {
    std::vector<uint8_t> key;
    std::vector<uint8_t> iv;
    EncryptionMode mode;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point expires;
    bool active{true};
};

class EncryptionManager {
public:
    EncryptionManager();
    ~EncryptionManager();

    // Initialize encryption system
    bool initialize();

    // Key management
    bool generate_key(EncryptionMode mode, std::string& key_id);
    bool rotate_key(const std::string& key_id);
    bool revoke_key(const std::string& key_id);
    bool export_key(const std::string& key_id, std::vector<uint8_t>& key_data);

    // Encryption operations
    bool encrypt_data(const std::string& key_id,
                     const std::vector<uint8_t>& plaintext,
                     std::vector<uint8_t>& ciphertext);
    bool decrypt_data(const std::string& key_id,
                     const std::vector<uint8_t>& ciphertext,
                     std::vector<uint8_t>& plaintext);

    // File operations
    bool encrypt_file(const std::string& key_id,
                     const std::string& input_path,
                     const std::string& output_path);
    bool decrypt_file(const std::string& key_id,
                     const std::string& input_path,
                     const std::string& output_path);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, EncryptionKey> keys_;
    EVP_CIPHER_CTX* cipher_ctx_;

    static constexpr size_t KEY_SIZE = 32;  // 256 bits
    static constexpr size_t IV_SIZE = 16;   // 128 bits
    static constexpr size_t TAG_SIZE = 16;  // 128 bits
    static constexpr auto KEY_VALIDITY = std::chrono::hours(24 * 90);  // 90 days

    // Key operations
    bool create_key(EncryptionMode mode, EncryptionKey& key);
    bool validate_key(const EncryptionKey& key);
    void cleanup_expired_keys();

    // Encryption helpers
    bool init_cipher(const EncryptionKey& key, bool encrypt);
    bool update_cipher(const uint8_t* input, size_t length,
                      std::vector<uint8_t>& output);
    bool finalize_cipher(std::vector<uint8_t>& output);

    // File helpers
    bool process_file(const std::string& input_path,
                     const std::string& output_path,
                     const EncryptionKey& key,
                     bool encrypt);

    // Security helpers
    void secure_zero(void* ptr, size_t size);
    bool generate_random(uint8_t* buffer, size_t size);
    std::string generate_key_id();
};

} // namespace fuse_t 