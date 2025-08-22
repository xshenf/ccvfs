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

// Global algorithm registries
CompressAlgorithm *g_compress_algorithms[CCVFS_MAX_ALGORITHMS];
EncryptAlgorithm *g_encrypt_algorithms[CCVFS_MAX_ALGORITHMS];
int g_compress_algorithm_count = 0;
int g_encrypt_algorithm_count = 0;
int g_algorithms_initialized = 0;

/*
 * Find compression algorithm by name
 */
CompressAlgorithm* ccvfs_find_compress_algorithm(const char *name) {
    int i;
    
    if (!name) return NULL;
    
    // Initialize builtin algorithms if not done
    if (!g_algorithms_initialized) {
        ccvfs_init_builtin_algorithms();
    }
    
    for (i = 0; i < g_compress_algorithm_count; i++) {
        if (g_compress_algorithms[i] && 
            strcmp(g_compress_algorithms[i]->name, name) == 0) {
            return g_compress_algorithms[i];
        }
    }
    
    CCVFS_ERROR("Compression algorithm '%s' not found", name);
    return NULL;
}

/*
 * Find encryption algorithm by name
 */
EncryptAlgorithm* ccvfs_find_encrypt_algorithm(const char *name) {
    int i;
    
    if (!name) return NULL;
    
    // Initialize builtin algorithms if not done
    if (!g_algorithms_initialized) {
        ccvfs_init_builtin_algorithms();
    }
    
    for (i = 0; i < g_encrypt_algorithm_count; i++) {
        if (g_encrypt_algorithms[i] && 
            strcmp(g_encrypt_algorithms[i]->name, name) == 0) {
            return g_encrypt_algorithms[i];
        }
    }
    
    CCVFS_ERROR("Encryption algorithm '%s' not found", name);
    return NULL;
}

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
#endif // HAVE_OPENSSL

/*
 * 初始化内置算法
 */
void ccvfs_init_builtin_algorithms(void) {
    if (g_algorithms_initialized) return;
    
    // 初始化数组为空
    memset(g_compress_algorithms, 0, sizeof(g_compress_algorithms));
    memset(g_encrypt_algorithms, 0, sizeof(g_encrypt_algorithms));
    
#ifdef HAVE_ZLIB
    // 注册zlib压缩算法
    g_compress_algorithms[g_compress_algorithm_count++] = &zlib_algorithm;
    CCVFS_DEBUG("Registered zlib compression algorithm");
#endif
    
#ifdef HAVE_OPENSSL
    // 初始化OpenSSL
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
    
    // 注册AES-256加密算法
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = &aes256_algorithm;
    CCVFS_DEBUG("Registered AES-256 encryption algorithm");
#endif
    
    g_algorithms_initialized = 1;
    
    CCVFS_DEBUG("Initialized %d compression and %d encryption algorithms",
               g_compress_algorithm_count, g_encrypt_algorithm_count);
}

/*
 * Register custom compression algorithm
 */
int sqlite3_ccvfs_register_compress_algorithm(CompressAlgorithm *algorithm) {
    if (!algorithm || !algorithm->name || !algorithm->compress || 
        !algorithm->decompress || !algorithm->get_max_compressed_size) {
        CCVFS_ERROR("Invalid compression algorithm structure");
        return SQLITE_ERROR;
    }
    
    // Initialize builtin algorithms if not done
    if (!g_algorithms_initialized) {
        ccvfs_init_builtin_algorithms();
    }
    
    if (ccvfs_find_compress_algorithm(algorithm->name)) {
        CCVFS_ERROR("Compression algorithm '%s' already exists", algorithm->name);
        return SQLITE_ERROR;
    }
    
    if (g_compress_algorithm_count >= CCVFS_MAX_ALGORITHMS) {
        CCVFS_ERROR("Maximum number of compression algorithms reached");
        return SQLITE_ERROR;
    }
    
    g_compress_algorithms[g_compress_algorithm_count++] = algorithm;
    CCVFS_DEBUG("Registered compression algorithm: %s", algorithm->name);
    return SQLITE_OK;
}

/*
 * Register custom encryption algorithm
 */
int sqlite3_ccvfs_register_encrypt_algorithm(EncryptAlgorithm *algorithm) {
    if (!algorithm || !algorithm->name || !algorithm->encrypt || 
        !algorithm->decrypt || algorithm->key_size <= 0) {
        CCVFS_ERROR("Invalid encryption algorithm structure");
        return SQLITE_ERROR;
    }
    
    // Initialize builtin algorithms if not done
    if (!g_algorithms_initialized) {
        ccvfs_init_builtin_algorithms();
    }
    
    if (ccvfs_find_encrypt_algorithm(algorithm->name)) {
        CCVFS_ERROR("Encryption algorithm '%s' already exists", algorithm->name);
        return SQLITE_ERROR;
    }
    
    if (g_encrypt_algorithm_count >= CCVFS_MAX_ALGORITHMS) {
        CCVFS_ERROR("Maximum number of encryption algorithms reached");
        return SQLITE_ERROR;
    }
    
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = algorithm;
    CCVFS_DEBUG("Registered encryption algorithm: %s", algorithm->name);
    return SQLITE_OK;
}

/*
 * List available compression algorithms
 */
int ccvfs_list_compress_algorithms(char *buffer, int buffer_size) {
    int pos = 0;
    
    // Initialize builtin algorithms if not done
    if (!g_algorithms_initialized) {
        ccvfs_init_builtin_algorithms();
    }
    
    for (int i = 0; i < g_compress_algorithm_count && pos < buffer_size - 1; i++) {
        if (g_compress_algorithms[i]) {
            pos += snprintf(buffer + pos, buffer_size - pos, 
                              "%s%s", (i > 0) ? "," : "", g_compress_algorithms[i]->name);
        }
    }
    buffer[pos] = '\0';
    return pos;
}

/*
 * List available encryption algorithms
 */
int ccvfs_list_encrypt_algorithms(char *buffer, int buffer_size) {
    int pos = 0;
    
    // Initialize builtin algorithms if not done
    if (!g_algorithms_initialized) {
        ccvfs_init_builtin_algorithms();
    }
    
    for (int i = 0; i < g_encrypt_algorithm_count && pos < buffer_size - 1; i++) {
        if (g_encrypt_algorithms[i]) {
            pos += snprintf(buffer + pos, buffer_size - pos, 
                              "%s%s", (i > 0) ? "," : "", g_encrypt_algorithms[i]->name);
        }
    }
    buffer[pos] = '\0';
    return pos;
}