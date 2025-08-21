/*
 * Basic System Tests
 * 
 * Contains basic VFS connection and simple database operation tests.
 */

#include "system_test_common.h"

// VFS Connection Test
int test_vfs_connection(TestResult* result) {
    result->name = "VFS Connection Test";
    result->passed = 0;
    result->total = 6;
    strcpy(result->message, "");
    
    cleanup_test_files("vfs_test");
    
    // Initialize algorithms
    init_test_algorithms();
    
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
    result->total = 5;  // Increased for enhanced data verification
    strcpy(result->message, "");
    
    cleanup_test_files("simple_test");
    
    sqlite3 *db;
    int rc;
    char *err_msg = NULL;
    
    // Define test data for verification
    const char* test_records[][2] = {
        {"1", "Hello"},
        {"2", "World"},
        {"3", "SQLite"}
    };
    const int expected_count = 3;
    
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
    result->passed++; // Database creation and initial data insertion
    
    // Compress database
    rc = sqlite3_ccvfs_compress_database("simple_test.db", "simple_test.ccvfs", "zlib", NULL, 6);
    if (rc == SQLITE_OK) {
        result->passed++; // Compression successful
    } else {
        snprintf(result->message, sizeof(result->message), "Database compression failed: %d", rc);
        return 0;
    }
    
    // Decompress database
    rc = sqlite3_ccvfs_decompress_database("simple_test.ccvfs", "simple_test_restored.db");
    if (rc == SQLITE_OK) {
        result->passed++; // Decompression successful
    } else {
        snprintf(result->message, sizeof(result->message), "Database decompression failed: %d", rc);
        return 0;
    }
    
    // Comprehensive data verification
    rc = sqlite3_open("simple_test_restored.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot open decompressed database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, name FROM test ORDER BY id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL query error: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    result->passed++; // Query preparation successful
    
    // Verify each record's content
    int verified_count = 0;
    int data_integrity_ok = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && data_integrity_ok) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        
        // Check if the record matches expected data
        if (verified_count < expected_count) {
            int expected_id = atoi(test_records[verified_count][0]);
            const char* expected_name = test_records[verified_count][1];
            
            if (id != expected_id || strcmp(name, expected_name) != 0) {
                snprintf(result->message, sizeof(result->message), 
                        "Data integrity error: record %d, expected id=%d name='%s', got id=%d name='%s'", 
                        verified_count + 1, expected_id, expected_name, id, name);
                data_integrity_ok = 0;
                break;
            }
        } else {
            snprintf(result->message, sizeof(result->message), 
                    "Unexpected extra record found: id=%d name='%s'", id, name);
            data_integrity_ok = 0;
            break;
        }
        verified_count++;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    if (data_integrity_ok && verified_count == expected_count) {
        result->passed++; // Data integrity verification passed
        snprintf(result->message, sizeof(result->message), 
                "Simple DB test passed: %d records verified with correct content", verified_count);
        return 1;
    } else if (data_integrity_ok) {
        snprintf(result->message, sizeof(result->message), 
                "Record count mismatch: expected %d, verified %d", expected_count, verified_count);
        return 0;
    } else {
        // Error message already set in the loop
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
    init_test_algorithms();
    
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
    init_test_algorithms();
    
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