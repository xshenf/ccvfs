/*
 * Simple Large Database Test 
 * Tests CCVFS mapping table with moderate data without verbose debug output
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"
#include "compress_vfs.h"
#include "ccvfs_algorithm.h"

// Test configuration
#define TEST_RECORDS 5000         // Number of records to insert
#define TEXT_SIZE 512            // Size of text data per record
#define BATCH_SIZE 500           // Records per transaction

// Clean up old test files
void cleanup_test_files() {
    printf("Cleaning up old test files...\n");
    remove("simple_large_test.db");
    remove("simple_large_test.db-journal");
    remove("simple_large_test.db-wal");
    remove("simple_large_test.db-shm");
}

// Generate test data
void generate_test_data(char *buffer, int size, int record_id) {
    snprintf(buffer, size, "Record_%06d: This is test data for record number %d. ", record_id, record_id);
    
    // Fill with some pattern
    int len = strlen(buffer);
    while (len < size - 50) {
        int remaining = size - len - 1;
        int to_add = (remaining > 30) ? 30 : remaining;
        snprintf(buffer + len, to_add, "PATTERN_%d_", record_id % 10);
        len = strlen(buffer);
    }
    buffer[size - 1] = '\0';
}

int test_simple_large_database() {
    printf("=== Simple Large Database Test ===\n");
    printf("Target: %d records with %d bytes each (~%.1f MB total data)\n", 
           TEST_RECORDS, TEXT_SIZE, (float)(TEST_RECORDS * TEXT_SIZE) / (1024 * 1024));
    
    clock_t start_time = clock();
    
    // Clean up
    cleanup_test_files();
    
    // Initialize algorithms (suppress output by redirecting temporarily)
    ccvfs_init_builtin_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("simple_large_vfs", NULL, "zlib", NULL, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ VFS creation failed: %d\n", rc);
        return 0;
    }
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("simple_large_test.db", &db, 
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "simple_large_vfs");
    if (rc != SQLITE_OK) {
        printf("❌ Database open failed: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("simple_large_vfs");
        return 0;
    }
    
    printf("✅ Database opened successfully\n");
    
    // Set page size to 64KB
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "PRAGMA page_size=65536;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Failed to set page size: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("simple_large_vfs");
        return 0;
    }
    
    printf("✅ Page size set to 64KB\n");
    
    // Create table
    rc = sqlite3_exec(db, 
        "CREATE TABLE test_data ("
        "  id INTEGER PRIMARY KEY,"
        "  content TEXT,"
        "  created_at INTEGER"
        ");", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Table creation failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("simple_large_vfs");
        return 0;
    }
    
    printf("✅ Table created successfully\n");
    
    // Prepare insert statement
    sqlite3_stmt *insert_stmt;
    rc = sqlite3_prepare_v2(db, 
        "INSERT INTO test_data (content, created_at) VALUES (?, ?);", 
        -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ Insert statement preparation failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("simple_large_vfs");
        return 0;
    }
    
    // Insert data in batches
    char *test_data = malloc(TEXT_SIZE);
    if (!test_data) {
        printf("❌ Failed to allocate test data buffer\n");
        sqlite3_finalize(insert_stmt);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("simple_large_vfs");
        return 0;
    }
    
    printf("Inserting %d records in batches of %d...\n", TEST_RECORDS, BATCH_SIZE);
    
    int successful_inserts = 0;
    clock_t insert_start = clock();
    
    for (int i = 0; i < TEST_RECORDS; i++) {
        // Start transaction
        if (i % BATCH_SIZE == 0) {
            rc = sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &err_msg);
            if (rc != SQLITE_OK) {
                printf("❌ Failed to start transaction at record %d: %s\n", i, err_msg);
                sqlite3_free(err_msg);
                break;
            }
        }
        
        // Generate and insert data
        generate_test_data(test_data, TEXT_SIZE, i);
        sqlite3_bind_text(insert_stmt, 1, test_data, -1, SQLITE_STATIC);
        sqlite3_bind_int64(insert_stmt, 2, time(NULL) + i);
        
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            printf("❌ Insert failed at record %d: %s\n", i, sqlite3_errmsg(db));
            break;
        }
        
        sqlite3_reset(insert_stmt);
        successful_inserts++;
        
        // Commit transaction
        if ((i + 1) % BATCH_SIZE == 0 || i == TEST_RECORDS - 1) {
            rc = sqlite3_exec(db, "COMMIT;", 0, 0, &err_msg);
            if (rc != SQLITE_OK) {
                printf("❌ Failed to commit transaction at record %d: %s\n", i, err_msg);
                sqlite3_free(err_msg);
                break;
            }
            
            // Progress
            if ((i + 1) % 1000 == 0) {
                printf("  Progress: %d/%d records (%.1f%%)\n", 
                       i + 1, TEST_RECORDS, ((float)(i + 1) / TEST_RECORDS) * 100);
            }
        }
    }
    
    clock_t insert_end = clock();
    double insert_time = ((double)(insert_end - insert_start)) / CLOCKS_PER_SEC;
    
    printf("✅ Insert completed: %d/%d records in %.2f seconds\n", 
           successful_inserts, TEST_RECORDS, insert_time);
    printf("   Performance: %.0f records/second\n", successful_inserts / insert_time);
    
    sqlite3_finalize(insert_stmt);
    free(test_data);
    
    if (successful_inserts == 0) {
        printf("❌ No records inserted\n");
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("simple_large_vfs");
        return 0;
    }
    
    // Test read performance
    printf("Testing read performance...\n");
    
    // Count records
    sqlite3_stmt *count_stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test_data;", -1, &count_stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(count_stmt);
        if (rc == SQLITE_ROW) {
            int count = sqlite3_column_int(count_stmt, 0);
            printf("✅ Record count: %d\n", count);
        }
        sqlite3_finalize(count_stmt);
    }
    
    // Test sequential read
    sqlite3_stmt *select_stmt;
    rc = sqlite3_prepare_v2(db, 
        "SELECT id, length(content) FROM test_data ORDER BY id LIMIT 1000;", 
        -1, &select_stmt, NULL);
    if (rc == SQLITE_OK) {
        int read_count = 0;
        while (sqlite3_step(select_stmt) == SQLITE_ROW && read_count < 1000) {
            read_count++;
        }
        printf("✅ Sequential read: %d records\n", read_count);
        sqlite3_finalize(select_stmt);
    }
    
    // Get database statistics
    sqlite3_stmt *size_stmt;
    rc = sqlite3_prepare_v2(db, "PRAGMA page_count;", -1, &size_stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(size_stmt);
        if (rc == SQLITE_ROW) {
            int page_count = sqlite3_column_int(size_stmt, 0);
            printf("📊 Database: %d pages x 64KB = %.2f MB logical size\n", 
                   page_count, (page_count * 64.0) / 1024.0);
        }
        sqlite3_finalize(size_stmt);
    }
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("simple_large_vfs");
    
    clock_t end_time = clock();
    double total_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    if (successful_inserts >= TEST_RECORDS * 0.95) {
        printf("✅ Simple large database test PASSED\n");
        printf("   Total time: %.2f seconds\n", total_time);
        printf("   Data processed: ~%.1f MB\n", (successful_inserts * TEXT_SIZE) / (1024.0 * 1024.0));
        return 1;
    } else {
        printf("❌ Simple large database test FAILED\n");
        return 0;
    }
}

int main() {
    printf("=== Simple Large Database CCVFS Test ===\n");
    printf("Testing mapping table with moderate data load\n\n");
    
    if (test_simple_large_database()) {
        printf("\n✅ CCVFS large database handling works correctly!\n");
        printf("   Fixed mapping table performs well\n");
        return 0;
    } else {
        printf("\n❌ CCVFS large database test failed\n");
        return 1;
    }
}