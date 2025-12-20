#ifndef MEGACUSTOM_CRYPTO_H
#define MEGACUSTOM_CRYPTO_H

#include <string>
#include <vector>
#include <stdexcept>

namespace megacustom {

/**
 * AES-256-GCM encryption class using OpenSSL.
 *
 * Ciphertext format: [12-byte IV][ciphertext][16-byte auth tag]
 */
class Crypto {
public:
    // Encryption result including IV and auth tag
    struct EncryptedData {
        std::vector<unsigned char> iv;       // 12 bytes
        std::vector<unsigned char> ciphertext;
        std::vector<unsigned char> tag;      // 16 bytes

        // Serialize to single buffer: IV + ciphertext + tag
        std::string toBase64() const;
        static EncryptedData fromBase64(const std::string& encoded);
    };

    /**
     * Encrypt plaintext using AES-256-GCM
     * @param plaintext Data to encrypt
     * @param key 32-byte encryption key (use deriveKey to generate)
     * @return Base64-encoded string containing IV + ciphertext + tag
     * @throws std::runtime_error on encryption failure
     */
    static std::string encrypt(const std::string& plaintext, const std::string& key);

    /**
     * Decrypt ciphertext using AES-256-GCM
     * @param ciphertext Base64-encoded string from encrypt()
     * @param key 32-byte encryption key (same key used for encryption)
     * @return Decrypted plaintext
     * @throws std::runtime_error on decryption failure or authentication failure
     */
    static std::string decrypt(const std::string& ciphertext, const std::string& key);

    /**
     * Generate cryptographically secure random IV
     * @return 12-byte random IV
     */
    static std::vector<unsigned char> generateIV();

    /**
     * Generate cryptographically secure random salt
     * @param length Salt length in bytes (default 32)
     * @return Random salt bytes
     */
    static std::vector<unsigned char> generateSalt(size_t length = 32);

    /**
     * Derive encryption key from password using PBKDF2-SHA256
     * @param password User password
     * @param salt Random salt (use generateSalt)
     * @param iterations PBKDF2 iterations (default 100000)
     * @return 32-byte derived key
     */
    static std::string deriveKey(const std::string& password,
                                  const std::vector<unsigned char>& salt,
                                  int iterations = 100000);

    /**
     * Derive encryption key from password with base64 salt
     * @param password User password
     * @param saltBase64 Base64-encoded salt
     * @param iterations PBKDF2 iterations
     * @return 32-byte derived key
     */
    static std::string deriveKey(const std::string& password,
                                  const std::string& saltBase64,
                                  int iterations = 100000);

    /**
     * Get machine-specific key for local credential storage
     * Uses combination of machine ID, username, and app-specific data
     * @return 32-byte machine-specific key
     */
    static std::string getMachineKey();

    // Constants
    static constexpr size_t KEY_SIZE = 32;    // 256 bits
    static constexpr size_t IV_SIZE = 12;     // 96 bits (GCM recommended)
    static constexpr size_t TAG_SIZE = 16;    // 128 bits
    static constexpr int DEFAULT_ITERATIONS = 100000;

private:
    // Internal helpers
    static std::string base64Encode(const std::vector<unsigned char>& data);
    static std::vector<unsigned char> base64Decode(const std::string& encoded);
};

class CryptoException : public std::runtime_error {
public:
    explicit CryptoException(const std::string& message)
        : std::runtime_error("Crypto error: " + message) {}
};

} // namespace megacustom

#endif // MEGACUSTOM_CRYPTO_H
