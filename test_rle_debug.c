#include "src/ccvfs_algorithm.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Test the RLE algorithm in isolation
int main() {
    // Create test data - lots of zeros like SQLite database pages
    unsigned char input[1024];
    memset(input, 0, 1024);
    
    // Add some non-zero data
    input[0] = 'S';
    input[1] = 'Q';  
    input[2] = 'L';
    input[3] = 'i';
    input[16] = 0x0D;
    input[20] = 0x00;
    input[21] = 0x00;
    input[22] = 0x0F;
    
    printf("Testing RLE compression/decompression...\n");
    
    // Test compression
    unsigned char compressed[2048];
    int compressed_size = rle_compress(input, 1024, compressed, 2048, 1);
    if (compressed_size < 0) {
        printf("Compression failed: %d\n", compressed_size);
        return 1;
    }
    
    printf("Original size: 1024, Compressed size: %d\n", compressed_size);
    
    // Print first 20 bytes of compressed data
    printf("Compressed data: ");
    for (int i = 0; i < 20 && i < compressed_size; i++) {
        printf("%02x ", compressed[i]);
    }
    printf("\n");
    
    // Test decompression
    unsigned char decompressed[1024];
    int decompressed_size = rle_decompress(compressed, compressed_size, decompressed, 1024);
    if (decompressed_size < 0) {
        printf("Decompression failed: %d\n", decompressed_size);
        return 1;
    }
    
    printf("Decompressed size: %d\n", decompressed_size);
    
    // Verify data
    if (decompressed_size != 1024) {
        printf("Size mismatch: expected 1024, got %d\n", decompressed_size);
        return 1;
    }
    
    if (memcmp(input, decompressed, 1024) != 0) {
        printf("Data mismatch!\n");
        return 1;
    }
    
    printf("RLE test passed!\n");
    
    // Now test with XOR encryption
    printf("\nTesting with XOR encryption...\n");
    
    unsigned char key[16] = "default_key_123";
    unsigned char encrypted[2048];
    
    // Encrypt compressed data
    int encrypted_size = xor_encrypt(key, 16, compressed, compressed_size, encrypted, 2048);
    if (encrypted_size < 0) {
        printf("Encryption failed: %d\n", encrypted_size);
        return 1;
    }
    
    printf("Encrypted size: %d\n", encrypted_size);
    
    // Print first 20 bytes of encrypted data
    printf("Encrypted data: ");
    for (int i = 0; i < 20 && i < encrypted_size; i++) {
        printf("%02x ", encrypted[i]);
    }
    printf("\n");
    
    // Decrypt
    unsigned char decrypted[2048];
    int decrypted_size = xor_decrypt(key, 16, encrypted, encrypted_size, decrypted, 2048);
    if (decrypted_size < 0) {
        printf("Decryption failed: %d\n", decrypted_size);
        return 1;
    }
    
    printf("Decrypted size: %d\n", decrypted_size);
    
    // Print first 20 bytes of decrypted data
    printf("Decrypted data: ");
    for (int i = 0; i < 20 && i < decrypted_size; i++) {
        printf("%02x ", decrypted[i]);
    }
    printf("\n");
    
    // Verify decrypted matches original compressed
    if (decrypted_size != compressed_size || memcmp(compressed, decrypted, compressed_size) != 0) {
        printf("Decryption failed to restore original compressed data!\n");
        return 1;
    }
    
    // Try to decompress the decrypted data
    unsigned char final_decompressed[1024];
    int final_size = rle_decompress(decrypted, decrypted_size, final_decompressed, 1024);
    if (final_size < 0) {
        printf("Final decompression failed: %d\n", final_size);
        return 1;
    }
    
    printf("Final decompressed size: %d\n", final_size);
    
    if (final_size != 1024 || memcmp(input, final_decompressed, 1024) != 0) {
        printf("Final data doesn't match original!\n");
        return 1;
    }
    
    printf("Full compress->encrypt->decrypt->decompress test passed!\n");
    return 0;
}