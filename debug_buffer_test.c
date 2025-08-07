/*
 * Debug Write Buffer Initialization Test
 */

#include "include/ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_TEST_DB_PATH "debug_buffer_test.ccvfs"
#define DEBUG_TEST_VFS_NAME "debug_test_ccvfs"

int main(void) {
    sqlite3 *db = NULL;
    int rc;
    
    printf("=== Debug Write Buffer Initialization Test ===\n");
    
    // Clean up any existing file
    remove(DEBUG_TEST_DB_PATH);
    
    // Create CCVFS
    rc = sqlite3_ccvfs_create(DEBUG_TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to create VFS: %d\n", rc);
        return 1;
    }
    
    printf("VFS created successfully\n");
    
    // Configure write buffer explicitly
    rc = sqlite3_ccvfs_configure_write_buffer(DEBUG_TEST_VFS_NAME, 1, 8, 1024*1024, 4);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to configure write buffer: %d\n", rc);
        sqlite3_ccvfs_destroy(DEBUG_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Write buffer configured: enabled=1, max_entries=8, auto_flush=4\n");
    
    // Open database - this should trigger initialization
    rc = sqlite3_open_v2(DEBUG_TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        DEBUG_TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy(DEBUG_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Database opened successfully\n");
    
    // First, try a very simple write operation that should trigger buffer initialization
    printf("Executing first SQL statement to trigger initialization...\n");
    rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("PRAGMA failed (expected for compressed VFS): %s\n", sqlite3_errmsg(db));
    }
    
    // Try creating a table
    printf("Creating table...\n");
    rc = sqlite3_exec(db, "CREATE TABLE debug_test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(DEBUG_TEST_VFS_NAME);
        return 1;
    }
    
    printf("Table created successfully\n");
    
    // Get buffer stats after table creation
    uint32_t hits, flushes, merges, total_writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &total_writes);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to get buffer stats: %d\n", rc);
    } else {
        printf("Buffer stats after table creation: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
               hits, flushes, merges, total_writes);
    }
    
    // Insert a single record to see if buffering kicks in
    printf("Inserting single record...\n");
    rc = sqlite3_exec(db, "INSERT INTO debug_test (data) VALUES ('test')", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("ERROR: Failed to insert record: %s\n", sqlite3_errmsg(db));
    } else {
        printf("Record inserted successfully\n");
        
        // Check buffer stats again
        rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &total_writes);
        if (rc != SQLITE_OK) {
            printf("ERROR: Failed to get buffer stats: %d\n", rc);
        } else {
            printf("Buffer stats after insert: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
                   hits, flushes, merges, total_writes);
        }
    }
    
    // Close and clean up
    sqlite3_close(db);
    sqlite3_ccvfs_destroy(DEBUG_TEST_VFS_NAME);
    remove(DEBUG_TEST_DB_PATH);
    
    printf("Test completed\n");
    return 0;
}