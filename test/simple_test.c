/*
 * Simple Compression Functionality Test
 * Tests basic compression functionality with data verification
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlite3.h"
#include "compress_vfs.h"
#include "ccvfs_algorithm.h"

int test_compression_simple(const char* algorithm) {
    printf("\n=== Testing %s ===\n", algorithm);
    
    char vfs_name[64];
    char db_name[64];
    snprintf(vfs_name, sizeof(vfs_name), "%s_test_vfs", algorithm);
    snprintf(db_name, sizeof(db_name), "simple_%s.db", algorithm);
    
    // Remove old database
    remove(db_name);
    
    // Create VFS
    int rc = sqlite3_ccvfs_create(vfs_name, NULL, algorithm, NULL, 0, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ VFS creation failed: %d\n", rc);
        return 0;
    }
    
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2(db_name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs_name);
    if (rc != SQLITE_OK) {
        printf("❌ Database open failed: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy(vfs_name);
        return 0;
    }
    
    printf("Database opened successfully\n");
    
    // Create table
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, text TEXT);", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ Table creation failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return 0;
    }
    
    printf("Table created successfully\n");
    
    // Insert test data
    const char* test_data[] = {
        "Hello World - Test record 001",
        "Compression test data - Record 002", 
        "SQLite VFS compression - Record 003",
        "Data integrity verification - Record 004",
        "Final test record - Record 005"
    };
    const int num_records = sizeof(test_data) / sizeof(test_data[0]);
    
    printf("Inserting %d records...\n", num_records);
    
    for (int i = 0; i < num_records; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql), "INSERT INTO test (text) VALUES ('%s');", test_data[i]);
        
        rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
        if (rc != SQLITE_OK) {
            printf("❌ Insert %d failed: %s\n", i + 1, err_msg);
            sqlite3_free(err_msg);
            sqlite3_close(db);
            sqlite3_ccvfs_destroy(vfs_name);
            return 0;
        }
    }
    
    printf("All records inserted successfully\n");
    
    // Verify data by reading back
    printf("Verifying data...\n");
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, text FROM test ORDER BY id;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ Query preparation failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return 0;
    }
    
    int records_verified = 0;
    int expected_id = 1;
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* text = (const char*)sqlite3_column_text(stmt, 1);
        
        if (id == expected_id && expected_id <= num_records) {
            const char* expected_text = test_data[expected_id - 1];
            if (strcmp(text, expected_text) == 0) {
                printf("  Record %d: ✅ VERIFIED\n", id);
                records_verified++;
            } else {
                printf("  Record %d: ❌ DATA MISMATCH\n", id);
                printf("    Expected: %s\n", expected_text);
                printf("    Got:      %s\n", text);
            }
        } else {
            printf("  Record %d: ❌ UNEXPECTED ID\n", id);
        }
        expected_id++;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    sqlite3_ccvfs_destroy(vfs_name);
    
    printf("Verification complete: %d/%d records verified\n", records_verified, num_records);
    
    if (records_verified == num_records) {
        printf("✅ %s: SUCCESS - All data verified correctly\n", algorithm);
        return 1;
    } else {
        printf("❌ %s: FAILED - Data verification errors\n", algorithm);
        return 0;
    }
}

int main() {
    printf("=== Simple Compression Functionality Test ===\n");
    printf("SQLite Version: %s\n", sqlite3_libversion());
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    // Show available algorithms
    char available[256];
    ccvfs_list_compress_algorithms(available, sizeof(available));
    printf("Available algorithms: %s\n", available);
    
    // Test each algorithm
    const char* algorithms[] = {"zlib", "lz4", "lzma"};
    int num_algorithms = sizeof(algorithms) / sizeof(algorithms[0]);
    int successful = 0;
    
    for (int i = 0; i < num_algorithms; i++) {
        CompressAlgorithm* algo = ccvfs_find_compress_algorithm(algorithms[i]);
        if (algo) {
            if (test_compression_simple(algorithms[i])) {
                successful++;
            }
        } else {
            printf("⚠️  %s: Not available\n", algorithms[i]);
        }
    }
    
    printf("\n=== FINAL RESULTS ===\n");
    printf("Successful tests: %d\n", successful);
    
    if (successful > 0) {
        printf("✅ Compression functionality verified!\n");
        return 0;
    } else {
        printf("❌ No compression algorithms working\n");
        return 1;
    }
}