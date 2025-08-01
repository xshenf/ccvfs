#include "ccvfs_algorithm.h"

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
 * RLE Compression Algorithm (Improved)
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
        
        // Count consecutive identical bytes
        while (i + count < input_len && input[i + count] == byte && count < 255) {
            count++;
        }
        
        if (count >= 3 || (count == 2 && byte == 0)) {
            // Use RLE encoding for runs of 3+ or runs of 2 zeros
            if (j + 3 > output_len) return -1;
            output[j++] = 0xFF;  // RLE marker
            output[j++] = byte;  // Repeated byte
            output[j++] = count; // Count
        } else {
            // Copy literal bytes
            if (byte == 0xFF) {
                // Escape the RLE marker
                if (j + 2 > output_len) return -1;
                output[j++] = 0xFF;
                output[j++] = 0x00;  // Escaped marker
            } else {
                if (j + 1 > output_len) return -1;
                output[j++] = byte;
            }
            count = 1;
        }
        
        i += count;
    }
    
    CCVFS_DEBUG("RLE compressed %d bytes to %d bytes", input_len, j);
    return j;
}

static int rle_decompress(const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    int i = 0, j = 0;
    
    CCVFS_DEBUG("RLE decompressing %d bytes", input_len);
    
    while (i < input_len && j < output_len) {
        if (input[i] == 0xFF) {
            if (i + 1 >= input_len) break;
            
            if (input[i + 1] == 0x00) {
                // Escaped marker
                output[j++] = 0xFF;
                i += 2;
            } else if (i + 2 < input_len) {
                // RLE sequence
                unsigned char byte = input[i + 1];
                int count = input[i + 2];
                
                if (j + count > output_len) {
                    CCVFS_ERROR("Output buffer overflow in RLE decompression");
                    return -1;
                }
                
                while (count-- > 0 && j < output_len) {
                    output[j++] = byte;
                }
                i += 3;
            } else {
                break;
            }
        } else {
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

static EncryptAlgorithm xor_algorithm = {
    "xor",
    xor_encrypt,
    xor_decrypt,
    16  // Default key size
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
    
    // Register builtin encryption algorithms
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = &xor_algorithm;
    
    g_algorithms_initialized = 1;
    
    CCVFS_INFO("Initialized %d compression and %d encryption algorithms", 
               g_compress_algorithm_count, g_encrypt_algorithm_count);
}