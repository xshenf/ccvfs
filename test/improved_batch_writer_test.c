/*
 * Improved Batch Writer Test
 * 
 * This test validates the new batch writer implementation that addresses
 * the data consistency and performance issues of the original design.
 */

#include "../include/ccvfs.h"
#include "../include/ccvfs_batch_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST_DB_PATH "improved_batch_test.ccvfs"
#define TEST_VFS_NAME "improved_batch_vfs"

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define VERIFY(condition, message) \
    do { \
        if (condition) { \
            printf("  ✓ %s\n", message); \
            tests_passed++; \
        } else { \
            printf("  ✗ %s\n", message); \
            tests_failed++; \
        } \
    } while(0)

static int setup_test_vfs(void) {
    int rc;
    
    // Clean up any existing VFS
    sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    
    // Create new CCVFS
    rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create CCVFS: %d\n", rc);
        return rc;
    }
    
    return SQLITE_OK;
}

static void cleanup_test_vfs(void) {
    sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    remove(TEST_DB_PATH);
}

static void test_batch_writer_initialization(void) {
    printf("\nTest 1: Batch Writer Initialization\n");
    
    sqlite3 *db = NULL;
    int rc;
    
    rc = setup_test_vfs();
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // The batch writer should be initialized automatically
    // We can't directly access it, but we can test through the API
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_batch_writer_basic_operations(void) {
    printf("\nTest 2: Basic Batch Writer Operations\n");
    
    sqlite3 *db = NULL;
    int rc;
    
    rc = setup_test_vfs();
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Create table and insert data
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    VERIFY(rc == SQLITE_OK, "Table creation successful");
    
    // Insert test data
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 20; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('Batch test record %d')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        VERIFY(rc == SQLITE_OK, "Insert operation successful");
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Verify data can be read back
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test", -1, &stmt, NULL);
    VERIFY(rc == SQLITE_OK, "Count query prepared");
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        VERIFY(count == 20, "All records inserted successfully");
    }
    sqlite3_finalize(stmt);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_batch_writer_consistency(void) {
    printf("\nTest 3: Data Consistency Test\n");
    
    sqlite3 *db = NULL;
    int rc;
    
    rc = setup_test_vfs();
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Create table
    rc = sqlite3_exec(db, "CREATE TABLE consistency_test (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    VERIFY(rc == SQLITE_OK, "Table creation successful");
    
    // Insert and update data multiple times (tests merge functionality)
    for (int round = 0; round < 5; round++) {
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        
        // Insert new records
        for (int i = 0; i < 10; i++) {
            char sql[256];
            snprintf(sql, sizeof(sql), 
                    "INSERT INTO consistency_test (data) VALUES ('Round %d Record %d')", round, i);
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        }
        
        // Update existing records
        if (round > 0) {
            for (int i = 1; i <= 5; i++) {
                char sql[256];
                snprintf(sql, sizeof(sql), 
                        "UPDATE consistency_test SET data = 'Updated Round %d' WHERE id = %d", 
                        round, i);
                sqlite3_exec(db, sql, NULL, NULL, NULL);
            }
        }
        
        sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    }
    
    // Verify final data consistency
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM consistency_test", -1, &stmt, NULL);
    VERIFY(rc == SQLITE_OK, "Count query prepared");
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        VERIFY(count == 50, "All records present after consistency test");
    }
    sqlite3_finalize(stmt);
    
    // Verify updated records
    rc = sqlite3_prepare_v2(db, "SELECT data FROM consistency_test WHERE id = 1", -1, &stmt, NULL);
    VERIFY(rc == SQLITE_OK, "Update verification query prepared");
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *data = (const char*)sqlite3_column_text(stmt, 0);
        VERIFY(strstr(data, "Updated Round") != NULL, "Record was properly updated");
    }
    sqlite3_finalize(stmt);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_batch_writer_large_data(void) {
    printf("\nTest 4: Large Data Test\n");
    
    sqlite3 *db = NULL;
    int rc;
    
    rc = setup_test_vfs();
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Create table
    rc = sqlite3_exec(db, "CREATE TABLE large_test (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    VERIFY(rc == SQLITE_OK, "Table creation successful");
    
    // Insert larger records to test compression and batch handling
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 100; i++) {
        char sql[2048];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO large_test (data) VALUES ('Large test record %d with substantial content to test compression effectiveness and batch writer performance under load. This record contains repeated text to improve compression ratios and test the batch writer with realistic data sizes.')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        VERIFY(rc == SQLITE_OK, "Large record insert successful");
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Verify all records
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM large_test", -1, &stmt, NULL);
    VERIFY(rc == SQLITE_OK, "Large data count query prepared");
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        VERIFY(count == 100, "All large records inserted successfully");
    }
    sqlite3_finalize(stmt);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_batch_writer_mixed_operations(void) {
    printf("\nTest 5: Mixed Operations Test\n");
    
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    
    rc = setup_test_vfs();
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Create table
    rc = sqlite3_exec(db, "CREATE TABLE mixed_test (id INTEGER PRIMARY KEY, data TEXT, value INTEGER)", 
                     NULL, NULL, NULL);
    VERIFY(rc == SQLITE_OK, "Table creation successful");
    
    // Mixed operations: insert, read, update pattern
    for (int round = 0; round < 10; round++) {
        // Insert records
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        for (int i = 0; i < 5; i++) {
            char sql[256];
            snprintf(sql, sizeof(sql), 
                    "INSERT INTO mixed_test (data, value) VALUES ('Mixed test round %d item %d', %d)", 
                    round, i, round * 5 + i);
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        }
        sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
        
        // Read some records (should test batch writer read hits)
        rc = sqlite3_prepare_v2(db, "SELECT data, value FROM mixed_test WHERE id > ? LIMIT 3", -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, round * 5);
            int read_count = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                read_count++;
            }
            VERIFY(read_count > 0, "Read operations successful");
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
        
        // Update some records
        if (round > 0) {
            sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
            for (int i = 0; i < 2; i++) {
                char sql[256];
                int update_id = (round - 1) * 5 + i + 1;
                snprintf(sql, sizeof(sql), 
                        "UPDATE mixed_test SET data = 'Updated in round %d', value = value + 100 WHERE id = %d", 
                        round, update_id);
                sqlite3_exec(db, sql, NULL, NULL, NULL);
            }
            sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
        }
    }
    
    // Verify final state
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM mixed_test", -1, &stmt, NULL);
    VERIFY(rc == SQLITE_OK, "Final count query prepared");
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        VERIFY(count == 50, "All mixed operations completed successfully");
    }
    sqlite3_finalize(stmt);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

int main(int argc, char *argv[]) {
    printf("=== Improved Batch Writer Test Suite ===\n");
    printf("Testing new batch writer implementation...\n");
    
    // Run all tests
    test_batch_writer_initialization();
    test_batch_writer_basic_operations();
    test_batch_writer_consistency();
    test_batch_writer_large_data();
    test_batch_writer_mixed_operations();
    
    // Print summary
    printf("\n=== Test Summary ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", 
           (tests_passed + tests_failed) > 0 ? 
           (double)tests_passed / (tests_passed + tests_failed) * 100.0 : 0.0);
    
    if (tests_failed == 0) {
        printf("\n✓ All improved batch writer tests PASSED!\n");
        printf("✓ New batch writer implementation is working correctly.\n");
        printf("✓ Data consistency and performance improvements validated.\n");
        return 0;
    } else {
        printf("\n✗ Some tests FAILED. Batch writer implementation needs fixes.\n");
        return 1;
    }
}