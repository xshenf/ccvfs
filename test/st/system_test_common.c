/*
 * System Test Common Utilities
 * 
 * Common utility functions used across all system test implementations.
 */

#include "system_test_common.h"

// Utility function to clean up test files
void cleanup_test_files(const char* prefix) {
    char filename[256];
    
    // Main database file
    snprintf(filename, sizeof(filename), "%s.db", prefix);
    remove(filename);
    
    // CCVFS compressed file
    snprintf(filename, sizeof(filename), "%s.ccvfs", prefix);
    remove(filename);
    
    // Encrypted database file
    snprintf(filename, sizeof(filename), "%s.dbe", prefix);
    remove(filename);
    
    // Encrypted database file with _short suffix
    snprintf(filename, sizeof(filename), "%s_short.dbe", prefix);
    remove(filename);
    
    // Journal file
    snprintf(filename, sizeof(filename), "%s.db-journal", prefix);
    remove(filename);
    
    // WAL file
    snprintf(filename, sizeof(filename), "%s.db-wal", prefix);
    remove(filename);
    
    // SHM file
    snprintf(filename, sizeof(filename), "%s.db-shm", prefix);
    remove(filename);
    
    // Restored file
    snprintf(filename, sizeof(filename), "%s_restored.db", prefix);
    remove(filename);
    
    // Decrypted files
    snprintf(filename, sizeof(filename), "%s_decrypted.db", prefix);
    remove(filename);
    
    // Short key decrypted files
    snprintf(filename, sizeof(filename), "%s_short_dec.db", prefix);
    remove(filename);
    
    // Expanded key decrypted files
    snprintf(filename, sizeof(filename), "%s_expanded_dec.db", prefix);
    remove(filename);
}

// Initialize algorithms
void init_test_algorithms(void) {
    ccvfs_init_builtin_algorithms();
}