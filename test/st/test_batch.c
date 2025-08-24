/*
 * Batch Write Tests
 * 
 * Contains tests for batch write functionality and performance optimization.
 */

#include "system_test_common.h"

// Batch Write Test
int test_batch_write(TestResult* result) {
    result->name = "Batch Write Test";
    result->passed = 0;
    result->total = 6;  // Increased total for additional verification steps
    strcpy(result->message, "");
    
    cleanup_test_files("test_batch");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS
#ifdef HAVE_ZLIB
    int rc = sqlite3_ccvfs_create("batch_vfs", NULL, CCVFS_COMPRESS_ZLIB, NULL, 4096, CCVFS_CREATE_REALTIME);
#else
    int rc = sqlite3_ccvfs_create("batch_vfs", NULL, NULL, NULL, 4096, CCVFS_CREATE_REALTIME);
#endif
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database with CCVFS
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("test_batch.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "batch_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("batch_vfs");
        return 0;
    }
    result->passed++;
    
    // Create test table
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS batch_test ("
        "id INTEGER PRIMARY KEY, "
        "data TEXT, "
        "timestamp INTEGER"
        ")";
    
    rc = sqlite3_exec(db, create_sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("batch_vfs");
        return 0;
    }
    result->passed++;
    
    // Insert test records with transaction
    const int TEST_RECORDS = 100;
    rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Begin transaction failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("batch_vfs");
        return 0;
    }
    
    // Prepare insert statement
    const char *insert_sql = "INSERT INTO batch_test (data, timestamp) VALUES (?, ?)";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Prepare insert statement failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("batch_vfs");
        return 0;
    }
    
    // Insert test data
    int successful_inserts = 0;
    for (int i = 0; i < TEST_RECORDS; i++) {
        char data_buffer[256];
        snprintf(data_buffer, sizeof(data_buffer), "Test data record %d with some content", i);
        
        sqlite3_bind_text(stmt, 1, data_buffer, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64) time(NULL));
        
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            successful_inserts++;
        }
        
        sqlite3_reset(stmt);
    }
    
    sqlite3_finalize(stmt);
    
    rc = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Commit transaction failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("batch_vfs");
        return 0;
    }
    result->passed++;
    
    // Verify data integrity
    sqlite3_stmt *verify_stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM batch_test", -1, &verify_stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(verify_stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(verify_stmt, 0);
        if (count == TEST_RECORDS) {
            result->passed++;
            snprintf(result->message, sizeof(result->message), "Batch write test passed: %d records inserted", count);
        } else {
            snprintf(result->message, sizeof(result->message), "Record count mismatch: expected %d, got %d", TEST_RECORDS, count);
        }
    } else {
        snprintf(result->message, sizeof(result->message), "Data verification failed: %s", sqlite3_errmsg(db));
    }
    sqlite3_finalize(verify_stmt);
    
    // Additional verification: Check data content
    rc = sqlite3_prepare_v2(db, "SELECT id, data FROM batch_test ORDER BY id LIMIT 5", -1, &verify_stmt, NULL);
    if (rc == SQLITE_OK) {
        int verified_records = 0;
        while (sqlite3_step(verify_stmt) == SQLITE_ROW && verified_records < 5) {
            int id = sqlite3_column_int(verify_stmt, 0);
            const char* data = (const char*)sqlite3_column_text(verify_stmt, 1);
            
            // ID is 1-based in the database, but our loop was 0-based
            char expected_data[256];
            snprintf(expected_data, sizeof(expected_data), "Test data record %d with some content", id - 1);
            
            if (strcmp(data, expected_data) == 0) {
                verified_records++;
            } else {
                snprintf(result->message, sizeof(result->message), 
                        "Data integrity error: id=%d, expected='%s', got='%s'", 
                        id, expected_data, data);
                break;
            }
        }
        
        if (verified_records == 5) {
            result->passed++;
        }
    }
    sqlite3_finalize(verify_stmt);
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("batch_vfs");
    
    return (result->passed == result->total) ? 1 : 0;
}

// Simple Batch Write Test
int test_simple_batch(TestResult* result) {
    result->name = "Simple Batch Write Test";
    result->passed = 0;
    result->total = 4;
    strcpy(result->message, "");
    
    cleanup_test_files("simple_batch");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS
#ifdef HAVE_ZLIB
    int rc = sqlite3_ccvfs_create("simple_batch_vfs", NULL, CCVFS_COMPRESS_ZLIB, NULL, 4096, CCVFS_CREATE_REALTIME);
#else
    int rc = sqlite3_ccvfs_create("simple_batch_vfs", NULL, NULL, NULL, 4096, CCVFS_CREATE_REALTIME);
#endif
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("simple_batch.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "simple_batch_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("simple_batch_vfs");
        return 0;
    }
    result->passed++;
    
    // Create table and insert data
    const char *sql = 
        "CREATE TABLE simple_batch (id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO simple_batch VALUES (1, 'First');"
        "INSERT INTO simple_batch VALUES (2, 'Second');"
        "INSERT INTO simple_batch VALUES (3, 'Third');";
    
    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    if (rc == SQLITE_OK) {
        result->passed++;
        
        // Verify data
        sqlite3_stmt *stmt;
        rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM simple_batch", -1, &stmt, NULL);
        if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            if (count == 3) {
                result->passed++;
                strcpy(result->message, "Simple batch write test passed");
            } else {
                snprintf(result->message, sizeof(result->message), "Record count mismatch: expected 3, got %d", count);
            }
        }
        sqlite3_finalize(stmt);
    } else {
        snprintf(result->message, sizeof(result->message), "SQL execution failed: %s", sqlite3_errmsg(db));
    }
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("simple_batch_vfs");
    
    return (result->passed == result->total) ? 1 : 0;
}