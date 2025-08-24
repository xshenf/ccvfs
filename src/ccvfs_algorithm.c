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
    
    if (key_len <= 0 || key_len > 64) {
        CCVFS_ERROR("AES-256 key length must be 1-64 bytes, got %d bytes", key_len);
        return -1;
    }
    
    // 密钥长度自动补全到32字节（重复填充）
    // Automatically expand key to 32 bytes using repetitive padding
    unsigned char expanded_key[32];
    if (key_len == 32) {
        // 已经是32字节，直接使用
        memcpy(expanded_key, key, 32);
        CCVFS_DEBUG("Using provided 32-byte key for AES-256");
    } else if (key_len < 32) {
        // 不足32字节，重复填充
        int pos = 0;
        while (pos < 32) {
            int copy_len = (32 - pos > key_len) ? key_len : (32 - pos);
            memcpy(expanded_key + pos, key, copy_len);
            pos += copy_len;
        }
        CCVFS_DEBUG("Expanded %d-byte key to 32 bytes using repetitive padding", key_len);
    } else {
        // 超过32字节，只取前32字节
        memcpy(expanded_key, key, 32);
        CCVFS_DEBUG("Truncated %d-byte key to 32 bytes for AES-256", key_len);
    }
    
    // AES需要IV，在输出的前16字节存储IV
    // AES-CBC需要额外空间用于IV和padding (最多15字节padding)
    if (output_len < input_len + 16 + 16) { // IV + 数据 + 最大padding
        CCVFS_ERROR("Output buffer too small for AES-256 encryption (need %d, got %d)", 
                    input_len + 32, output_len);
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
    
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, expanded_key, iv) != 1) {
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
    
    if (key_len <= 0 || key_len > 64) {
        CCVFS_ERROR("AES-256 key length must be 1-64 bytes, got %d bytes", key_len);
        return -1;
    }
    
    // 密钥长度自动补全到32字节（重复填充）
    // Automatically expand key to 32 bytes using repetitive padding
    unsigned char expanded_key[32];
    if (key_len == 32) {
        // 已经是32字节，直接使用
        memcpy(expanded_key, key, 32);
        CCVFS_DEBUG("Using provided 32-byte key for AES-256");
    } else if (key_len < 32) {
        // 不足32字节，重复填充
        int pos = 0;
        while (pos < 32) {
            int copy_len = (32 - pos > key_len) ? key_len : (32 - pos);
            memcpy(expanded_key + pos, key, copy_len);
            pos += copy_len;
        }
        CCVFS_DEBUG("Expanded %d-byte key to 32 bytes using repetitive padding", key_len);
    } else {
        // 超过32字节，只取前32字节
        memcpy(expanded_key, key, 32);
        CCVFS_DEBUG("Truncated %d-byte key to 32 bytes for AES-256", key_len);
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
    
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, expanded_key, iv) != 1) {
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

/*
 * AES-128-CBC 加密算法实现
 */
static int aes128_encrypt(const unsigned char *key, int key_len,
                         const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    CCVFS_DEBUG("AES-128 encrypting %d bytes", input_len);
    
    if (key_len <= 0 || key_len > 32) {
        CCVFS_ERROR("AES-128 key length must be 1-32 bytes, got %d bytes", key_len);
        return -1;
    }
    
    // 密钥长度自动补全到16字节（重复填充）
    // Automatically expand key to 16 bytes using repetitive padding
    unsigned char expanded_key[16];
    memset(expanded_key, 0, sizeof(expanded_key)); // 清零以确保干净的状态
    
    if (key_len == 16) {
        // 已经是16字节，直接使用
        memcpy(expanded_key, key, 16);
        CCVFS_DEBUG("Using provided 16-byte key for AES-128");
    } else if (key_len < 16) {
        // 不足16字节，重复填充
        for (int i = 0; i < 16; i++) {
            expanded_key[i] = key[i % key_len];
        }
        CCVFS_DEBUG("Expanded %d-byte key to 16 bytes using repetitive padding", key_len);
    } else {
        // 超过16字节，只取前16字节
        memcpy(expanded_key, key, 16);
        CCVFS_DEBUG("Truncated %d-byte key to 16 bytes for AES-128", key_len);
    }
    
    // AES需要IV，在输出的前16字节存储IV
    // AES-CBC需要额外空间用于IV和padding (最多15字节padding)
    if (output_len < input_len + 16 + 16) { // IV + 数据 + 最大padding
        CCVFS_ERROR("Output buffer too small for AES-128 encryption (need %d, got %d)", 
                    input_len + 32, output_len);
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
    
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, expanded_key, iv) != 1) {
        CCVFS_ERROR("Failed to initialize AES-128 encryption");
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
    
    CCVFS_DEBUG("AES-128 encrypted %d bytes to %d bytes", input_len, ciphertext_len + 16);
    return ciphertext_len + 16; // 包含IV
}

static int aes128_decrypt(const unsigned char *key, int key_len,
                         const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    CCVFS_DEBUG("AES-128 decrypting %d bytes", input_len);
    
    if (key_len <= 0 || key_len > 32) {
        CCVFS_ERROR("AES-128 key length must be 1-32 bytes, got %d bytes", key_len);
        return -1;
    }
    
    // 密钥长度自动补全到16字节（重复填充）
    // Automatically expand key to 16 bytes using repetitive padding
    unsigned char expanded_key[16];
    memset(expanded_key, 0, sizeof(expanded_key)); // 清零以确保干净的状态
    
    if (key_len == 16) {
        // 已经是16字节，直接使用
        memcpy(expanded_key, key, 16);
        CCVFS_DEBUG("Using provided 16-byte key for AES-128");
    } else if (key_len < 16) {
        // 不足16字节，重复填充
        for (int i = 0; i < 16; i++) {
            expanded_key[i] = key[i % key_len];
        }
        CCVFS_DEBUG("Expanded %d-byte key to 16 bytes using repetitive padding", key_len);
    } else {
        // 超过16字节，只取前16字节
        memcpy(expanded_key, key, 16);
        CCVFS_DEBUG("Truncated %d-byte key to 16 bytes for AES-128", key_len);
    }
    
    if (input_len < 16) {
        CCVFS_ERROR("Input too small to contain IV");
        return -1;
    }
    
    // 验证密文长度：必须是16字节的倍数（除了IV）
    int ciphertext_len = input_len - 16;
    if (ciphertext_len % 16 != 0) {
        CCVFS_ERROR("Invalid ciphertext length: %d (should be multiple of 16)", ciphertext_len);
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
    
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, expanded_key, iv) != 1) {
        CCVFS_ERROR("Failed to initialize AES-128 decryption");
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
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        CCVFS_ERROR("Failed to finalize AES-128 decryption: %s (OpenSSL error: %lu)", err_buf, err);
        CCVFS_ERROR("Decrypt debug: input_len=%d, ciphertext_len=%d, plaintext_len_so_far=%d", 
                   input_len, ciphertext_len, plaintext_len);
        
        // 打印一些数据用于调试
        CCVFS_ERROR("IV (first 8 bytes): %02X%02X%02X%02X%02X%02X%02X%02X",
                   iv[0], iv[1], iv[2], iv[3], iv[4], iv[5], iv[6], iv[7]);
        CCVFS_ERROR("Expanded key (first 8 bytes): %02X%02X%02X%02X%02X%02X%02X%02X",
                   expanded_key[0], expanded_key[1], expanded_key[2], expanded_key[3],
                   expanded_key[4], expanded_key[5], expanded_key[6], expanded_key[7]);
        
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    CCVFS_DEBUG("AES-128 decrypted %d bytes to %d bytes", input_len, plaintext_len);
    return plaintext_len;
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

