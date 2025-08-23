#include "ccvfs_algorithm.h"

// 只包含内置的压缩和加密库
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#endif

#ifdef HAVE_ZLIB
/*
 * Zlib 压缩算法实现
 */
static int zlib_compress(const unsigned char *input, int input_len, 
                        unsigned char *output, int output_len, int level) {
    CCVFS_DEBUG("Zlib compressing %d bytes (level %d)", input_len, level);
    
    // Set compression level (default to 6 if invalid)
    if (level < 1 || level > 9) level = 6;
    
    uLongf dest_len = output_len;
    int result = compress2(output, &dest_len, input, input_len, level);
    
    if (result != Z_OK) {
        CCVFS_ERROR("Zlib compression failed with error %d", result);
        return -1;
    }
    
    CCVFS_DEBUG("Zlib compressed %d bytes to %lu bytes (%.1f%%)", 
                input_len, dest_len, (double)dest_len / input_len * 100.0);
    return (int)dest_len;
}

static int zlib_decompress(const unsigned char *input, int input_len,
                          unsigned char *output, int output_len) {
    CCVFS_DEBUG("Zlib decompressing %d bytes", input_len);
    
    uLongf dest_len = output_len;
    int result = uncompress(output, &dest_len, input, input_len);
    
    if (result != Z_OK) {
        CCVFS_ERROR("Zlib decompression failed with error %d", result);
        return -1;
    }
    
    CCVFS_DEBUG("Zlib decompressed %d bytes to %lu bytes", input_len, dest_len);
    return (int)dest_len;
}

static int zlib_get_max_compressed_size(int input_len) {
    // Use zlib's recommended formula for maximum compressed size
    return compressBound(input_len);
}

static CompressAlgorithm zlib_algorithm = {
    "zlib",
    zlib_compress,
    zlib_decompress,
    zlib_get_max_compressed_size
};
#endif // HAVE_ZLIB

#ifdef HAVE_OPENSSL
/*
 * AES-256-CBC 加密算法实现
 */
static int aes256_encrypt(const unsigned char *key, int key_len,
                         const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    CCVFS_DEBUG("AES-256 encrypting %d bytes", input_len);
    
    if (key_len != 32) {
        CCVFS_ERROR("AES-256 requires 32-byte key, got %d bytes", key_len);
        return -1;
    }
    
    // AES需要IV，在输出的前16字节存储IV
    if (output_len < input_len + 16 + 16) { // IV + 数据 + padding
        CCVFS_ERROR("Output buffer too small for AES encryption");
        return -1;
    }
    
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        CCVFS_ERROR("Failed to create EVP context");
        return -1;
    }
    
    unsigned char iv[16];
    if (RAND_bytes(iv, sizeof(iv)) != 1) {
        CCVFS_ERROR("Failed to generate random IV");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    // 复制IV到输出缓冲区开头
    memcpy(output, iv, 16);
    
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1) {
        CCVFS_ERROR("Failed to initialize AES encryption");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    int len, ciphertext_len;
    if (EVP_EncryptUpdate(ctx, output + 16, &len, input, input_len) != 1) {
        CCVFS_ERROR("Failed to encrypt data");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len = len;
    
    if (EVP_EncryptFinal_ex(ctx, output + 16 + len, &len) != 1) {
        CCVFS_ERROR("Failed to finalize encryption");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    CCVFS_DEBUG("AES-256 encrypted %d bytes to %d bytes", input_len, ciphertext_len + 16);
    return ciphertext_len + 16; // 包含IV
}

static int aes256_decrypt(const unsigned char *key, int key_len,
                         const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    CCVFS_DEBUG("AES-256 decrypting %d bytes", input_len);
    
    if (key_len != 32) {
        CCVFS_ERROR("AES-256 requires 32-byte key, got %d bytes", key_len);
        return -1;
    }
    
    if (input_len < 16) {
        CCVFS_ERROR("Input too small to contain IV");
        return -1;
    }
    
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        CCVFS_ERROR("Failed to create EVP context");
        return -1;
    }
    
    // 从输入中提取IV
    const unsigned char *iv = input;
    const unsigned char *ciphertext = input + 16;
    int ciphertext_len = input_len - 16;
    
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1) {
        CCVFS_ERROR("Failed to initialize AES decryption");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    int len, plaintext_len;
    if (EVP_DecryptUpdate(ctx, output, &len, ciphertext, ciphertext_len) != 1) {
        CCVFS_ERROR("Failed to decrypt data");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len = len;
    
    if (EVP_DecryptFinal_ex(ctx, output + len, &len) != 1) {
        CCVFS_ERROR("Failed to finalize decryption");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    CCVFS_DEBUG("AES-256 decrypted %d bytes to %d bytes", input_len, plaintext_len);
    return plaintext_len;
}

static EncryptAlgorithm aes256_algorithm = {
    "aes256",
    aes256_encrypt,
    aes256_decrypt,
    32  // 32-byte key for AES-256
};

// 为AES-128添加简化实现（使用AES-256但只取前16字节密钥）
static int aes128_encrypt(const unsigned char *key, int key_len,
                         const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    if (key_len < 16) {
        CCVFS_ERROR("AES-128 requires at least 16-byte key, got %d bytes", key_len);
        return -1;
    }
    // 使用前16字节作为AES-128密钥，但需要扩展到32字节
    unsigned char expanded_key[32];
    memcpy(expanded_key, key, 16);
    memcpy(expanded_key + 16, key, 16);  // 简单重复前16字节
    return aes256_encrypt(expanded_key, 32, input, input_len, output, output_len);
}

static int aes128_decrypt(const unsigned char *key, int key_len,
                         const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    if (key_len < 16) {
        CCVFS_ERROR("AES-128 requires at least 16-byte key, got %d bytes", key_len);
        return -1;
    }
    // 使用前16字节作为AES-128密钥，但需要扩展到32字节
    unsigned char expanded_key[32];
    memcpy(expanded_key, key, 16);
    memcpy(expanded_key + 16, key, 16);  // 简单重复前16字节
    return aes256_decrypt(expanded_key, 32, input, input_len, output, output_len);
}

static EncryptAlgorithm aes128_algorithm = {
    "aes128",
    aes128_encrypt,
    aes128_decrypt,
    16  // 16-byte key for AES-128
};
#endif // HAVE_OPENSSL

// ============================================================================
// PREDEFINED ALGORITHM CONSTANTS
// ============================================================================

// 导出的算法常量
#ifdef HAVE_ZLIB
const CompressAlgorithm *CCVFS_COMPRESS_ZLIB = &zlib_algorithm;
#endif

#ifdef HAVE_OPENSSL
const EncryptAlgorithm *CCVFS_ENCRYPT_AES128 = &aes128_algorithm;
const EncryptAlgorithm *CCVFS_ENCRYPT_AES256 = &aes256_algorithm;
#endif

/*
 * 初始化内置算法
 */
void ccvfs_init_builtin_algorithms(void) {
    static int algorithms_initialized = 0;
    
    if (algorithms_initialized) return;
    
#ifdef HAVE_OPENSSL
    // 初始化OpenSSL
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
    CCVFS_DEBUG("Initialized OpenSSL algorithms");
#endif
    
    algorithms_initialized = 1;
    CCVFS_DEBUG("Built-in algorithms initialized");
}

