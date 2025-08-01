/*
 * Basic Compression Functionality Test
 * Tests basic compression/decompression with strict data consistency verification
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"
#include "compress_vfs.h"
#include "ccvfs_algorithm.h"

typedef struct {
    int id;
    char data[256];
    unsigned int checksum;
} TestRecord;

// Calculate simple checksum for data verification
unsigned int calculate_checksum(const char* data) {
    unsigned int checksum = 0;
    for (int i = 0; data[i]; i++) {
        checksum = checksum * 31 + (unsigned char)data[i];
    }
    return checksum;
}

int test_basic_functionality(const char* algorithm) {
    printf("\n=== Testing %s compression algorithm ===\n", algorithm);
    
    char vfs_name[64];
    char db_name[64];
    snprintf(vfs_name, sizeof(vfs_name), "%s_vfs", algorithm);
    snprintf(db_name, sizeof(db_name), "basic_%s.db", algorithm);
    
    // Remove old database
    remove(db_name);
    
    // Create VFS
    int rc = sqlite3_ccvfs_create(vfs_name, NULL, algorithm, NULL, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ Failed to create VFS: %d\n", rc);
        return 0;
    }
    
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2(db_name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs_name);
    if (rc != SQLITE_OK) {
        printf("❌ Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy(vfs_name);
        return 0;
    }
    
    // Create table
    const char* create_sql = 
        "CREATE TABLE test_data ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "data TEXT NOT NULL, "
        "checksum INTEGER NOT NULL"
        ");";
    
    char* err_msg = NULL;
    rc = sqlite3_exec(db, create_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Failed to create table: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return 0;
    }
    
    printf("✅ Database and table created successfully\n");
    
    // Prepare test data
    TestRecord test_records[100];
    const int num_records = 100;
    
    for (int i = 0; i < num_records; i++) {
        snprintf(test_records[i].data, sizeof(test_records[i].data), 
                "Test record %03d: This is compression test data with some repeated content for better compression ratio. ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", i + 1);
        test_records[i].checksum = calculate_checksum(test_records[i].data);
    }
    
    // Insert data (same connection, no close/reopen)
    printf("Inserting %d test records...\n", num_records);
    
    sqlite3_stmt* insert_stmt;
    rc = sqlite3_prepare_v2(db, "INSERT INTO test_data (data, checksum) VALUES (?, ?);", -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return 0;
    }
    
    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, NULL);
    
    for (int i = 0; i < num_records; i++) {
        sqlite3_bind_text(insert_stmt, 1, test_records[i].data, -1, SQLITE_STATIC);
        sqlite3_bind_int(insert_stmt, 2, test_records[i].checksum);
        
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            printf("❌ Failed to insert record %d: %s\n", i + 1, sqlite3_errmsg(db));
            sqlite3_finalize(insert_stmt);
            sqlite3_close(db);
            sqlite3_ccvfs_destroy(vfs_name);
            return 0;
        }
        sqlite3_reset(insert_stmt);
    }
    
    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db, "COMMIT;", 0, 0, NULL);
    
    printf("✅ All %d records inserted successfully\n", num_records);
    
    // Verify data integrity by reading back (same connection)
    printf("Verifying data integrity...\n");
    
    sqlite3_stmt* select_stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, data, checksum FROM test_data ORDER BY id;", -1, &select_stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ Failed to prepare select statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return 0;
    }
    
    int records_verified = 0;
    int integrity_errors = 0;
    
    while ((rc = sqlite3_step(select_stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(select_stmt, 0);
        const char* data = (const char*)sqlite3_column_text(select_stmt, 1);
        int stored_checksum = sqlite3_column_int(select_stmt, 2);
        
        // Verify data matches what we inserted
        if (id >= 1 && id <= num_records) {
            TestRecord* expected = &test_records[id - 1];
            
            // Check data content
            if (strcmp(data, expected->data) != 0) {
                printf("❌ Data mismatch at record %d\n", id);
                integrity_errors++;
                continue;
            }
            
            // Check stored checksum
            if (stored_checksum != (int)expected->checksum) {
                printf("❌ Stored checksum mismatch at record %d\n", id);
                integrity_errors++;
                continue;
            }
            
            // Recalculate and verify checksum
            unsigned int calculated_checksum = calculate_checksum(data);
            if (calculated_checksum != expected->checksum) {
                printf("❌ Calculated checksum mismatch at record %d\n", id);
                integrity_errors++;
                continue;
            }
            
            records_verified++;
        } else {
            printf("❌ Invalid record ID: %d\n", id);
            integrity_errors++;
        }
    }
    
    sqlite3_finalize(select_stmt);
    sqlite3_close(db);
    sqlite3_ccvfs_destroy(vfs_name);
    
    // Final verification results
    printf("Data integrity verification results:\n");
    printf("  Records inserted: %d\n", num_records);
    printf("  Records verified: %d\n", records_verified);
    printf("  Integrity errors: %d\n", integrity_errors);
    
    if (records_verified == num_records && integrity_errors == 0) {
        printf("✅ %s: ALL DATA VERIFIED SUCCESSFULLY\n", algorithm);
        return 1;
    } else {
        printf("❌ %s: DATA INTEGRITY VERIFICATION FAILED\n", algorithm);
        return 0;
    }
}

int main() {
    printf("=== Basic Compression Functionality Test ===\n");
    printf("SQLite Version: %s\n", sqlite3_libversion());
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // Get available algorithms
    char available_algorithms[256];
    ccvfs_list_compress_algorithms(available_algorithms, sizeof(available_algorithms));
    printf("Available compression algorithms: %s\n", available_algorithms);
    
    // Test each available algorithm
    const char* test_algorithms[] = {"zlib", "lz4", "lzma"};
    const int num_algorithms = sizeof(test_algorithms) / sizeof(test_algorithms[0]);
    
    int successful_tests = 0;
    int total_tests = 0;
    
    for (int i = 0; i < num_algorithms; i++) {
        const char* algo = test_algorithms[i];
        
        // Check if algorithm is available
        CompressAlgorithm* compress_algo = ccvfs_find_compress_algorithm(algo);
        if (compress_algo) {
            total_tests++;
            if (test_basic_functionality(algo)) {
                successful_tests++;
            }
        } else {
            printf("⚠️  %s: Not available, skipping\n", algo);
        }
    }
    
    printf("\n=== Final Results ===\n");
    printf("Tests completed: %d/%d\n", successful_tests, total_tests);
    
    if (successful_tests == total_tests && total_tests > 0) {
        printf("✅ ALL COMPRESSION ALGORITHMS PASSED BASIC FUNCTIONALITY TEST\n");
        printf("✅ Data integrity verification: PERFECT\n");
        return 0;
    } else {
        printf("❌ Some tests failed or no algorithms available\n");
        return 1;
    }
}