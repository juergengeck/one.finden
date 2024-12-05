#include "encryption_manager.hpp"
#include <openssl/rand.h>
#include <openssl/err.h>
#include <fstream>
#include <iomanip>

namespace fused {

EncryptionManager::EncryptionManager() : cipher_ctx_(nullptr) {
    // Initialize OpenSSL
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
    cipher_ctx_ = EVP_CIPHER_CTX_new();
}

EncryptionManager::~EncryptionManager() {
    // Cleanup OpenSSL
    if (cipher_ctx_) {
        EVP_CIPHER_CTX_free(cipher_ctx_);
    }
    EVP_cleanup();
    ERR_free_strings();

    // Securely clear keys
    for (auto& [id, key] : keys_) {
        secure_zero(key.key.data(), key.key.size());
        secure_zero(key.iv.data(), key.iv.size());
    }
}

bool EncryptionManager::initialize() {
    LOG_INFO("Initializing encryption manager");
    
    // Initialize secure random
    if (RAND_load_file("/dev/urandom", 32) != 32) {
        LOG_ERROR("Failed to initialize secure random");
        return false;
    }

    return true;
}

bool EncryptionManager::generate_key(EncryptionMode mode, std::string& key_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    EncryptionKey key;
    if (!create_key(mode, key)) {
        return false;
    }

    key_id = generate_key_id();
    keys_[key_id] = std::move(key);
    
    LOG_INFO("Generated new key: {}", key_id);
    return true;
}

bool EncryptionManager::rotate_key(const std::string& key_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end()) {
        return false;
    }

    // Create new key with same mode
    EncryptionKey new_key;
    if (!create_key(it->second.mode, new_key)) {
        return false;
    }

    // Replace old key
    secure_zero(it->second.key.data(), it->second.key.size());
    secure_zero(it->second.iv.data(), it->second.iv.size());
    it->second = std::move(new_key);

    LOG_INFO("Rotated key: {}", key_id);
    return true;
}

bool EncryptionManager::revoke_key(const std::string& key_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end()) {
        return false;
    }

    // Securely clear key material
    secure_zero(it->second.key.data(), it->second.key.size());
    secure_zero(it->second.iv.data(), it->second.iv.size());
    
    keys_.erase(it);
    LOG_INFO("Revoked key: {}", key_id);
    return true;
}

bool EncryptionManager::encrypt_data(const std::string& key_id,
                                   const std::vector<uint8_t>& plaintext,
                                   std::vector<uint8_t>& ciphertext) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end() || !it->second.active) {
        return false;
    }

    // Initialize cipher
    if (!init_cipher(it->second, true)) {
        return false;
    }

    // Allocate output buffer with space for tag
    ciphertext.resize(plaintext.size() + TAG_SIZE);
    size_t out_len = 0;

    // Encrypt data
    if (!update_cipher(plaintext.data(), plaintext.size(), ciphertext)) {
        return false;
    }

    // Finalize encryption and add tag
    if (!finalize_cipher(ciphertext)) {
        return false;
    }

    return true;
}

bool EncryptionManager::decrypt_data(const std::string& key_id,
                                   const std::vector<uint8_t>& ciphertext,
                                   std::vector<uint8_t>& plaintext) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end() || !it->second.active) {
        return false;
    }

    // Initialize cipher
    if (!init_cipher(it->second, false)) {
        return false;
    }

    // Allocate output buffer
    plaintext.resize(ciphertext.size() - TAG_SIZE);

    // Decrypt data
    if (!update_cipher(ciphertext.data(), ciphertext.size() - TAG_SIZE, plaintext)) {
        return false;
    }

    // Set expected tag and finalize decryption
    if (!EVP_CIPHER_CTX_ctrl(cipher_ctx_, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                            const_cast<uint8_t*>(ciphertext.data() + ciphertext.size() - TAG_SIZE))) {
        return false;
    }

    if (!finalize_cipher(plaintext)) {
        return false;
    }

    return true;
}

bool EncryptionManager::encrypt_file(const std::string& key_id,
                                   const std::string& input_path,
                                   const std::string& output_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end() || !it->second.active) {
        return false;
    }

    return process_file(input_path, output_path, it->second, true);
}

bool EncryptionManager::decrypt_file(const std::string& key_id,
                                   const std::string& input_path,
                                   const std::string& output_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = keys_.find(key_id);
    if (it == keys_.end() || !it->second.active) {
        return false;
    }

    return process_file(input_path, output_path, it->second, false);
}

// Private methods
bool EncryptionManager::create_key(EncryptionMode mode, EncryptionKey& key) {
    key.mode = mode;
    key.key.resize(KEY_SIZE);
    key.iv.resize(IV_SIZE);

    // Generate random key and IV
    if (!generate_random(key.key.data(), KEY_SIZE) ||
        !generate_random(key.iv.data(), IV_SIZE)) {
        return false;
    }

    // Set validity period
    key.created = std::chrono::system_clock::now();
    key.expires = key.created + KEY_VALIDITY;
    key.active = true;

    return true;
}

bool EncryptionManager::validate_key(const EncryptionKey& key) {
    if (key.key.size() != KEY_SIZE || key.iv.size() != IV_SIZE) {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    return key.active && now < key.expires;
}

void EncryptionManager::cleanup_expired_keys() {
    auto now = std::chrono::system_clock::now();
    
    for (auto it = keys_.begin(); it != keys_.end();) {
        if (now >= it->second.expires) {
            secure_zero(it->second.key.data(), it->second.key.size());
            secure_zero(it->second.iv.data(), it->second.iv.size());
            it = keys_.erase(it);
        } else {
            ++it;
        }
    }
}

bool EncryptionManager::init_cipher(const EncryptionKey& key, bool encrypt) {
    const EVP_CIPHER* cipher = nullptr;
    
    switch (key.mode) {
        case EncryptionMode::AES_256_GCM:
            cipher = EVP_aes_256_gcm();
            break;
        case EncryptionMode::AES_256_CBC:
            cipher = EVP_aes_256_cbc();
            break;
        case EncryptionMode::CHACHA20:
            cipher = EVP_chacha20_poly1305();
            break;
        default:
            return false;
    }

    if (!EVP_CipherInit_ex(cipher_ctx_, cipher, nullptr,
                          key.key.data(), key.iv.data(), encrypt)) {
        return false;
    }

    return true;
}

bool EncryptionManager::update_cipher(const uint8_t* input, size_t length,
                                    std::vector<uint8_t>& output) {
    int out_len = 0;
    if (!EVP_CipherUpdate(cipher_ctx_, output.data(), &out_len,
                         input, length)) {
        return false;
    }

    output.resize(out_len);
    return true;
}

bool EncryptionManager::finalize_cipher(std::vector<uint8_t>& output) {
    int out_len = 0;
    if (!EVP_CipherFinal_ex(cipher_ctx_, output.data() + output.size(), &out_len)) {
        return false;
    }

    output.resize(output.size() + out_len);
    return true;
}

bool EncryptionManager::process_file(const std::string& input_path,
                                   const std::string& output_path,
                                   const EncryptionKey& key,
                                   bool encrypt) {
    std::ifstream input(input_path, std::ios::binary);
    std::ofstream output(output_path, std::ios::binary);
    
    if (!input || !output) {
        return false;
    }

    // Initialize cipher
    if (!init_cipher(key, encrypt)) {
        return false;
    }

    // Process file in chunks
    std::vector<uint8_t> buffer(4096);
    std::vector<uint8_t> out_buffer(4096 + TAG_SIZE);
    
    while (input.read(reinterpret_cast<char*>(buffer.data()), buffer.size())) {
        if (!update_cipher(buffer.data(), input.gcount(), out_buffer)) {
            return false;
        }
        output.write(reinterpret_cast<char*>(out_buffer.data()), out_buffer.size());
    }

    // Process final block
    if (!finalize_cipher(out_buffer)) {
        return false;
    }
    output.write(reinterpret_cast<char*>(out_buffer.data()), out_buffer.size());

    return true;
}

void EncryptionManager::secure_zero(void* ptr, size_t size) {
    volatile uint8_t* p = static_cast<uint8_t*>(ptr);
    while (size--) {
        *p++ = 0;
    }
}

bool EncryptionManager::generate_random(uint8_t* buffer, size_t size) {
    return RAND_bytes(buffer, size) == 1;
}

std::string EncryptionManager::generate_key_id() {
    uint8_t random[16];
    if (!generate_random(random, sizeof(random))) {
        return "";
    }

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t byte : random) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

} // namespace fuse_t 