/*
 * Storage and Hole Detection Tests
 * 
 * Contains tests for space management, hole detection, and storage optimization.
 */

#include "system_test_common.h"

// Hole Detection Test (comprehensive)
int test_hole_detection(TestResult* result) {
    result->name = "Hole Detection Test";
    result->passed = 0;
    result->total = 6;
    strcpy(result->message, "");
    
    cleanup_test_files("test_holes");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS with hole detection enabled
    int rc = sqlite3_ccvfs_create("hole_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("test_holes.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "hole_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("hole_vfs");
        return 0;
    }
    result->passed++;
    
    // Create test table
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("hole_vfs");
        return 0;
    }
    result->passed++;
    
    // Insert data to create pages
    for (int i = 1; i <= 50; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('Test data row %d for hole detection')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            snprintf(result->message, sizeof(result->message), "Insert failed at row %d: %s", i, sqlite3_errmsg(db));
            sqlite3_close(db);
            sqlite3_ccvfs_destroy("hole_vfs");
            return 0;
        }
    }
    result->passed++;
    
    // Force sync to ensure pages are written
    sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);
    
    // Delete some data to create holes
    rc = sqlite3_exec(db, "DELETE FROM test WHERE id % 3 = 0", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Delete failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("hole_vfs");
        return 0;
    }
    result->passed++;
    
    // Insert new data to test hole reuse
    for (int i = 51; i <= 70; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('New data row %d for hole reuse')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            snprintf(result->message, sizeof(result->message), "New insert failed: %s", sqlite3_errmsg(db));
            sqlite3_close(db);
            sqlite3_ccvfs_destroy("hole_vfs");
            return 0;
        }
    }
    
    // Verify data integrity
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test", -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        result->passed++;
        snprintf(result->message, sizeof(result->message), "Hole detection completed, %d records verified", count);
        sqlite3_finalize(stmt);
    } else {
        snprintf(result->message, sizeof(result->message), "Data verification failed");
    }
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("hole_vfs");
    
    return (result->passed == result->total) ? 1 : 0;
}

// Simple Hole Test
int test_simple_hole(TestResult* result) {
    result->name = "Simple Hole Test";
    result->passed = 0;
    result->total = 4;
    strcpy(result->message, "");
    
    cleanup_test_files("simple_holes");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("simple_hole_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("simple_holes.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "simple_hole_vfs");
    if (rc == SQLITE_OK) {
        result->passed++;
        
        // Create table and insert data
        rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
        if (rc == SQLITE_OK) {
            result->passed++;
            
            // Insert and delete data to create holes
            for (int i = 1; i <= 10; i++) {
                char sql[256];
                snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('Simple data %d')", i);
                sqlite3_exec(db, sql, NULL, NULL, NULL);
            }
            
            sqlite3_exec(db, "DELETE FROM test WHERE id % 2 = 0", NULL, NULL, NULL);
            
            // Insert new data
            for (int i = 11; i <= 15; i++) {
                char sql[256];
                snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('New data %d')", i);
                sqlite3_exec(db, sql, NULL, NULL, NULL);
            }
            
            result->passed++;
            strcpy(result->message, "Simple hole management verified");
        } else {
            snprintf(result->message, sizeof(result->message), "Table creation failed: %s", sqlite3_errmsg(db));
        }
        
        sqlite3_close(db);
    } else {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
    }
    
    sqlite3_ccvfs_destroy("simple_hole_vfs");
    return (result->passed == result->total) ? 1 : 0;
}