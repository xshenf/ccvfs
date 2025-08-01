#include "ccvfs_algorithm.h"
#include <zlib.h>

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
 * RLE Compression Algorithm (Optimized)
 */
static int rle_compress(const unsigned char *input, int input_len, 
                       unsigned char *output, int output_len, int level) {
    int i = 0, j = 0;
    int count;
    
    CCVFS_DEBUG("RLE compressing %d bytes", input_len);
    
    if (output_len < input_len + input_len/2) {
        CCVFS_ERROR("Output buffer too small for RLE compression");
        return -1;
    }
    
    while (i < input_len) {
        unsigned char byte = input[i];
        count = 1;
        
        // Count consecutive identical bytes (optimized with bounds check)
        int max_run = input_len - i;
        if (max_run > 255) max_run = 255;
        
        while (count < max_run && input[i + count] == byte) {
            count++;
        }
        
        // Use RLE encoding for runs of 3+ or runs of 2 zeros
        if (count >= 3 || (count == 2 && byte == 0)) {
            if (j + 3 > output_len) return -1;
            output[j++] = 0xFF;  // RLE marker
            output[j++] = byte;  // Repeated byte
            output[j++] = count; // Count (guaranteed to be > 0)
            i += count;
        } else {
            // Copy literal bytes
            for (int k = 0; k < count; k++) {
                if (input[i] == 0xFF) {
                    // Escape the RLE marker
                    if (j + 2 > output_len) return -1;
                    output[j++] = 0xFF;
                    output[j++] = 0x00;  // Escaped marker
                } else {
                    if (j + 1 > output_len) return -1;
                    output[j++] = input[i];
                }
                i++;
            }
        }
    }
    
    CCVFS_DEBUG("RLE compressed %d bytes to %d bytes (%.1f%%)", 
                input_len, j, (j * 100.0) / input_len);
    return j;
}

static int rle_decompress(const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    int i = 0, j = 0;
    
    CCVFS_DEBUG("RLE decompressing %d bytes", input_len);
    
    if (!input || !output || input_len <= 0 || output_len <= 0) {
        CCVFS_ERROR("Invalid parameters for RLE decompression");
        return -1;
    }
    
    // Debug: Print first few bytes of input
    CCVFS_DEBUG("First 20 bytes of input: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", 
                input[0], input[1], input[2], input[3], input[4], 
                input[5], input[6], input[7], input[8], input[9],
                input[10], input[11], input[12], input[13], input[14],
                input[15], input[16], input[17], input[18], input[19]);
    
    while (i < input_len && j < output_len) {
        if (input[i] == 0xFF) {
            if (i + 1 >= input_len) {
                CCVFS_ERROR("Incomplete RLE sequence at end of input");
                break;
            }
            
            if (input[i + 1] == 0x00) {
                // Escaped marker
                if (j >= output_len) {
                    CCVFS_ERROR("Output buffer overflow in RLE decompression");
                    return -1;
                }
                output[j++] = 0xFF;
                i += 2;
                CCVFS_DEBUG("Decoded escaped 0xFF at position %d", i-2);
            } else if (i + 2 < input_len) {  // Need at least 3 bytes: marker + byte + count
                // RLE sequence
                unsigned char byte = input[i + 1];
                int count = input[i + 2];
                
                CCVFS_DEBUG("RLE sequence at position %d: byte=0x%02x, count=%d", i, byte, count);
                
                if (count == 0) {
                    CCVFS_ERROR("Invalid RLE count of 0 at position %d", i);
                    CCVFS_ERROR("This may indicate data corruption or encryption/decryption issue");
                    // Try to continue by treating this as a literal 0xFF
                    if (j >= output_len) {
                        CCVFS_ERROR("Output buffer overflow in RLE decompression");
                        return -1;
                    }
                    output[j++] = input[i++];
                    continue;
                }
                
                if (j + count > output_len) {
                    CCVFS_ERROR("Output buffer overflow in RLE decompression: need %d bytes, have %d", 
                               count, output_len - j);
                    return -1;
                }
                
                for (int k = 0; k < count && j < output_len; k++) {
                    output[j++] = byte;
                }
                i += 3;
            } else {
                CCVFS_ERROR("Incomplete RLE sequence at end of input: need 3 bytes, have %d", input_len - i);
                // Treat as literal byte
                if (j >= output_len) {
                    CCVFS_ERROR("Output buffer overflow in RLE decompression");
                    return -1;
                }
                output[j++] = input[i++];
            }
        } else {
            if (j >= output_len) {
                CCVFS_ERROR("Output buffer overflow in RLE decompression");
                return -1;
            }
            output[j++] = input[i++];
        }
    }
    
    CCVFS_DEBUG("RLE decompressed %d bytes to %d bytes", input_len, j);
    return j;
}

static int rle_get_max_compressed_size(int input_len) {
    // Worst case: every byte needs to be escaped
    return input_len * 2 + 16;
}

/*
 * XOR Encryption Algorithm (Simple)
 */
static int xor_encrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    int i;
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small for XOR encryption");
        return -1;
    }
    
    if (key_len == 0) {
        memcpy(output, input, input_len);
        return input_len;
    }
    
    for (i = 0; i < input_len; i++) {
        output[i] = input[i] ^ key[i % key_len];
    }
    
    return input_len;
}

static int xor_decrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    // XOR encryption is symmetric
    return xor_encrypt(key, key_len, input, input_len, output, output_len);
}

// Builtin algorithm instances
static CompressAlgorithm rle_algorithm = {
    "rle",
    rle_compress,
    rle_decompress,
    rle_get_max_compressed_size
};

/*
 * LZ4 Compression Algorithm Implementation (Simple LZ4-like)
 */
static int lz4_compress(const unsigned char *input, int input_len, 
                       unsigned char *output, int output_len, int level) {
    int i = 0, j = 0;
    int match_len, match_pos;
    int literal_len = 0;
    int literal_start = 0;
    
    CCVFS_DEBUG("LZ4 compressing %d bytes", input_len);
    
    if (output_len < input_len + input_len/8 + 64) {
        CCVFS_ERROR("Output buffer too small for LZ4 compression");
        return -1;
    }
    
    while (i < input_len) {
        match_len = 0;
        match_pos = 0;
        
        // Find best match in sliding window (simplified)
        for (int back = 1; back <= 65535 && back <= i; back++) {
            int len = 0;
            while (len < 255 && i + len < input_len && len < back &&
                   input[i + len] == input[i - back + len]) {
                len++;
            }
            if (len >= 4 && len > match_len) {
                match_len = len;
                match_pos = back;
            }
        }
        
        if (match_len >= 4) {
            // Output literals first
            if (literal_len > 0) {
                if (j + 1 + literal_len > output_len) return -1;
                output[j++] = (literal_len < 15) ? (literal_len << 4) : 0xF0;
                if (literal_len >= 15) {
                    int remaining = literal_len - 15;
                    while (remaining >= 255) {
                        if (j >= output_len) return -1;
                        output[j++] = 255;
                        remaining -= 255;
                    }
                    if (j >= output_len) return -1;
                    output[j++] = remaining;
                }
                
                if (j + literal_len > output_len) return -1;
                memcpy(&output[j], &input[literal_start], literal_len);
                j += literal_len;
                literal_len = 0;
            } else if (j > 0) {
                // Update previous token
                output[j-1] |= (match_len - 4 < 15) ? (match_len - 4) : 15;
            }
            
            // Output match
            if (j + 2 > output_len) return -1;
            output[j++] = match_pos & 0xFF;
            output[j++] = (match_pos >> 8) & 0xFF;
            
            if (match_len - 4 >= 15) {
                int remaining = match_len - 4 - 15;
                while (remaining >= 255) {
                    if (j >= output_len) return -1;
                    output[j++] = 255;
                    remaining -= 255;
                }
                if (j >= output_len) return -1;
                output[j++] = remaining;
            }
            
            i += match_len;
            literal_start = i;
        } else {
            literal_len++;
            i++;
        }
    }
    
    // Output remaining literals
    if (literal_len > 0) {
        if (j + 1 + literal_len > output_len) return -1;
        output[j++] = (literal_len < 15) ? (literal_len << 4) : 0xF0;
        if (literal_len >= 15) {
            int remaining = literal_len - 15;
            while (remaining >= 255) {
                if (j >= output_len) return -1;
                output[j++] = 255;
                remaining -= 255;
            }
            if (j >= output_len) return -1;
            output[j++] = remaining;
        }
        
        if (j + literal_len > output_len) return -1;
        memcpy(&output[j], &input[literal_start], literal_len);
        j += literal_len;
    }
    
    CCVFS_DEBUG("LZ4 compressed %d bytes to %d bytes", input_len, j);
    return j;
}

static int lz4_decompress(const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    int i = 0, j = 0;
    
    CCVFS_DEBUG("LZ4 decompressing %d bytes", input_len);
    
    if (!input || !output || input_len <= 0 || output_len <= 0) {
        CCVFS_ERROR("Invalid parameters for LZ4 decompression");
        return -1;
    }
    
    while (i < input_len && j < output_len) {
        if (i >= input_len) break;
        
        unsigned char token = input[i++];
        int literal_len = (token >> 4) & 0x0F;
        int match_len = token & 0x0F;
        
        // Read extended literal length
        if (literal_len == 15) {
            while (i < input_len) {
                unsigned char byte = input[i++];
                literal_len += byte;
                if (byte != 255) break;
                if (literal_len > output_len) {
                    CCVFS_ERROR("Literal length overflow in LZ4 decompression");
                    return -1;
                }
            }
        }
        
        // Copy literals
        if (literal_len > 0) {
            if (i + literal_len > input_len || j + literal_len > output_len) {
                CCVFS_ERROR("Buffer overflow in LZ4 decompression: literal_len=%d, remaining_input=%d, remaining_output=%d", 
                           literal_len, input_len - i, output_len - j);
                return -1;
            }
            memcpy(&output[j], &input[i], literal_len);
            i += literal_len;
            j += literal_len;
        }
        
        // Check if we have more data to process
        if (i >= input_len) break;
        
        // Need at least 2 bytes for match offset
        if (i + 1 >= input_len) {
            CCVFS_ERROR("Incomplete match offset in LZ4 decompression");
            return -1;
        }
        
        // Read match offset
        int match_pos = input[i] | (input[i+1] << 8);
        i += 2;
        
        // Read extended match length
        match_len += 4;
        if ((token & 0x0F) == 15) {
            while (i < input_len) {
                unsigned char byte = input[i++];
                match_len += byte;
                if (byte != 255) break;
                if (match_len > output_len) {
                    CCVFS_ERROR("Match length overflow in LZ4 decompression");
                    return -1;
                }
            }
        }
        
        // Validate match
        if (match_pos == 0 || match_pos > j) {
            CCVFS_ERROR("Invalid match offset in LZ4 decompression: offset=%d, position=%d", match_pos, j);
            return -1;
        }
        
        if (j + match_len > output_len) {
            CCVFS_ERROR("Match overflow in LZ4 decompression: match_len=%d, remaining_output=%d", 
                       match_len, output_len - j);
            return -1;
        }
        
        // Copy match (handle overlapping copies)
        int match_start = j - match_pos;
        for (int k = 0; k < match_len; k++) {
            output[j++] = output[match_start + (k % match_pos)];
        }
    }
    
    CCVFS_DEBUG("LZ4 decompressed %d bytes to %d bytes", input_len, j);
    return j;
}

static int lz4_get_max_compressed_size(int input_len) {
    return input_len + input_len/8 + 64;
}

/*
 * Real Zlib Compression Algorithm using actual zlib library
 */
static int zlib_compress(const unsigned char *input, int input_len, 
                        unsigned char *output, int output_len, int level) {
    CCVFS_DEBUG("Real Zlib compressing %d bytes (level %d)", input_len, level);
    
    // Set compression level (default to 6 if invalid)
    if (level < 1 || level > 9) level = 6;
    
    uLongf dest_len = output_len;
    int result = compress2(output, &dest_len, input, input_len, level);
    
    if (result != Z_OK) {
        CCVFS_ERROR("Zlib compression failed with error %d", result);
        return -1;
    }
    
    CCVFS_DEBUG("Real Zlib compressed %d bytes to %lu bytes (%.1f%%)", 
                input_len, dest_len, (double)dest_len / input_len * 100.0);
    return (int)dest_len;
}

static int zlib_decompress(const unsigned char *input, int input_len,
                          unsigned char *output, int output_len) {
    CCVFS_DEBUG("Real Zlib decompressing %d bytes", input_len);
    
    uLongf dest_len = output_len;
    int result = uncompress(output, &dest_len, input, input_len);
    
    if (result != Z_OK) {
        CCVFS_ERROR("Zlib decompression failed with error %d", result);
        return -1;
    }
    
    CCVFS_DEBUG("Real Zlib decompressed %d bytes to %lu bytes", input_len, dest_len);
    return (int)dest_len;
}

static int zlib_get_max_compressed_size(int input_len) {
    // Use zlib's recommended formula for maximum compressed size
    return compressBound(input_len);
}

// Add the new algorithm instances after the original ones
static CompressAlgorithm lz4_algorithm = {
    "lz4",
    lz4_compress,
    lz4_decompress,
    lz4_get_max_compressed_size
};

static CompressAlgorithm zlib_algorithm = {
    "zlib",
    zlib_compress,
    zlib_decompress,
    zlib_get_max_compressed_size
};

/*
 * AES-128 Encryption Algorithm (Simplified Implementation)
 */
static int aes128_encrypt(const unsigned char *key, int key_len,
                         const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    CCVFS_DEBUG("AES-128 encrypting %d bytes", input_len);
    
    if (key_len < 16) {
        CCVFS_ERROR("AES-128 requires 16-byte key");
        return -1;
    }
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small for AES-128 encryption");
        return -1;
    }
    
    // Simplified AES-like encryption (not real AES - for demonstration)
    // In production, use a proper AES implementation
    for (int i = 0; i < input_len; i++) {
        // Simple substitution and key mixing
        unsigned char byte = input[i];
        byte ^= key[i % 16];
        byte = ((byte << 1) | (byte >> 7)) & 0xFF;  // Rotate left
        byte ^= key[(i + 1) % 16];
        output[i] = byte;
    }
    
    CCVFS_DEBUG("AES-128 encrypted %d bytes", input_len);
    return input_len;
}

static int aes128_decrypt(const unsigned char *key, int key_len,
                         const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    CCVFS_DEBUG("AES-128 decrypting %d bytes", input_len);
    
    if (key_len < 16) {
        CCVFS_ERROR("AES-128 requires 16-byte key");
        return -1;
    }
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small for AES-128 decryption");
        return -1;
    }
    
    // Reverse the simplified encryption
    for (int i = 0; i < input_len; i++) {
        unsigned char byte = input[i];
        byte ^= key[(i + 1) % 16];
        byte = ((byte >> 1) | (byte << 7)) & 0xFF;  // Rotate right
        byte ^= key[i % 16];
        output[i] = byte;
    }
    
    CCVFS_DEBUG("AES-128 decrypted %d bytes", input_len);
    return input_len;
}

/*
 * ChaCha20 Encryption Algorithm (Simplified Implementation)
 */
static int chacha20_encrypt(const unsigned char *key, int key_len,
                           const unsigned char *input, int input_len,
                           unsigned char *output, int output_len) {
    CCVFS_DEBUG("ChaCha20 encrypting %d bytes", input_len);
    
    if (key_len < 32) {
        CCVFS_ERROR("ChaCha20 requires 32-byte key");
        return -1;
    }
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small for ChaCha20 encryption");
        return -1;
    }
    
    // Simplified ChaCha20-like stream cipher
    // In production, use a proper ChaCha20 implementation
    for (int i = 0; i < input_len; i++) {
        // Generate pseudo-random keystream byte
        uint32_t counter = i / 64;
        uint32_t position = i % 64;
        
        // Simple keystream generation (not real ChaCha20)
        uint32_t k0 = ((uint32_t*)key)[0] + counter;
        uint32_t k1 = ((uint32_t*)key)[1] ^ position;
        uint32_t k2 = ((uint32_t*)key)[2] + (counter << 8);
        uint32_t k3 = ((uint32_t*)key)[3] ^ (position << 16);
        
        uint32_t stream = k0 ^ k1 ^ k2 ^ k3;
        unsigned char keystream_byte = (stream >> (8 * (position % 4))) & 0xFF;
        
        output[i] = input[i] ^ keystream_byte;
    }
    
    CCVFS_DEBUG("ChaCha20 encrypted %d bytes", input_len);
    return input_len;
}

static int chacha20_decrypt(const unsigned char *key, int key_len,
                           const unsigned char *input, int input_len,
                           unsigned char *output, int output_len) {
    // ChaCha20 is symmetric
    return chacha20_encrypt(key, key_len, input, input_len, output, output_len);
}

static EncryptAlgorithm xor_algorithm = {
    "xor",
    xor_encrypt,
    xor_decrypt,
    16  // Default key size
};

static EncryptAlgorithm aes128_algorithm = {
    "aes128",
    aes128_encrypt,
    aes128_decrypt,
    16  // 128-bit key
};

static EncryptAlgorithm chacha20_algorithm = {
    "chacha20",
    chacha20_encrypt,
    chacha20_decrypt,
    32  // 256-bit key
};

/*
 * Initialize builtin algorithms
 */
void ccvfs_init_builtin_algorithms(void) {
    if (g_algorithms_initialized) return;
    
    // Initialize arrays to NULL
    memset(g_compress_algorithms, 0, sizeof(g_compress_algorithms));
    memset(g_encrypt_algorithms, 0, sizeof(g_encrypt_algorithms));
    
    // Register builtin compression algorithms
    g_compress_algorithms[g_compress_algorithm_count++] = &rle_algorithm;
    g_compress_algorithms[g_compress_algorithm_count++] = &lz4_algorithm;
    g_compress_algorithms[g_compress_algorithm_count++] = &zlib_algorithm;
    
    // Register builtin encryption algorithms
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = &xor_algorithm;
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = &aes128_algorithm;
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = &chacha20_algorithm;
    
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
    
    // Check if algorithm already exists
    if (ccvfs_find_compress_algorithm(algorithm->name)) {
        CCVFS_ERROR("Compression algorithm '%s' already exists", algorithm->name);
        return SQLITE_ERROR;
    }
    
    // Check if we have space
    if (g_compress_algorithm_count >= CCVFS_MAX_ALGORITHMS) {
        CCVFS_ERROR("Maximum number of compression algorithms reached");
        return SQLITE_ERROR;
    }
    
    // Register the algorithm
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
    
    // Check if algorithm already exists
    if (ccvfs_find_encrypt_algorithm(algorithm->name)) {
        CCVFS_ERROR("Encryption algorithm '%s' already exists", algorithm->name);
        return SQLITE_ERROR;
    }
    
    // Check if we have space
    if (g_encrypt_algorithm_count >= CCVFS_MAX_ALGORITHMS) {
        CCVFS_ERROR("Maximum number of encryption algorithms reached");
        return SQLITE_ERROR;
    }
    
    // Register the algorithm
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = algorithm;
    
    CCVFS_INFO("Registered encryption algorithm: %s", algorithm->name);
    return SQLITE_OK;
}

/*
 * List available compression algorithms
 */
int ccvfs_list_compress_algorithms(char *buffer, int buffer_size) {
    if (!buffer || buffer_size <= 0) {
        return -1;
    }
    
    // Initialize builtin algorithms if not done
    if (!g_algorithms_initialized) {
        ccvfs_init_builtin_algorithms();
    }
    
    int pos = 0;
    for (int i = 0; i < g_compress_algorithm_count && pos < buffer_size - 1; i++) {
        if (g_compress_algorithms[i]) {
            int len = snprintf(buffer + pos, buffer_size - pos, "%s%s", 
                              (i > 0) ? "," : "", g_compress_algorithms[i]->name);
            if (len > 0 && pos + len < buffer_size) {
                pos += len;
            } else {
                break;
            }
        }
    }
    
    buffer[pos] = '\0';
    return pos;
}

/*
 * List available encryption algorithms
 */
int ccvfs_list_encrypt_algorithms(char *buffer, int buffer_size) {
    if (!buffer || buffer_size <= 0) {
        return -1;
    }
    
    // Initialize builtin algorithms if not done
    if (!g_algorithms_initialized) {
        ccvfs_init_builtin_algorithms();
    }
    
    int pos = 0;
    for (int i = 0; i < g_encrypt_algorithm_count && pos < buffer_size - 1; i++) {
        if (g_encrypt_algorithms[i]) {
            int len = snprintf(buffer + pos, buffer_size - pos, "%s%s", 
                              (i > 0) ? "," : "", g_encrypt_algorithms[i]->name);
            if (len > 0 && pos + len < buffer_size) {
                pos += len;
            } else {
                break;
            }
        }
    }
    
    buffer[pos] = '\0';
    return pos;
}