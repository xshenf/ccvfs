/*
 * Buffer Management Tests
 * 
 * Contains tests for write buffering, buffer statistics, and performance optimization.
 */

#include "system_test_common.h"

// Batch Write Buffer Test
int test_batch_write_buffer(TestResult* result) {
    result->name = "Batch Write Buffer Test";
    result->passed = 0;
    result->total = 5;
    strcpy(result->message, "");
    
    cleanup_test_files("test_buffer");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS with write buffering
    int rc = sqlite3_ccvfs_create("buffer_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Configure write buffer
    rc = sqlite3_ccvfs_configure_write_buffer("buffer_vfs", 1, 32, 4*1024*1024, 16);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Buffer configuration failed: %d", rc);
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("test_buffer.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "buffer_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    }
    result->passed++;
    
    // Get initial buffer stats
    uint32_t initial_hits, initial_flushes, initial_merges, initial_writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &initial_hits, &initial_flushes, &initial_merges, &initial_writes);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Failed to get buffer stats: %d", rc);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    }
    
    // Create table and insert data
    rc = sqlite3_exec(db, "CREATE TABLE test_buffer (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    }
    
    // Insert data to trigger buffering
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 100; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test_buffer (data) VALUES ('Buffer test data %d')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    result->passed++;
    
    // Get final buffer stats
    uint32_t final_hits, final_flushes, final_merges, final_writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &final_hits, &final_flushes, &final_merges, &final_writes);
    if (rc == SQLITE_OK) {
        if (final_writes > initial_writes || final_flushes > initial_flushes) {
            result->passed++;
            snprintf(result->message, sizeof(result->message), 
                    "Buffering active: %u writes, %u flushes", 
                    final_writes - initial_writes, 
                    final_flushes - initial_flushes);
        } else {
            snprintf(result->message, sizeof(result->message), "No buffer activity detected");
        }
    } else {
        snprintf(result->message, sizeof(result->message), "Failed to get final buffer stats");
    }
    
    // Test manual flush
    sqlite3_ccvfs_flush_write_buffer(db);
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("buffer_vfs");
    
    return (result->passed == result->total) ? 1 : 0;
}

// Simple Buffer Test
int test_simple_buffer(TestResult* result) {
    result->name = "Simple Buffer Test";
    result->passed = 0;
    result->total = 4;
    strcpy(result->message, "");
    
    cleanup_test_files("simple_buffer");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("simple_buffer_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Configure simple buffer
    rc = sqlite3_ccvfs_configure_write_buffer("simple_buffer_vfs", 1, 16, 1024*1024, 8);
    if (rc == SQLITE_OK) {
        result->passed++;
        
        // Open database
        sqlite3 *db = NULL;
        rc = sqlite3_open_v2("simple_buffer.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "simple_buffer_vfs");
        if (rc == SQLITE_OK) {
            result->passed++;
            
            // Simple operations
            rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
            if (rc == SQLITE_OK) {
                for (int i = 0; i < 10; i++) {
                    char sql[256];
                    snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('Simple test %d')", i);
                    sqlite3_exec(db, sql, NULL, NULL, NULL);
                }
                
                // Test manual flush
                rc = sqlite3_ccvfs_flush_write_buffer(db);
                if (rc == SQLITE_OK) {
                    result->passed++;
                    strcpy(result->message, "Simple buffer operations completed");
                } else {
                    snprintf(result->message, sizeof(result->message), "Manual flush failed: %d", rc);
                }
            } else {
                snprintf(result->message, sizeof(result->message), "Table creation failed: %s", sqlite3_errmsg(db));
            }
            
            sqlite3_close(db);
        } else {
            snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        }
    } else {
        snprintf(result->message, sizeof(result->message), "Buffer configuration failed: %d", rc);
    }
    
    sqlite3_ccvfs_destroy("simple_buffer_vfs");
    return (result->passed == result->total) ? 1 : 0;
}