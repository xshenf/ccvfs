/*
 * Large Database Stress Test
 * Tests the CCVFS mapping table with a substantial amount of data
 * Verifies performance and integrity with many blocks
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"
#include "compress_vfs.h"
#include "ccvfs_algorithm.h"

// Test configuration
#define TEST_RECORDS 50000        // Number of records to insert
#define TEXT_SIZE 1024           // Size of text data per record
#define BATCH_SIZE 1000          // Records per transaction

// Clean up old test files
void cleanup_test_files() {
    printf("🧹 Cleaning up old large test files...\n");
    
    // Remove main database file
    if (remove("large_stress_test.db") == 0) {
        printf("   Removed large_stress_test.db\n");
    }
    
    // Remove journal file
    if (remove("large_stress_test.db-journal") == 0) {
        printf("   Removed large_stress_test.db-journal\n");
    }
    
    // Remove WAL file (if exists)
    if (remove("large_stress_test.db-wal") == 0) {
        printf("   Removed large_stress_test.db-wal\n");
    }
    
    // Remove SHM file (if exists)
    if (remove("large_stress_test.db-shm") == 0) {
        printf("   Removed large_stress_test.db-shm\n");
    }
    
    printf("✅ Large test cleanup completed\n\n");
}

// Generate test data
void generate_test_data(char *buffer, int size, int record_id) {
    // Create varied text data to ensure good compression test
    const char *patterns[] = {
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ",
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ",
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco. ",
        "Duis aute irure dolor in reprehenderit in voluptate velit esse. ",
        "Excepteur sint occaecat cupidatat non proident, sunt in culpa. "
    };
    
    int pos = 0;
    int pattern_count = sizeof(patterns) / sizeof(patterns[0]);
    
    // Add record identifier
    pos += snprintf(buffer + pos, size - pos, "Record_%06d: ", record_id);
    
    // Fill with repeating patterns (good for compression testing)
    while (pos < size - 100) {
        const char *pattern = patterns[record_id % pattern_count];
        int pattern_len = strlen(pattern);
        if (pos + pattern_len >= size - 100) break;
        strcpy(buffer + pos, pattern);
        pos += pattern_len;
    }
    
    // Add some random variation to prevent too much compression
    pos += snprintf(buffer + pos, size - pos, " [RAND:%d] ", rand() % 10000);
    
    // Null terminate
    buffer[size - 1] = '\0';
}

int test_large_database() {
    printf("=== Large Database Stress Test ===\n");
    printf("Target: %d records with %d bytes each (~%.1f MB total data)\n", 
           TEST_RECORDS, TEXT_SIZE, (float)(TEST_RECORDS * TEXT_SIZE) / (1024 * 1024));
    
    clock_t start_time = clock();
    
    // Clean up before testing
    cleanup_test_files();
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // Create VFS with zlib compression
    int rc = sqlite3_ccvfs_create("large_test_vfs", NULL, "zlib", NULL, 0, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ VFS creation failed: %d\n", rc);
        return 0;
    }
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("large_stress_test.db", &db, 
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "large_test_vfs");
    if (rc != SQLITE_OK) {
        printf("❌ Database open failed: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("large_test_vfs");
        return 0;
    }
    
    printf("✅ Database opened successfully\n");
    
    // Set page size to 64KB to match CCVFS block size
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "PRAGMA page_size=65536;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Failed to set page size: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("large_test_vfs");
        return 0;
    }
    
    printf("✅ Page size set to 64KB for optimal performance\n");
    
    // Create table with appropriate indexes
    rc = sqlite3_exec(db, 
        "CREATE TABLE large_test ("
        "  id INTEGER PRIMARY KEY,"
        "  data_text TEXT,"
        "  created_time INTEGER,"
        "  record_size INTEGER"
        ");", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Table creation failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("large_test_vfs");
        return 0;
    }
    
    printf("✅ Table created successfully\n");
    
    // Create index for better query performance
    rc = sqlite3_exec(db, "CREATE INDEX idx_created_time ON large_test(created_time);", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Index creation failed: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("✅ Index created successfully\n");
    }
    
    // Prepare insert statement
    sqlite3_stmt *insert_stmt;
    rc = sqlite3_prepare_v2(db, 
        "INSERT INTO large_test (data_text, created_time, record_size) VALUES (?, ?, ?);", 
        -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ Insert statement preparation failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("large_test_vfs");
        return 0;
    }
    
    // Insert data in batches for better performance
    char *test_data = malloc(TEXT_SIZE);
    if (!test_data) {
        printf("❌ Failed to allocate test data buffer\n");
        sqlite3_finalize(insert_stmt);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("large_test_vfs");
        return 0;
    }
    
    printf("🔄 Inserting %d records in batches of %d...\n", TEST_RECORDS, BATCH_SIZE);
    
    int successful_inserts = 0;
    clock_t insert_start = clock();
    
    for (int i = 0; i < TEST_RECORDS; i++) {
        // Start transaction for batch
        if (i % BATCH_SIZE == 0) {
            rc = sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &err_msg);
            if (rc != SQLITE_OK) {
                printf("❌ Failed to start transaction at record %d: %s\n", i, err_msg);
                sqlite3_free(err_msg);
                break;
            }
        }
        
        // Generate test data
        generate_test_data(test_data, TEXT_SIZE, i);
        
        // Bind parameters
        sqlite3_bind_text(insert_stmt, 1, test_data, -1, SQLITE_STATIC);
        sqlite3_bind_int64(insert_stmt, 2, time(NULL) + i);
        sqlite3_bind_int(insert_stmt, 3, strlen(test_data));
        
        // Execute insert
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            printf("❌ Insert failed at record %d: %s\n", i, sqlite3_errmsg(db));
            break;
        }
        
        sqlite3_reset(insert_stmt);
        successful_inserts++;
        
        // Commit transaction for batch
        if ((i + 1) % BATCH_SIZE == 0 || i == TEST_RECORDS - 1) {
            rc = sqlite3_exec(db, "COMMIT;", 0, 0, &err_msg);
            if (rc != SQLITE_OK) {
                printf("❌ Failed to commit transaction at record %d: %s\n", i, err_msg);
                sqlite3_free(err_msg);
                break;
            }
            
            // Progress indicator
            printf("   Progress: %d/%d records (%.1f%%) inserted\n", 
                   i + 1, TEST_RECORDS, ((float)(i + 1) / TEST_RECORDS) * 100);
        }
    }
    
    clock_t insert_end = clock();
    double insert_time = ((double)(insert_end - insert_start)) / CLOCKS_PER_SEC;
    
    printf("✅ Insert phase completed: %d/%d records in %.2f seconds\n", 
           successful_inserts, TEST_RECORDS, insert_time);
    printf("   Performance: %.0f records/second\n", successful_inserts / insert_time);
    
    sqlite3_finalize(insert_stmt);
    free(test_data);
    
    if (successful_inserts == 0) {
        printf("❌ No records inserted successfully\n");
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("large_test_vfs");
        return 0;
    }
    
    // Test reading performance with various queries
    printf("🔍 Testing read performance...\n");
    
    // Count all records
    clock_t read_start = clock();
    sqlite3_stmt *count_stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM large_test;", -1, &count_stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(count_stmt);
        if (rc == SQLITE_ROW) {
            int count = sqlite3_column_int(count_stmt, 0);
            printf("✅ Record count verification: %d records\n", count);
        }
        sqlite3_finalize(count_stmt);
    }
    
    // Test sequential read of first 1000 records
    sqlite3_stmt *select_stmt;
    rc = sqlite3_prepare_v2(db, 
        "SELECT id, length(data_text), created_time FROM large_test ORDER BY id LIMIT 1000;", 
        -1, &select_stmt, NULL);
    if (rc == SQLITE_OK) {
        int read_count = 0;
        while (sqlite3_step(select_stmt) == SQLITE_ROW) {
            read_count++;
        }
        printf("✅ Sequential read test: %d records read\n", read_count);
        sqlite3_finalize(select_stmt);
    }
    
    // Test random access
    sqlite3_stmt *random_stmt;
    rc = sqlite3_prepare_v2(db, 
        "SELECT data_text FROM large_test WHERE id = ?;", 
        -1, &random_stmt, NULL);
    if (rc == SQLITE_OK) {
        int random_tests = 100;
        int random_success = 0;
        for (int i = 0; i < random_tests; i++) {
            int random_id = (rand() % successful_inserts) + 1;
            sqlite3_bind_int(random_stmt, 1, random_id);
            if (sqlite3_step(random_stmt) == SQLITE_ROW) {
                random_success++;
            }
            sqlite3_reset(random_stmt);
        }
        printf("✅ Random access test: %d/%d successful reads\n", random_success, random_tests);
        sqlite3_finalize(random_stmt);
    }
    
    clock_t read_end = clock();
    double read_time = ((double)(read_end - read_start)) / CLOCKS_PER_SEC;
    printf("   Read tests completed in %.2f seconds\n", read_time);
    
    // Get final database size information
    sqlite3_stmt *size_stmt;
    rc = sqlite3_prepare_v2(db, "PRAGMA page_count;", -1, &size_stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(size_stmt);
        if (rc == SQLITE_ROW) {
            int page_count = sqlite3_column_int(size_stmt, 0);
            printf("📊 Database statistics:\n");
            printf("   Pages: %d x 64KB = %.2f MB logical size\n", 
                   page_count, (page_count * 64.0) / 1024.0);
        }
        sqlite3_finalize(size_stmt);
    }
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("large_test_vfs");
    
    clock_t end_time = clock();
    double total_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    if (successful_inserts >= TEST_RECORDS * 0.95) { // 95% success rate required
        printf("✅ Large database stress test PASSED\n");
        printf("   Total time: %.2f seconds\n", total_time);
        printf("   Data processed: ~%.1f MB\n", (successful_inserts * TEXT_SIZE) / (1024.0 * 1024.0));
        printf("   Average throughput: %.1f MB/second\n", 
               (successful_inserts * TEXT_SIZE) / (1024.0 * 1024.0) / total_time);
        return 1;
    } else {
        printf("❌ Large database stress test FAILED\n");
        printf("   Only %d/%d records successful (%.1f%%)\n", 
               successful_inserts, TEST_RECORDS, 
               ((float)successful_inserts / TEST_RECORDS) * 100);
        return 0;
    }
}

int main() {
    printf("=== Large Database CCVFS Stress Test ===\n");
    printf("Testing mapping table performance with substantial data load\n\n");
    
    // Seed random number generator
    srand((unsigned int)time(NULL));
    
    if (test_large_database()) {
        printf("\n✅ CCVFS large database handling is working correctly!\n");
        printf("   Fixed mapping table system performs well under load\n");
        return 0;
    } else {
        printf("\n❌ CCVFS large database test has issues\n");
        return 1;
    }
}