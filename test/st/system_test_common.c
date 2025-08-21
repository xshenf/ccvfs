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
}

// Initialize algorithms
void init_test_algorithms(void) {
    ccvfs_init_builtin_algorithms();
}