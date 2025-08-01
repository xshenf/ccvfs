#include "compress_vfs.h"
#include "ccvfs_algorithm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_DATA_SIZE 1024
#define BUFFER_SIZE (TEST_DATA_SIZE * 2)

// Test data patterns
typedef struct {
    const char* name;
    void (*generate)(unsigned char* data, int size);
} TestPattern;

void generate_random_data(unsigned char* data, int size) {
    for (int i = 0; i < size; i++) {
        data[i] = rand() % 256;
    }
}

void generate_repetitive_data(unsigned char* data, int size) {
    for (int i = 0; i < size; i++) {
        data[i] = (i / 10) % 256;
    }
}

void generate_sparse_data(unsigned char* data, int size) {
    memset(data, 0, size);
    for (int i = 0; i < size; i += 50) {
        data[i] = rand() % 256;
    }
}

void generate_text_like_data(unsigned char* data, int size) {
    const char* text = "The quick brown fox jumps over the lazy dog. ";
    int text_len = strlen(text);
    for (int i = 0; i < size; i++) {
        data[i] = text[i % text_len];
    }
}

TestPattern test_patterns[] = {
    {"Random", generate_random_data},
    {"Repetitive", generate_repetitive_data},
    {"Sparse", generate_sparse_data},
    {"Text-like", generate_text_like_data}
};

int test_compression_algorithm(const char* algo_name, TestPattern* pattern) {
    CompressAlgorithm* algo = ccvfs_find_compress_algorithm(algo_name);
    if (!algo) {
        printf("  Algorithm '%s' not found\n", algo_name);
        return -1;
    }
    
    unsigned char input[TEST_DATA_SIZE];
    unsigned char compressed[BUFFER_SIZE];
    unsigned char decompressed[BUFFER_SIZE];
    
    // Generate test data
    pattern->generate(input, TEST_DATA_SIZE);
    
    // Test compression
    clock_t start = clock();
    int compressed_size = algo->compress(input, TEST_DATA_SIZE, compressed, BUFFER_SIZE, 6);
    clock_t compress_time = clock() - start;
    
    if (compressed_size < 0) {
        printf("  %s: Compression failed\n", algo_name);
        return -1;
    }
    
    // Test decompression
    start = clock();
    int decompressed_size = algo->decompress(compressed, compressed_size, decompressed, BUFFER_SIZE);
    clock_t decompress_time = clock() - start;
    
    if (decompressed_size != TEST_DATA_SIZE) {
        printf("  %s: Decompression size mismatch (expected %d, got %d)\n", 
                algo_name, TEST_DATA_SIZE, decompressed_size);
        return -1;
    }
    
    // Verify data integrity
    if (memcmp(input, decompressed, TEST_DATA_SIZE) != 0) {
        printf("  %s: Data integrity check failed\n", algo_name);
        return -1;
    }
    
    // Calculate statistics
    double compression_ratio = (double)compressed_size / TEST_DATA_SIZE;
    double compress_speed = (double)TEST_DATA_SIZE / ((double)compress_time / CLOCKS_PER_SEC);
    double decompress_speed = (double)TEST_DATA_SIZE / ((double)decompress_time / CLOCKS_PER_SEC);
    
    printf("  %-8s: %4d -> %4d bytes (%.1f%%) | Compress: %.0f KB/s | Decompress: %.0f KB/s\n",
           algo_name, TEST_DATA_SIZE, compressed_size, compression_ratio * 100,
           compress_speed / 1024, decompress_speed / 1024);
    
    return 0;
}

int test_encryption_algorithm(const char* algo_name) {
    EncryptAlgorithm* algo = ccvfs_find_encrypt_algorithm(algo_name);
    if (!algo) {
        printf("  Algorithm '%s' not found\n", algo_name);
        return -1;
    }
    
    unsigned char input[TEST_DATA_SIZE];
    unsigned char encrypted[BUFFER_SIZE];
    unsigned char decrypted[BUFFER_SIZE];
    unsigned char key[64] = {0};
    
    // Generate test data and key
    generate_text_like_data(input, TEST_DATA_SIZE);
    for (int i = 0; i < algo->key_size && i < 64; i++) {
        key[i] = i * 7 + 13;  // Simple key pattern
    }
    
    // Test encryption
    clock_t start = clock();
    int encrypted_size = algo->encrypt(key, algo->key_size, input, TEST_DATA_SIZE, encrypted, BUFFER_SIZE);
    clock_t encrypt_time = clock() - start;
    
    if (encrypted_size != TEST_DATA_SIZE) {
        printf("  %s: Encryption size mismatch\n", algo_name);
        return -1;
    }
    
    // Test decryption
    start = clock();
    int decrypted_size = algo->decrypt(key, algo->key_size, encrypted, encrypted_size, decrypted, BUFFER_SIZE);
    clock_t decrypt_time = clock() - start;
    
    if (decrypted_size != TEST_DATA_SIZE) {
        printf("  %s: Decryption size mismatch\n", algo_name);
        return -1;
    }
    
    // Verify data integrity
    if (memcmp(input, decrypted, TEST_DATA_SIZE) != 0) {
        printf("  %s: Data integrity check failed\n", algo_name);
        return -1;
    }
    
    // Check that encrypted data is different from input
    if (memcmp(input, encrypted, TEST_DATA_SIZE) == 0) {
        printf("  %s: Warning - encrypted data identical to input\n", algo_name);
    }
    
    // Calculate statistics
    double encrypt_speed = (double)TEST_DATA_SIZE / ((double)encrypt_time / CLOCKS_PER_SEC);
    double decrypt_speed = (double)TEST_DATA_SIZE / ((double)decrypt_time / CLOCKS_PER_SEC);
    
    printf("  %-8s: %d-bit key | Encrypt: %.0f KB/s | Decrypt: %.0f KB/s\n",
           algo_name, algo->key_size * 8, encrypt_speed / 1024, decrypt_speed / 1024);
    
    return 0;
}

int main() {
    printf("CCVFS Algorithm Test Suite\n");
    printf("=========================\n\n");
    
    srand(time(NULL));
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // List available algorithms
    char buffer[256];
    if (ccvfs_list_compress_algorithms(buffer, sizeof(buffer)) > 0) {
        printf("Available compression algorithms: %s\n", buffer);
    }
    if (ccvfs_list_encrypt_algorithms(buffer, sizeof(buffer)) > 0) {
        printf("Available encryption algorithms: %s\n", buffer);
    }
    printf("\n");
    
    // Test compression algorithms with different data patterns
    printf("Compression Algorithm Performance:\n");
    printf("----------------------------------\n");
    
    int pattern_count = sizeof(test_patterns) / sizeof(test_patterns[0]);
    const char* compression_algos[] = {"rle", "lz4", "zlib"};
    int compression_algo_count = sizeof(compression_algos) / sizeof(compression_algos[0]);
    
    for (int p = 0; p < pattern_count; p++) {
        printf("\n%s Data Pattern:\n", test_patterns[p].name);
        for (int a = 0; a < compression_algo_count; a++) {
            test_compression_algorithm(compression_algos[a], &test_patterns[p]);
        }
    }
    
    // Test encryption algorithms
    printf("\n\nEncryption Algorithm Performance:\n");
    printf("---------------------------------\n");
    const char* encryption_algos[] = {"xor", "aes128", "chacha20"};
    int encryption_algo_count = sizeof(encryption_algos) / sizeof(encryption_algos[0]);
    
    for (int a = 0; a < encryption_algo_count; a++) {
        test_encryption_algorithm(encryption_algos[a]);
    }
    
    printf("\nAll tests completed successfully!\n");
    return 0;
}