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
    result->total = 7;  // Increased total for additional verification steps
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
    
    // Create table and insert test data
    rc = sqlite3_exec(db, "CREATE TABLE test_buffer (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    }
    
    // Insert test data with known content for verification
    const int TEST_RECORDS = 100;
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < TEST_RECORDS; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test_buffer (data) VALUES ('Buffer test data record %d')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            snprintf(result->message, sizeof(result->message), "Insert failed at record %d: %s", i, sqlite3_errmsg(db));
            sqlite3_close(db);
            sqlite3_ccvfs_destroy("buffer_vfs");
            return 0;
        }
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    result->passed++; // Data insertion completed
    
    // Verify buffer statistics
    uint32_t final_hits, final_flushes, final_merges, final_writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &final_hits, &final_flushes, &final_merges, &final_writes);
    if (rc == SQLITE_OK && (final_writes > initial_writes || final_flushes > initial_flushes)) {
        result->passed++; // Buffer activity verified
    } else {
        snprintf(result->message, sizeof(result->message), "Buffer activity verification failed");
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    }
    
    // Critical: Verify data integrity by reading back all records
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, data FROM test_buffer ORDER BY id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Select preparation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    }
    
    int verified_records = 0;
    int data_integrity_ok = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && data_integrity_ok) {
        int id = sqlite3_column_int(stmt, 0);
        const char* data = (const char*)sqlite3_column_text(stmt, 1);
        
        // Verify each record matches expected content
        char expected_data[256];
        snprintf(expected_data, sizeof(expected_data), "Buffer test data record %d", id - 1); // id is 1-based, our loop was 0-based
        
        if (strcmp(data, expected_data) != 0) {
            snprintf(result->message, sizeof(result->message), 
                    "Data integrity error: id=%d, expected='%s', got='%s'", 
                    id, expected_data, data);
            data_integrity_ok = 0;
            break;
        }
        verified_records++;
    }
    
    sqlite3_finalize(stmt);
    
    if (data_integrity_ok && verified_records == TEST_RECORDS) {
        result->passed++; // Data integrity verification passed
    } else if (data_integrity_ok) {
        snprintf(result->message, sizeof(result->message), 
                "Record count mismatch: expected %d, verified %d", 
                TEST_RECORDS, verified_records);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    } else {
        // Error message already set in the loop
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("buffer_vfs");
        return 0;
    }
    
    // Test manual flush and verify it works
    rc = sqlite3_ccvfs_flush_write_buffer(db);
    if (rc == SQLITE_OK) {
        result->passed++; // Manual flush successful
        snprintf(result->message, sizeof(result->message), 
                "Buffer test passed: %d records verified, buffering active (%u writes, %u flushes)", 
                verified_records, final_writes - initial_writes, final_flushes - initial_flushes);
    } else {
        snprintf(result->message, sizeof(result->message), "Manual flush failed: %d", rc);
    }
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("buffer_vfs");
    
    return (result->passed == result->total) ? 1 : 0;
}

// Simple Buffer Test
int test_simple_buffer(TestResult* result) {
    result->name = "Simple Buffer Test";
    result->passed = 0;
    result->total = 5;  // Increased for data verification
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
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Buffer configuration failed: %d", rc);
        sqlite3_ccvfs_destroy("simple_buffer_vfs");
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("simple_buffer.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "simple_buffer_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("simple_buffer_vfs");
        return 0;
    }
    result->passed++;
    
    // Create table and insert test data
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("simple_buffer_vfs");
        return 0;
    }
    
    // Insert test data with known content
    const int TEST_COUNT = 10;
    for (int i = 0; i < TEST_COUNT; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('Simple test record %d')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            snprintf(result->message, sizeof(result->message), "Insert failed at record %d: %s", i, sqlite3_errmsg(db));
            sqlite3_close(db);
            sqlite3_ccvfs_destroy("simple_buffer_vfs");
            return 0;
        }
    }
    result->passed++; // Data insertion completed
    
    // Test manual flush
    rc = sqlite3_ccvfs_flush_write_buffer(db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Manual flush failed: %d", rc);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("simple_buffer_vfs");
        return 0;
    }
    
    // Verify data integrity by reading back all records
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, data FROM test ORDER BY id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Select preparation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("simple_buffer_vfs");
        return 0;
    }
    
    int verified_count = 0;
    int data_integrity_ok = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && data_integrity_ok) {
        int id = sqlite3_column_int(stmt, 0);
        const char* data = (const char*)sqlite3_column_text(stmt, 1);
        
        // Verify record content matches expected
        char expected_data[256];
        snprintf(expected_data, sizeof(expected_data), "Simple test record %d", id - 1);
        
        if (strcmp(data, expected_data) != 0) {
            snprintf(result->message, sizeof(result->message), 
                    "Data integrity error: id=%d, expected='%s', got='%s'", 
                    id, expected_data, data);
            data_integrity_ok = 0;
            break;
        }
        verified_count++;
    }
    
    sqlite3_finalize(stmt);
    
    if (data_integrity_ok && verified_count == TEST_COUNT) {
        result->passed++; // Data integrity verification passed
        snprintf(result->message, sizeof(result->message), 
                "Simple buffer test passed: %d records verified", verified_count);
    } else if (data_integrity_ok) {
        snprintf(result->message, sizeof(result->message), 
                "Record count mismatch: expected %d, verified %d", TEST_COUNT, verified_count);
    }
    // Error message already set for data integrity issues
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("simple_buffer_vfs");
    
    return (result->passed == result->total) ? 1 : 0;
}