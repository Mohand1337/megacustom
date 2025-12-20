#include "core/Crypto.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#else
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>
#endif

namespace megacustom {

// Base64 encoding table
static const char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Crypto::base64Encode(const std::vector<unsigned char>& data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned int n = data[i] << 16;
        if (i + 1 < data.size()) n |= data[i + 1] << 8;
        if (i + 2 < data.size()) n |= data[i + 2];

        result.push_back(BASE64_CHARS[(n >> 18) & 0x3F]);
        result.push_back(BASE64_CHARS[(n >> 12) & 0x3F]);
        result.push_back((i + 1 < data.size()) ? BASE64_CHARS[(n >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < data.size()) ? BASE64_CHARS[n & 0x3F] : '=');
    }

    return result;
}

std::vector<unsigned char> Crypto::base64Decode(const std::string& encoded) {
    static const int DECODE_TABLE[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::vector<unsigned char> result;
    result.reserve((encoded.size() * 3) / 4);

    int val = 0;
    int bits = 0;

    for (char c : encoded) {
        if (c == '=') break;
        int d = DECODE_TABLE[static_cast<unsigned char>(c)];
        if (d < 0) continue;

        val = (val << 6) | d;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<unsigned char>((val >> bits) & 0xFF));
        }
    }

    return result;
}

std::vector<unsigned char> Crypto::generateIV() {
    std::vector<unsigned char> iv(IV_SIZE);
    if (RAND_bytes(iv.data(), IV_SIZE) != 1) {
        throw CryptoException("Failed to generate random IV");
    }
    return iv;
}

std::vector<unsigned char> Crypto::generateSalt(size_t length) {
    std::vector<unsigned char> salt(length);
    if (RAND_bytes(salt.data(), static_cast<int>(length)) != 1) {
        throw CryptoException("Failed to generate random salt");
    }
    return salt;
}

std::string Crypto::deriveKey(const std::string& password,
                               const std::vector<unsigned char>& salt,
                               int iterations) {
    std::vector<unsigned char> key(KEY_SIZE);

    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt.size()),
                          iterations,
                          EVP_sha256(),
                          KEY_SIZE, key.data()) != 1) {
        throw CryptoException("Failed to derive key with PBKDF2");
    }

    return std::string(reinterpret_cast<char*>(key.data()), key.size());
}

std::string Crypto::deriveKey(const std::string& password,
                               const std::string& saltBase64,
                               int iterations) {
    std::vector<unsigned char> salt = base64Decode(saltBase64);
    return deriveKey(password, salt, iterations);
}

std::string Crypto::encrypt(const std::string& plaintext, const std::string& key) {
    if (key.size() != KEY_SIZE) {
        throw CryptoException("Key must be " + std::to_string(KEY_SIZE) + " bytes");
    }

    // Generate random IV
    std::vector<unsigned char> iv = generateIV();

    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw CryptoException("Failed to create cipher context");
    }

    // RAII cleanup
    struct CtxGuard {
        EVP_CIPHER_CTX* ctx;
        ~CtxGuard() { EVP_CIPHER_CTX_free(ctx); }
    } guard{ctx};

    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.data()),
                           iv.data()) != 1) {
        throw CryptoException("Failed to initialize encryption");
    }

    // Encrypt
    std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int ciphertextLen = 0;

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                          reinterpret_cast<const unsigned char*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        throw CryptoException("Encryption failed");
    }
    ciphertextLen = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        throw CryptoException("Encryption finalization failed");
    }
    ciphertextLen += len;
    ciphertext.resize(ciphertextLen);

    // Get auth tag
    std::vector<unsigned char> tag(TAG_SIZE);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data()) != 1) {
        throw CryptoException("Failed to get authentication tag");
    }

    // Combine: IV + ciphertext + tag
    std::vector<unsigned char> combined;
    combined.reserve(iv.size() + ciphertext.size() + tag.size());
    combined.insert(combined.end(), iv.begin(), iv.end());
    combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
    combined.insert(combined.end(), tag.begin(), tag.end());

    return base64Encode(combined);
}

std::string Crypto::decrypt(const std::string& ciphertextBase64, const std::string& key) {
    if (key.size() != KEY_SIZE) {
        throw CryptoException("Key must be " + std::to_string(KEY_SIZE) + " bytes");
    }

    // Decode from base64
    std::vector<unsigned char> combined = base64Decode(ciphertextBase64);

    if (combined.size() < IV_SIZE + TAG_SIZE) {
        throw CryptoException("Ciphertext too short");
    }

    // Extract IV, ciphertext, and tag
    std::vector<unsigned char> iv(combined.begin(), combined.begin() + IV_SIZE);
    std::vector<unsigned char> tag(combined.end() - TAG_SIZE, combined.end());
    std::vector<unsigned char> ciphertext(combined.begin() + IV_SIZE,
                                           combined.end() - TAG_SIZE);

    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw CryptoException("Failed to create cipher context");
    }

    struct CtxGuard {
        EVP_CIPHER_CTX* ctx;
        ~CtxGuard() { EVP_CIPHER_CTX_free(ctx); }
    } guard{ctx};

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.data()),
                           iv.data()) != 1) {
        throw CryptoException("Failed to initialize decryption");
    }

    // Decrypt
    std::vector<unsigned char> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintextLen = 0;

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          ciphertext.data(),
                          static_cast<int>(ciphertext.size())) != 1) {
        throw CryptoException("Decryption failed");
    }
    plaintextLen = len;

    // Set auth tag for verification
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                            const_cast<unsigned char*>(tag.data())) != 1) {
        throw CryptoException("Failed to set authentication tag");
    }

    // Finalize and verify tag
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        throw CryptoException("Authentication failed - data may be tampered");
    }
    plaintextLen += len;

    return std::string(reinterpret_cast<char*>(plaintext.data()), plaintextLen);
}

std::string Crypto::getMachineKey() {
    std::string machineId;

#ifdef _WIN32
    // Windows: Use machine GUID from registry
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256];
        DWORD size = sizeof(buffer);
        if (RegQueryValueExA(hKey, "MachineGuid", nullptr, nullptr,
                            reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS) {
            machineId = buffer;
        }
        RegCloseKey(hKey);
    }

    // Fallback: computer name + volume serial
    if (machineId.empty()) {
        char computerName[256];
        DWORD size = sizeof(computerName);
        GetComputerNameA(computerName, &size);
        machineId = computerName;

        DWORD serialNumber;
        if (GetVolumeInformationA("C:\\", nullptr, 0, &serialNumber,
                                  nullptr, nullptr, nullptr, 0)) {
            machineId += std::to_string(serialNumber);
        }
    }
#else
    // Linux: Try /etc/machine-id first
    std::ifstream machineIdFile("/etc/machine-id");
    if (machineIdFile.good()) {
        std::getline(machineIdFile, machineId);
    }

    // Fallback: hostname + user
    if (machineId.empty()) {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            machineId = hostname;
        }

        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            machineId += pw->pw_name;
        }
    }
#endif

    // Add app-specific constant
    machineId += "MegaCustomApp";

    // Hash to get consistent key
    std::vector<unsigned char> salt = {'M', 'e', 'g', 'a', 'C', 'u', 's', 't',
                                        'o', 'm', 'S', 'a', 'l', 't', '2', '4'};
    return deriveKey(machineId, salt, 10000);
}

// EncryptedData serialization methods
std::string Crypto::EncryptedData::toBase64() const {
    std::vector<unsigned char> combined;
    combined.reserve(iv.size() + ciphertext.size() + tag.size());
    combined.insert(combined.end(), iv.begin(), iv.end());
    combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
    combined.insert(combined.end(), tag.begin(), tag.end());
    return Crypto::base64Encode(combined);
}

Crypto::EncryptedData Crypto::EncryptedData::fromBase64(const std::string& encoded) {
    std::vector<unsigned char> combined = Crypto::base64Decode(encoded);

    if (combined.size() < IV_SIZE + TAG_SIZE) {
        throw CryptoException("Invalid encrypted data format");
    }

    EncryptedData data;
    data.iv.assign(combined.begin(), combined.begin() + IV_SIZE);
    data.tag.assign(combined.end() - TAG_SIZE, combined.end());
    data.ciphertext.assign(combined.begin() + IV_SIZE, combined.end() - TAG_SIZE);

    return data;
}

} // namespace megacustom
