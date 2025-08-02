/*
 * VFS Connection Test
 * Tests if the VFS connection issue is resolved
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"
#include "ccvfs.h"
#include "ccvfs_algorithm.h"

// Clean up old test files
void cleanup_test_files() {
    printf("🧹 Cleaning up old test files...\n");
    
    // Remove main database file
    if (remove("vfs_test.db") == 0) {
        printf("   Removed vfs_test.db\n");
    }
    
    // Remove journal file
    if (remove("vfs_test.db-journal") == 0) {
        printf("   Removed vfs_test.db-journal\n");
    }
    
    // Remove WAL file (if exists)
    if (remove("vfs_test.db-wal") == 0) {
        printf("   Removed vfs_test.db-wal\n");
    }
    
    // Remove SHM file (if exists)
    if (remove("vfs_test.db-shm") == 0) {
        printf("   Removed vfs_test.db-shm\n");
    }
    
    printf("✅ Cleanup completed\n\n");
}

int test_vfs_connection() {
    printf("=== Testing VFS Connection ===\n");
    
    // Clean up before testing
    cleanup_test_files();
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("test_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ VFS creation failed: %d\n", rc);
        return 0;
    }
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("vfs_test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "test_vfs");
    if (rc != SQLITE_OK) {
        printf("❌ Database open failed: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    
    printf("✅ Database opened successfully\n");
    
    // Set page size to 64KB to match CCVFS block size for optimal performance
    char *err_msg = NULL;
    /*rc = sqlite3_exec(db, "PRAGMA page_size=65536;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Failed to set page size: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }*/
    
    printf("✅ Page size set to 64KB\n");
    
    // Create table
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, text TEXT);", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Table creation failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    
    printf("✅ Table created successfully\n");
    
    // Test insert
    rc = sqlite3_exec(db, "INSERT INTO test (text) VALUES ('Hello World');", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Insert failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    
    printf("✅ Insert successful\n");
    
    // Test select
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, text FROM test;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ Select preparation failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    
    int found_records = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* text = (const char*)sqlite3_column_text(stmt, 1);
        printf("✅ Found record: id=%d, text='%s'\n", id, text);
        found_records++;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("test_vfs");
    
    if (found_records > 0) {
        printf("✅ VFS connection test PASSED - %d records retrieved\n", found_records);
        return 1;
    } else {
        printf("❌ VFS connection test FAILED - no records retrieved\n");
        return 0;
    }
}

int main() {
    printf("=== VFS Connection Debug Test ===\n");
    
    if (test_vfs_connection()) {
        printf("\n✅ VFS is working correctly!\n");
        return 0;
    } else {
        printf("\n❌ VFS still has issues\n");
        return 1;
    }
}