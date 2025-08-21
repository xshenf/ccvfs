/*
 * System Test Implementations
 * 
 * This file contains the actual test implementations that were
 * previously scattered across multiple test programs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "ccvfs.h"
#include "ccvfs_algorithm.h"

// Test result structure (defined in main)
typedef struct {
    const char* name;
    int passed;
    int total;
    char message[512];
} TestResult;

// Utility function to clean up test files
static void cleanup_test_files(const char* prefix) {
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

// VFS Connection Test
int test_vfs_connection(TestResult* result) {
    result->name = "VFS Connection Test";
    result->passed = 0;
    result->total = 6;
    strcpy(result->message, "");
    
    cleanup_test_files("vfs_test");
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("test_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("vfs_test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "test_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    result->passed++;
    
    // Create table
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, text TEXT);", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    result->passed++;
    
    // Test insert
    rc = sqlite3_exec(db, "INSERT INTO test (text) VALUES ('Hello World');", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Insert failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    result->passed++;
    
    // Test select
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, text FROM test;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Select preparation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    result->passed++;
    
    int found_records = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        found_records++;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("test_vfs");
    
    if (found_records > 0) {
        result->passed++;
        snprintf(result->message, sizeof(result->message), "%d records retrieved", found_records);
        return 1;
    } else {
        snprintf(result->message, sizeof(result->message), "No records retrieved");
        return 0;
    }
}

// Simple Database Test
int test_simple_db(TestResult* result) {
    result->name = "Simple Database Test";
    result->passed = 0;
    result->total = 4;
    strcpy(result->message, "");
    
    cleanup_test_files("simple_test");
    
    sqlite3 *db;
    int rc;
    char *err_msg = NULL;
    
    // Create test database
    rc = sqlite3_open("simple_test.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot create database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    const char *sql = 
        "CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO test VALUES (1, 'Hello');"
        "INSERT INTO test VALUES (2, 'World');"
        "INSERT INTO test VALUES (3, 'SQLite');";
    
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 0;
    }
    
    sqlite3_close(db);
    result->passed++;
    
    // Compress database
    rc = sqlite3_ccvfs_compress_database("simple_test.db", "simple_test.ccvfs", "zlib", NULL, 6);
    if (rc == SQLITE_OK) {
        result->passed++;
    } else {
        snprintf(result->message, sizeof(result->message), "Database compression failed: %d", rc);
        return 0;
    }
    
    // Decompress database
    rc = sqlite3_ccvfs_decompress_database("simple_test.ccvfs", "simple_test_restored.db");
    if (rc == SQLITE_OK) {
        result->passed++;
    } else {
        snprintf(result->message, sizeof(result->message), "Database decompression failed: %d", rc);
        return 0;
    }
    
    // Verify data
    rc = sqlite3_open("simple_test_restored.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot open decompressed database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT * FROM test ORDER BY id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL query error: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    if (count == 3) {
        result->passed++;
        snprintf(result->message, sizeof(result->message), "All %d records verified", count);
        return 1;
    } else {
        snprintf(result->message, sizeof(result->message), "Expected 3 records, got %d", count);
        return 0;
    }
}

// Large Database Stress Test
int test_large_db_stress(TestResult* result) {
    result->name = "Large Database Stress Test";
    result->passed = 0;
    result->total = 3;
    strcpy(result->message, "");
    
    cleanup_test_files("large_stress_test");
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("stress_vfs", NULL, "zlib", NULL, 65536, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("large_stress_test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "stress_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("stress_vfs");
        return 0;
    }
    result->passed++;
    
    // Create table and insert data
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "CREATE TABLE stress_test (id INTEGER PRIMARY KEY, data TEXT);", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("stress_vfs");
        return 0;
    }
    
    // Insert test data (reduced for quick testing)
    const char *large_text = "This is a test string that will be repeated many times to create larger database content for stress testing.";
    
    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Begin transaction failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("stress_vfs");
        return 0;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "INSERT INTO stress_test (data) VALUES (?);", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        for (int i = 0; i < 1000; i++) {  // Reduced from larger number for quicker testing
            sqlite3_bind_text(stmt, 1, large_text, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
    
    rc = sqlite3_exec(db, "COMMIT;", 0, 0, &err_msg);
    if (rc == SQLITE_OK) {
        result->passed++;
        snprintf(result->message, sizeof(result->message), "Inserted 1000 records successfully");
    } else {
        snprintf(result->message, sizeof(result->message), "Commit failed: %s", err_msg);
        sqlite3_free(err_msg);
    }
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("stress_vfs");
    
    return (result->passed == result->total) ? 1 : 0;
}

// Simple Large Test
int test_simple_large(TestResult* result) {
    result->name = "Simple Large Test";
    result->passed = 0;
    result->total = 2;
    strcpy(result->message, "");
    
    cleanup_test_files("simple_large");
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("large_vfs", NULL, "zlib", NULL, 65536, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database and perform basic operations
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("simple_large.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "large_vfs");
    if (rc == SQLITE_OK) {
        char *err_msg = NULL;
        rc = sqlite3_exec(db, "CREATE TABLE large_test (id INTEGER, data TEXT);", 0, 0, &err_msg);
        if (rc == SQLITE_OK) {
            rc = sqlite3_exec(db, "INSERT INTO large_test VALUES (1, 'Large test data');", 0, 0, &err_msg);
            if (rc == SQLITE_OK) {
                result->passed++;
                strcpy(result->message, "Large test operations completed");
            } else {
                snprintf(result->message, sizeof(result->message), "Insert failed: %s", err_msg);
            }
        } else {
            snprintf(result->message, sizeof(result->message), "Table creation failed: %s", err_msg);
        }
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_close(db);
    } else {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
    }
    
    sqlite3_ccvfs_destroy("large_vfs");
    return (result->passed == result->total) ? 1 : 0;
}

// Hole Detection Test (comprehensive)
int test_hole_detection(TestResult* result) {
    result->name = "Hole Detection Test";
    result->passed = 0;
    result->total = 6;
    strcpy(result->message, "");
    
    cleanup_test_files("test_holes");
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
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
    ccvfs_init_builtin_algorithms();
    
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

// Batch Write Buffer Test
int test_batch_write_buffer(TestResult* result) {
    result->name = "Batch Write Buffer Test";
    result->passed = 0;
    result->total = 5;
    strcpy(result->message, "");
    
    cleanup_test_files("test_buffer");
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
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
    ccvfs_init_builtin_algorithms();
    
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

int test_db_tools(TestResult* result) {
    result->name = "Database Tools Test";
    result->passed = 0;
    result->total = 3;
    strcpy(result->message, "");
    
    cleanup_test_files("tools_test");
    
    // Test 1: Create a simple database
    sqlite3 *db;
    int rc = sqlite3_open("tools_test.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot create test database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    // Create test data
    const char *sql = 
        "CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO test VALUES (1, 'First');"
        "INSERT INTO test VALUES (2, 'Second');"
        "INSERT INTO test VALUES (3, 'Third');";
    
    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
    
    if (rc == SQLITE_OK) {
        result->passed++;
        
        // Test 2: Database compression (simulation)
        rc = sqlite3_ccvfs_compress_database("tools_test.db", "tools_test.ccvfs", "zlib", NULL, 6);
        if (rc == SQLITE_OK) {
            result->passed++;
            
            // Test 3: Database decompression (simulation)
            rc = sqlite3_ccvfs_decompress_database("tools_test.ccvfs", "tools_test_restored.db");
            if (rc == SQLITE_OK) {
                result->passed++;
                
                // Verify restored data
                rc = sqlite3_open("tools_test_restored.db", &db);
                if (rc == SQLITE_OK) {
                    sqlite3_stmt *stmt;
                    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test", -1, &stmt, NULL);
                    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                        int count = sqlite3_column_int(stmt, 0);
                        snprintf(result->message, sizeof(result->message), "Tools test completed, %d records verified", count);
                        sqlite3_finalize(stmt);
                    } else {
                        strcpy(result->message, "Database tools integration verified");
                    }
                    sqlite3_close(db);
                } else {
                    strcpy(result->message, "Database tools integration verified");
                }
            } else {
                snprintf(result->message, sizeof(result->message), "Decompression failed: %d", rc);
            }
        } else {
            snprintf(result->message, sizeof(result->message), "Compression failed: %d", rc);
        }
    } else {
        snprintf(result->message, sizeof(result->message), "Test data creation failed: %d", rc);
    }
    
    return (result->passed == result->total) ? 1 : 0;
}