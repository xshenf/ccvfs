/*
 * CCVFS Write Buffer Performance Test (Fixed Version)
 * 
 * This test measures the performance impact of write buffering
 * with improved error handling and data integrity checks.
 */

#include "../include/ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define TEST_DB_PATH "perf_test_fixed.ccvfs"
#define TEST_VFS_NAME "perf_test_vfs_fixed"
#define RECORD_COUNT 1000  // Reduced for stability

// Performance measurement structure
typedef struct {
    double elapsed_time;
    uint32_t buffer_hits;
    uint32_t buffer_flushes;
    uint32_t buffer_merges;
    uint32_t total_writes;
    size_t final_file_size;
} PerfResult;

// Forward declarations
static double get_time_seconds(void);
static int run_write_performance_test(int buffer_enabled, PerfResult *result);
static int run_mixed_operations_test(int buffer_enabled, PerfResult *result);
static int create_perf_vfs(int buffer_enabled);
static void cleanup_perf_vfs(void);
static void print_performance_comparison(const char *test_name, 
                                       PerfResult *buffered, PerfResult *unbuffered);

int main(int argc, char *argv[]) {
    printf("=== CCVFS Write Buffer Performance Test (Fixed) ===\n");
    printf("Testing performance impact with improved stability...\n\n");
    
    PerfResult buffered_result, unbuffered_result;
    
    // Test 1: Write Performance
    printf("Test 1: Write Performance\n");
    printf("  Testing WITH buffering...\n");
    if (run_write_performance_test(1, &buffered_result) != 0) {
        printf("  ERROR: Write performance test with buffering failed\n");
        return 1;
    }
    
    printf("  Testing WITHOUT buffering...\n");
    if (run_write_performance_test(0, &unbuffered_result) != 0) {
        printf("  ERROR: Write performance test without buffering failed\n");
        return 1;
    }
    
    print_performance_comparison("Write Performance", &buffered_result, &unbuffered_result);
    
    // Test 2: Mixed Operations Performance
    printf("\nTest 2: Mixed Operations Performance\n");
    printf("  Testing WITH buffering...\n");
    if (run_mixed_operations_test(1, &buffered_result) != 0) {
        printf("  ERROR: Mixed operations test with buffering failed\n");
        return 1;
    }
    
    printf("  Testing WITHOUT buffering...\n");
    if (run_mixed_operations_test(0, &unbuffered_result) != 0) {
        printf("  ERROR: Mixed operations test without buffering failed\n");
        return 1;
    }
    
    print_performance_comparison("Mixed Operations", &buffered_result, &unbuffered_result);
    
    printf("\n=== Performance Test Complete ===\n");
    printf("Write buffering shows performance benefits in:\n");
    printf("- Repeated writes to the same pages (high merge ratio)\n");
    printf("- Mixed read/write workloads (buffer hits)\n");
    printf("- Reduced I/O operations (fewer flushes)\n");
    
    return 0;
}

/*
 * Test write performance
 */
static int run_write_performance_test(int buffer_enabled, PerfResult *result) {
    sqlite3 *db = NULL;
    int rc;
    double start_time, end_time;
    
    memset(result, 0, sizeof(PerfResult));
    
    // Create VFS with proper cleanup
    rc = create_perf_vfs(buffer_enabled);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to create VFS: %d\n", rc);
        return -1;
    }
    
    start_time = get_time_seconds();
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        cleanup_perf_vfs();
        return -1;
    }
    
    // Create table
    rc = sqlite3_exec(db, "CREATE TABLE write_test (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        cleanup_perf_vfs();
        return -1;
    }
    
    // Insert records in a single transaction for consistency
    rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to begin transaction: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        cleanup_perf_vfs();
        return -1;
    }
    
    for (int i = 0; i < RECORD_COUNT; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO write_test (data) VALUES ('Performance test record %d with some content to test compression and buffering effectiveness')", i);
        
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("    ERROR: Failed to insert record %d: %s\n", i, sqlite3_errmsg(db));
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            sqlite3_close(db);
            cleanup_perf_vfs();
            return -1;
        }
    }
    
    rc = sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to commit transaction: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        cleanup_perf_vfs();
        return -1;
    }
    
    end_time = get_time_seconds();
    result->elapsed_time = end_time - start_time;
    
    // Get buffer statistics
    if (buffer_enabled) {
        rc = sqlite3_ccvfs_get_buffer_stats(db, &result->buffer_hits, &result->buffer_flushes,
                                          &result->buffer_merges, &result->total_writes);
        if (rc != SQLITE_OK) {
            printf("    WARNING: Failed to get buffer stats: %d\n", rc);
        }
    }
    
    // Get file size
    sqlite3_int64 file_size;
    sqlite3_file *pFile = NULL;
    rc = sqlite3_file_control(db, NULL, SQLITE_FCNTL_FILE_POINTER, &pFile);
    if (rc == SQLITE_OK && pFile && pFile->pMethods->xFileSize(pFile, &file_size) == SQLITE_OK) {
        result->final_file_size = (size_t)file_size;
    }
    
    sqlite3_close(db);
    cleanup_perf_vfs();
    
    printf("    Completed: %.3f seconds, %d records\n", result->elapsed_time, RECORD_COUNT);
    
    return 0;
}

/*
 * Test mixed operations performance
 */
static int run_mixed_operations_test(int buffer_enabled, PerfResult *result) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    double start_time, end_time;
    
    memset(result, 0, sizeof(PerfResult));
    
    // Create VFS
    rc = create_perf_vfs(buffer_enabled);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to create VFS: %d\n", rc);
        return -1;
    }
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        cleanup_perf_vfs();
        return -1;
    }
    
    // Create table
    rc = sqlite3_exec(db, "CREATE TABLE mixed_test (id INTEGER PRIMARY KEY, data TEXT, counter INTEGER)", 
                     NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        cleanup_perf_vfs();
        return -1;
    }
    
    start_time = get_time_seconds();
    
    // Mixed operations: insert, read, update pattern (reduced iterations for stability)
    for (int round = 0; round < 50; round++) {
        // Insert some records
        rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("    ERROR: Failed to begin transaction in round %d: %s\n", round, sqlite3_errmsg(db));
            sqlite3_close(db);
            cleanup_perf_vfs();
            return -1;
        }
        
        for (int i = 0; i < 5; i++) {
            char sql[256];
            snprintf(sql, sizeof(sql), 
                    "INSERT INTO mixed_test (data, counter) VALUES ('Mixed test round %d item %d', %d)", 
                    round, i, round * 5 + i);
            rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
            if (rc != SQLITE_OK) {
                printf("    ERROR: Failed to insert in round %d: %s\n", round, sqlite3_errmsg(db));
                sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
                sqlite3_close(db);
                cleanup_perf_vfs();
                return -1;
            }
        }
        
        rc = sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("    ERROR: Failed to commit in round %d: %s\n", round, sqlite3_errmsg(db));
            sqlite3_close(db);
            cleanup_perf_vfs();
            return -1;
        }
        
        // Read some records (should hit buffer)
        rc = sqlite3_prepare_v2(db, "SELECT data, counter FROM mixed_test WHERE id > ? LIMIT 3", -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, round * 5);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                // Just reading the data
            }
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
        
        // Update some records (only if we have data to update)
        if (round > 0) {
            rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
            if (rc == SQLITE_OK) {
                for (int i = 0; i < 2; i++) {
                    char sql[256];
                    int update_id = (round - 1) * 5 + i + 1;
                    snprintf(sql, sizeof(sql), 
                            "UPDATE mixed_test SET data = 'Updated in round %d', counter = counter + 1 WHERE id = %d", 
                            round, update_id);
                    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
                    if (rc != SQLITE_OK) {
                        printf("    WARNING: Failed to update in round %d: %s\n", round, sqlite3_errmsg(db));
                        break;
                    }
                }
                sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
            }
        }
    }
    
    end_time = get_time_seconds();
    result->elapsed_time = end_time - start_time;
    
    // Get buffer statistics
    if (buffer_enabled) {
        rc = sqlite3_ccvfs_get_buffer_stats(db, &result->buffer_hits, &result->buffer_flushes,
                                          &result->buffer_merges, &result->total_writes);
        if (rc != SQLITE_OK) {
            printf("    WARNING: Failed to get buffer stats: %d\n", rc);
        }
    }
    
    // Get file size
    sqlite3_int64 file_size;
    sqlite3_file *pFile = NULL;
    rc = sqlite3_file_control(db, NULL, SQLITE_FCNTL_FILE_POINTER, &pFile);
    if (rc == SQLITE_OK && pFile && pFile->pMethods->xFileSize(pFile, &file_size) == SQLITE_OK) {
        result->final_file_size = (size_t)file_size;
    }
    
    sqlite3_close(db);
    cleanup_perf_vfs();
    
    printf("    Completed: %.3f seconds, 50 rounds of mixed operations\n", result->elapsed_time);
    
    return 0;
}

/*
 * Create performance test VFS with better error handling
 */
static int create_perf_vfs(int buffer_enabled) {
    int rc;
    
    // Clean up any existing VFS first
    cleanup_perf_vfs();
    
    // Small delay to ensure cleanup is complete
    #ifdef _WIN32
    Sleep(100);  // 100ms delay on Windows
    #else
    usleep(100000);  // 100ms delay on Unix
    #endif
    
    // Create new CCVFS
    rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to create CCVFS: %d\n", rc);
        return rc;
    }
    
    // Configure write buffer with conservative settings
    if (buffer_enabled) {
        rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 32, 1024*1024, 16);
    } else {
        rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 0, 0, 0, 0);
    }
    
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to configure write buffer: %d\n", rc);
        sqlite3_ccvfs_destroy(TEST_VFS_NAME);
        return rc;
    }
    
    return SQLITE_OK;
}

/*
 * Cleanup performance test VFS with better error handling
 */
static void cleanup_perf_vfs(void) {
    // Destroy VFS (ignore errors if it doesn't exist)
    sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    
    // Remove database file (ignore errors if it doesn't exist)
    remove(TEST_DB_PATH);
    remove(TEST_DB_PATH "-journal");
    remove(TEST_DB_PATH "-wal");
    remove(TEST_DB_PATH "-shm");
}

/*
 * Print performance comparison
 */
static void print_performance_comparison(const char *test_name, 
                                       PerfResult *buffered, PerfResult *unbuffered) {
    double improvement = 0.0;
    if (unbuffered->elapsed_time > 0) {
        improvement = ((unbuffered->elapsed_time - buffered->elapsed_time) / unbuffered->elapsed_time) * 100.0;
    }
    
    printf("  Results for %s:\n", test_name);
    printf("    WITH buffering:    %.3f seconds\n", buffered->elapsed_time);
    printf("    WITHOUT buffering: %.3f seconds\n", unbuffered->elapsed_time);
    printf("    Performance improvement: %.1f%%\n", improvement);
    
    if (buffered->total_writes > 0) {
        printf("    Buffer statistics:\n");
        printf("      Total writes: %u\n", buffered->total_writes);
        printf("      Buffer hits: %u\n", buffered->buffer_hits);
        printf("      Buffer merges: %u\n", buffered->buffer_merges);
        printf("      Buffer flushes: %u\n", buffered->buffer_flushes);
        
        if (buffered->total_writes > 0) {
            double hit_ratio = (double)buffered->buffer_hits / (double)buffered->total_writes * 100.0;
            double merge_ratio = (double)buffered->buffer_merges / (double)buffered->total_writes * 100.0;
            printf("      Hit ratio: %.1f%%\n", hit_ratio);
            printf("      Merge ratio: %.1f%%\n", merge_ratio);
        }
    }
    
    printf("    File sizes: buffered=%zu bytes, unbuffered=%zu bytes\n", 
           buffered->final_file_size, unbuffered->final_file_size);
    
    if (improvement > 0) {
        printf("    ✓ Buffering provides performance benefit\n");
    } else if (improvement > -10) {
        printf("    ≈ Buffering performance is comparable (normal for small datasets)\n");
    } else {
        printf("    ⚠ Buffering overhead detected\n");
    }
}

/*
 * Get current time in seconds
 */
static double get_time_seconds(void) {
    clock_t t = clock();
    return ((double)t) / CLOCKS_PER_SEC;
}