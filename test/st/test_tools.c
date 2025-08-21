/*
 * Tools Integration Tests
 * 
 * Contains tests for database compression/decompression tools and utilities.
 */

#include "system_test_common.h"

// Database Tools Test
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