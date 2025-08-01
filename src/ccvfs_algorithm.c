#include "ccvfs_algorithm.h"
#include <zlib.h>

// 根据CMake检测结果包含其他压缩库
#ifdef HAVE_LZ4
#include <lz4.h>
#include <lz4hc.h>
#endif

#ifdef HAVE_LZMA
#include <lzma.h>
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

/*
 * Zlib Compression Algorithm using actual zlib library
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

#ifdef HAVE_LZ4
/*
 * LZ4 Compression Algorithm using real LZ4 library
 */
static int lz4_compress(const unsigned char *input, int input_len, 
                       unsigned char *output, int output_len, int level) {
    CCVFS_DEBUG("LZ4 compressing %d bytes (level %d)", input_len, level);
    
    int compressed_size;
    if (level > 3) {
        // Use LZ4HC for higher compression levels
        compressed_size = LZ4_compress_HC((const char*)input, (char*)output, input_len, output_len, level);
    } else {
        // Use fast LZ4 for lower levels
        compressed_size = LZ4_compress_default((const char*)input, (char*)output, input_len, output_len);
    }
    
    if (compressed_size <= 0) {
        CCVFS_ERROR("LZ4 compression failed");
        return -1;
    }
    
    CCVFS_DEBUG("LZ4 compressed %d bytes to %d bytes (%.1f%%)", 
                input_len, compressed_size, (double)compressed_size / input_len * 100.0);
    return compressed_size;
}

static int lz4_decompress(const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    CCVFS_DEBUG("LZ4 decompressing %d bytes", input_len);
    
    int decompressed_size = LZ4_decompress_safe((const char*)input, (char*)output, input_len, output_len);
    
    if (decompressed_size < 0) {
        CCVFS_ERROR("LZ4 decompression failed with error %d", decompressed_size);
        return -1;
    }
    
    CCVFS_DEBUG("LZ4 decompressed %d bytes to %d bytes", input_len, decompressed_size);
    return decompressed_size;
}

static int lz4_get_max_compressed_size(int input_len) {
    return LZ4_compressBound(input_len);
}

static CompressAlgorithm lz4_algorithm = {
    "lz4",
    lz4_compress,
    lz4_decompress,
    lz4_get_max_compressed_size
};
#endif // HAVE_LZ4

#ifdef HAVE_LZMA
/*
 * LZMA Compression Algorithm using real LZMA library
 */
static int lzma_compress(const unsigned char *input, int input_len, 
                        unsigned char *output, int output_len, int level) {
    CCVFS_DEBUG("LZMA compressing %d bytes (level %d)", input_len, level);
    
    // Set compression level (0-9, default 6)
    if (level < 0 || level > 9) level = 6;
    
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_easy_encoder(&strm, level, LZMA_CHECK_CRC64);
    
    if (ret != LZMA_OK) {
        CCVFS_ERROR("LZMA encoder initialization failed: %d", ret);
        return -1;
    }
    
    strm.next_in = input;
    strm.avail_in = input_len;
    strm.next_out = output;
    strm.avail_out = output_len;
    
    ret = lzma_code(&strm, LZMA_FINISH);
    
    if (ret != LZMA_STREAM_END) {
        lzma_end(&strm);
        CCVFS_ERROR("LZMA compression failed: %d", ret);
        return -1;
    }
    
    size_t compressed_size = output_len - strm.avail_out;
    lzma_end(&strm);
    
    CCVFS_DEBUG("LZMA compressed %d bytes to %zu bytes (%.1f%%)", 
                input_len, compressed_size, (double)compressed_size / input_len * 100.0);
    return (int)compressed_size;
}

static int lzma_decompress(const unsigned char *input, int input_len,
                          unsigned char *output, int output_len) {
    CCVFS_DEBUG("LZMA decompressing %d bytes", input_len);
    
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    
    if (ret != LZMA_OK) {
        CCVFS_ERROR("LZMA decoder initialization failed: %d", ret);
        return -1;
    }
    
    strm.next_in = input;
    strm.avail_in = input_len;
    strm.next_out = output;
    strm.avail_out = output_len;
    
    ret = lzma_code(&strm, LZMA_FINISH);
    
    if (ret != LZMA_STREAM_END && ret != LZMA_OK) {
        lzma_end(&strm);
        CCVFS_ERROR("LZMA decompression failed: %d", ret);
        return -1;
    }
    
    size_t decompressed_size = output_len - strm.avail_out;
    lzma_end(&strm);
    
    CCVFS_DEBUG("LZMA decompressed %d bytes to %zu bytes", input_len, decompressed_size);
    return (int)decompressed_size;
}

static int lzma_get_max_compressed_size(int input_len) {
    // LZMA worst case: input + 5% + 32KB
    return input_len + (input_len / 20) + (32 * 1024);
}

static CompressAlgorithm lzma_algorithm = {
    "lzma",
    lzma_compress,
    lzma_decompress,
    lzma_get_max_compressed_size
};
#endif // HAVE_LZMA

/*
 * Simple XOR Encryption Algorithm (for testing only)
 */
static int xor_encrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    CCVFS_DEBUG("XOR encrypting %d bytes", input_len);
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small for XOR encryption");
        return -1;
    }
    
    for (int i = 0; i < input_len; i++) {
        output[i] = input[i] ^ key[i % key_len];
    }
    
    return input_len;
}

static int xor_decrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    CCVFS_DEBUG("XOR decrypting %d bytes", input_len);
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small for XOR decryption");
        return -1;
    }
    
    // XOR encryption/decryption is symmetric
    for (int i = 0; i < input_len; i++) {
        output[i] = input[i] ^ key[i % key_len];
    }
    
    return input_len;
}

static EncryptAlgorithm xor_algorithm = {
    "xor",
    xor_encrypt,
    xor_decrypt,
    16  // 16-byte key
};

/*
 * Initialize builtin algorithms
 */
void ccvfs_init_builtin_algorithms(void) {
    if (g_algorithms_initialized) return;
    
    // Initialize arrays to NULL
    memset(g_compress_algorithms, 0, sizeof(g_compress_algorithms));
    memset(g_encrypt_algorithms, 0, sizeof(g_encrypt_algorithms));
    
    // Register real compression algorithms
    g_compress_algorithms[g_compress_algorithm_count++] = &zlib_algorithm;
    
#ifdef HAVE_LZ4
    g_compress_algorithms[g_compress_algorithm_count++] = &lz4_algorithm;
#endif
    
#ifdef HAVE_LZMA
    g_compress_algorithms[g_compress_algorithm_count++] = &lzma_algorithm;
#endif
    
    // Register simple encryption algorithms (for testing)
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = &xor_algorithm;
    
    g_algorithms_initialized = 1;
    
    CCVFS_INFO("Initialized %d compression and %d encryption algorithms", 
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
    CCVFS_INFO("Registered compression algorithm: %s", algorithm->name);
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
    CCVFS_INFO("Registered encryption algorithm: %s", algorithm->name);
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