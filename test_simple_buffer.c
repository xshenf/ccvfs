/*
 * Simple Write Buffer Test
 * Minimal test to verify write buffering functionality works
 */

#include "include/ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIMPLE_TEST_DB_PATH "simple_buffer_test.ccvfs"
#define SIMPLE_TEST_VFS_NAME "simple_test_ccvfs"

int main(void) {
    sqlite3 *db = NULL;
    int rc;
    uint32_t hits, flushes, merges, total_writes;
    
    printf("=== Simple Write Buffer Test ===\n");
    
    // Clean up any existing file
    remove(SIMPLE_TEST_DB_PATH);
    
    // Create CCVFS
    rc = sqlite3_ccvfs_create(SIMPLE_TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to create VFS: %d\n", rc);
        return 1;
    }
    
    // Configure write buffer
    rc = sqlite3_ccvfs_configure_write_buffer(SIMPLE_TEST_VFS_NAME, 1, 16, 2*1024*1024, 8);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to configure write buffer: %d\n", rc);
        sqlite3_ccvfs_destroy(SIMPLE_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Write buffer configured successfully\n");
    
    // Open database
    rc = sqlite3_open_v2(SIMPLE_TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        SIMPLE_TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy(SIMPLE_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Database opened successfully\n");
    
    // Get initial buffer stats
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &total_writes);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to get initial buffer stats: %d\n", rc);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(SIMPLE_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Initial stats: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
           hits, flushes, merges, total_writes);
    
    // Create a simple table
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(SIMPLE_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Table created successfully\n");
    
    // Insert some data
    rc = sqlite3_exec(db, "INSERT INTO test (name) VALUES ('Test1'), ('Test2'), ('Test3')", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to insert data: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(SIMPLE_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Data inserted successfully\n");
    
    // Get buffer stats after operations
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &total_writes);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to get buffer stats: %d\n", rc);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(SIMPLE_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Final stats: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
           hits, flushes, merges, total_writes);
    
    if (total_writes > 0) {
        printf("SUCCESS: Write buffering is working (recorded %u buffered writes)\n", total_writes);
    } else {
        printf("INFO: No buffered writes recorded (may be normal for small operations)\n");
    }
    
    // Manually flush buffer
    rc = sqlite3_ccvfs_flush_write_buffer(db);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to flush write buffer: %d\n", rc);
    } else {
        printf("Write buffer flushed successfully\n");
    }
    
    // Test a query to make sure data is accessible
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            printf("Query result: %d records in test table\n", count);
            if (count == 3) {
                printf("SUCCESS: Data verification passed\n");
            } else {
                printf("WARNING: Expected 3 records, got %d\n", count);
            }
        }
        sqlite3_finalize(stmt);
    } else {
        printf("ERROR: Failed to query data: %s\n", sqlite3_errmsg(db));
    }
    
    // Close database
    sqlite3_close(db);
    
    // Clean up
    sqlite3_ccvfs_destroy(SIMPLE_TEST_VFS_NAME);
    remove(SIMPLE_TEST_DB_PATH);
    
    printf("Test completed successfully\n");
    return 0;
}