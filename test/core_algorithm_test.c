/*
 * Core Compression Algorithm Test
 * Tests compression/decompression algorithms directly without VFS
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccvfs_algorithm.h"

int test_algorithm_directly(const char* algo_name) {
    printf("\n=== Testing %s algorithm directly ===\n", algo_name);
    
    // Find algorithm
    CompressAlgorithm* algo = ccvfs_find_compress_algorithm(algo_name);
    if (!algo) {
        printf("❌ Algorithm '%s' not found\n", algo_name);
        return 0;
    }
    
    // Test data with high compression potential
    const char* test_data = 
        "This is test data for compression. "
        "This is test data for compression. "
        "This is test data for compression. "
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    
    int input_len = strlen(test_data);
    printf("Original data length: %d bytes\n", input_len);
    printf("Original data: %.80s...\n", test_data);
    
    // Get maximum compressed size
    int max_compressed_size = algo->get_max_compressed_size(input_len);
    printf("Maximum compressed size: %d bytes\n", max_compressed_size);
    
    // Allocate buffers
    unsigned char* compressed = malloc(max_compressed_size);
    unsigned char* decompressed = malloc(input_len + 1);
    
    if (!compressed || !decompressed) {
        printf("❌ Memory allocation failed\n");
        free(compressed);
        free(decompressed);
        return 0;
    }
    
    // Test compression
    printf("Compressing...\n");
    int compressed_size = algo->compress(
        (const unsigned char*)test_data, input_len,
        compressed, max_compressed_size, 6);
    
    if (compressed_size <= 0) {
        printf("❌ Compression failed: returned %d\n", compressed_size);
        free(compressed);
        free(decompressed);
        return 0;
    }
    
    printf("✅ Compression successful: %d → %d bytes (%.1f%%)\n", 
           input_len, compressed_size, (double)compressed_size / input_len * 100.0);
    
    // Test decompression
    printf("Decompressing...\n");
    int decompressed_size = algo->decompress(
        compressed, compressed_size,
        decompressed, input_len);
    
    if (decompressed_size != input_len) {
        printf("❌ Decompression failed: expected %d bytes, got %d\n", 
               input_len, decompressed_size);
        free(compressed);
        free(decompressed);
        return 0;
    }
    
    // Null-terminate for comparison
    decompressed[decompressed_size] = '\0';
    
    // Verify data integrity
    if (strcmp((char*)decompressed, test_data) == 0) {
        printf("✅ Data integrity verified: decompressed data matches original\n");
        printf("Decompressed data: %.80s...\n", (char*)decompressed);
        
        free(compressed);
        free(decompressed);
        return 1;
    } else {
        printf("❌ Data integrity failed: decompressed data differs\n");
        printf("Expected: %.80s...\n", test_data);
        printf("Got:      %.80s...\n", (char*)decompressed);
        
        free(compressed);
        free(decompressed);
        return 0;
    }
}

int main() {
    printf("=== Core Compression Algorithm Test ===\n");
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // Show available algorithms
    char available[256];
    ccvfs_list_compress_algorithms(available, sizeof(available));
    printf("Available algorithms: %s\n", available);
    
    // Test each algorithm
    const char* algorithms[] = {"zlib", "lz4", "lzma"};
    int num_algorithms = sizeof(algorithms) / sizeof(algorithms[0]);
    int successful = 0;
    
    for (int i = 0; i < num_algorithms; i++) {
        if (test_algorithm_directly(algorithms[i])) {
            successful++;
        }
    }
    
    printf("\n=== CORE ALGORITHM TEST RESULTS ===\n");
    printf("Successful algorithm tests: %d/%d\n", successful, num_algorithms);
    
    if (successful == num_algorithms) {
        printf("✅ ALL COMPRESSION ALGORITHMS WORKING CORRECTLY\n");
        printf("✅ Core compression logic is verified\n");
        return 0;
    } else {
        printf("❌ Some compression algorithms failed\n");
        return 1;
    }
}