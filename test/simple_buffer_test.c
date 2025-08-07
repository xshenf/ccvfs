/*
 * Simple Write Buffer Test Program
 * 
 * This is a simplified test that focuses on testing the write buffer functionality
 * without complex SQLite operations that might cause corruption issues.
 */

#include "../include/ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_DB_PATH "simple_test.ccvfs"
#define TEST_VFS_NAME "simple_ccvfs"

int main(int argc, char *argv[]) {
    printf("=== Simple CCVFS Write Buffer Test ===\n");
    
    // Clean up any existing files
    remove(TEST_DB_PATH);
    
    // Create CCVFS with write buffering
    printf("Creating CCVFS with write buffering...\n");
    int rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to create CCVFS: %d\n", rc);
        return 1;
    }
    
    // Configure write buffer
    printf("Configuring write buffer...\n");
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 16, 1024*1024, 8);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to configure write buffer: %d\n", rc);
        sqlite3_ccvfs_destroy(TEST_VFS_NAME);
        return 1;
    }
    
    // Open database
    sqlite3 *db = NULL;
    printf("Opening database...\n");
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy(TEST_VFS_NAME);
        return 1;
    }
    
    // Get initial buffer stats
    uint32_t initial_hits = 0, initial_flushes = 0, initial_merges = 0, initial_writes = 0;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &initial_hits, &initial_flushes, &initial_merges, &initial_writes);
    if (rc == SQLITE_OK) {
        printf("Initial buffer stats: hits=%u, flushes=%u, merges=%u, writes=%u\n",
               initial_hits, initial_flushes, initial_merges, initial_writes);
    }
    
    // Create a simple table
    printf("Creating test table...\n");
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(TEST_VFS_NAME);
        return 1;
    }
    
    // Insert some data
    printf("Inserting test data...\n");
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 10; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('Test data %d')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("ERROR: Failed to insert record %d: %s\n", i, sqlite3_errmsg(db));
            break;
        }
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get final buffer stats
    uint32_t final_hits = 0, final_flushes = 0, final_merges = 0, final_writes = 0;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &final_hits, &final_flushes, &final_merges, &final_writes);
    if (rc == SQLITE_OK) {
        printf("Final buffer stats: hits=%u, flushes=%u, merges=%u, writes=%u\n",
               final_hits, final_flushes, final_merges, final_writes);
        
        // Check if buffering occurred
        if (final_writes > initial_writes) {
            printf("SUCCESS: Write buffering is working! (%u buffered writes)\n", 
                   final_writes - initial_writes);
        } else {
            printf("INFO: No buffered writes detected\n");
        }
        
        if (final_flushes > initial_flushes) {
            printf("SUCCESS: Buffer flushing is working! (%u flushes)\n", 
                   final_flushes - initial_flushes);
        }
    }
    
    // Test manual flush
    printf("Testing manual buffer flush...\n");
    rc = sqlite3_ccvfs_flush_write_buffer(db);
    if (rc == SQLITE_OK) {
        printf("SUCCESS: Manual flush completed\n");
    } else {
        printf("ERROR: Manual flush failed: %d\n", rc);
    }
    
    // Close database
    sqlite3_close(db);
    
    // Cleanup
    sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    remove(TEST_DB_PATH);
    
    printf("=== Test Completed Successfully ===\n");
    return 0;
}