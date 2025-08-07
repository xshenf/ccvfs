/*
 * CCVFS Write Buffer Performance Test
 * 
 * This test measures the performance impact of write buffering
 * by comparing operations with and without buffering enabled.
 * 
 * Test scenarios:
 * 1. Sequential write performance
 * 2. Random write performance  
 * 3. Mixed read/write performance
 * 4. Transaction performance
 * 5. Buffer hit ratio impact
 */

#include "../include/ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define TEST_DB_PATH "perf_test.ccvfs"
#define TEST_VFS_NAME "perf_test_vfs"
#define LARGE_RECORD_COUNT 5000
#define SMALL_RECORD_COUNT 1000

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
static int run_sequential_write_test(int buffer_enabled, PerfResult *result);
static int run_random_write_test(int buffer_enabled, PerfResult *result);
static int run_mixed_operations_test(int buffer_enabled, PerfResult *result);
static int run_transaction_test(int buffer_enabled, PerfResult *result);
static int create_perf_vfs(int buffer_enabled);
static void cleanup_perf_vfs(void);
static void print_performance_comparison(const char *test_name, 
                                       PerfResult *buffered, PerfResult *unbuffered);

int main(int argc, char *argv[]) {
    printf("=== CCVFS Write Buffer Performance Test ===\n");
    printf("Measuring performance impact of write buffering...\n\n");
    
    PerfResult buffered_result, unbuffered_result;
    
    // Test 1: Sequential Write Performance
    printf("Test 1: Sequential Write Performance\n");
    printf("  Testing WITH buffering...\n");
    if (run_sequential_write_test(1, &buffered_result) != 0) {
        printf("  ERROR: Sequential write test with buffering failed\n");
        return 1;
    }
    
    printf("  Testing WITHOUT buffering...\n");
    if (run_sequential_write_test(0, &unbuffered_result) != 0) {
        printf("  ERROR: Sequential write test without buffering failed\n");
        return 1;
    }
    
    print_performance_comparison("Sequential Writes", &buffered_result, &unbuffered_result);
    
    // Test 2: Random Write Performance
    printf("\nTest 2: Random Write Performance\n");
    printf("  Testing WITH buffering...\n");
    if (run_random_write_test(1, &buffered_result) != 0) {
        printf("  ERROR: Random write test with buffering failed\n");
        return 1;
    }
    
    printf("  Testing WITHOUT buffering...\n");
    if (run_random_write_test(0, &unbuffered_result) != 0) {
        printf("  ERROR: Random write test without buffering failed\n");
        return 1;
    }
    
    print_performance_comparison("Random Writes", &buffered_result, &unbuffered_result);
    
    // Test 3: Mixed Operations Performance
    printf("\nTest 3: Mixed Read/Write Performance\n");
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
    
    // Test 4: Transaction Performance
    printf("\nTest 4: Transaction Performance\n");
    printf("  Testing WITH buffering...\n");
    if (run_transaction_test(1, &buffered_result) != 0) {
        printf("  ERROR: Transaction test with buffering failed\n");
        return 1;
    }
    
    printf("  Testing WITHOUT buffering...\n");
    if (run_transaction_test(0, &unbuffered_result) != 0) {
        printf("  ERROR: Transaction test without buffering failed\n");
        return 1;
    }
    
    print_performance_comparison("Transactions", &buffered_result, &unbuffered_result);
    
    printf("\n=== Performance Test Complete ===\n");
    printf("Write buffering shows performance benefits in scenarios with:\n");
    printf("- Repeated writes to the same pages (high merge ratio)\n");
    printf("- Mixed read/write workloads (buffer hits)\n");
    printf("- Large transactions (reduced I/O operations)\n");
    
    return 0;
}

/*
 * Test sequential write performance
 */
static int run_sequential_write_test(int buffer_enabled, PerfResult *result) {
    sqlite3 *db = NULL;
    int rc;
    double start_time, end_time;
    
    memset(result, 0, sizeof(PerfResult));
    
    // Create VFS
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
    rc = sqlite3_exec(db, "CREATE TABLE seq_test (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        cleanup_perf_vfs();
        return -1;
    }
    
    // Sequential inserts
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (int i = 0; i < LARGE_RECORD_COUNT; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO seq_test (data) VALUES ('Sequential test record %d with substantial content to make the record larger and test compression effectiveness')", i);
        
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("    ERROR: Failed to insert record %d: %s\n", i, sqlite3_errmsg(db));
            sqlite3_close(db);
            cleanup_perf_vfs();
            return -1;
        }
    }
    
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    end_time = get_time_seconds();
    result->elapsed_time = end_time - start_time;
    
    // Get buffer statistics
    if (buffer_enabled) {
        sqlite3_ccvfs_get_buffer_stats(db, &result->buffer_hits, &result->buffer_flushes,
                                      &result->buffer_merges, &result->total_writes);
    }
    
    // Get file size
    sqlite3_int64 file_size;
    sqlite3_file *pFile = NULL;
    sqlite3_file_control(db, NULL, SQLITE_FCNTL_FILE_POINTER, &pFile);
    if (pFile && pFile->pMethods->xFileSize(pFile, &file_size) == SQLITE_OK) {
        result->final_file_size = (size_t)file_size;
    }
    
    sqlite3_close(db);
    cleanup_perf_vfs();
    
    printf("    Completed: %.3f seconds, %d records\n", result->elapsed_time, LARGE_RECORD_COUNT);
    
    return 0;
}

/*
 * Test random write performance
 */
static int run_random_write_test(int buffer_enabled, PerfResult *result) {
    sqlite3 *db = NULL;
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
    
    // Create table with initial data
    rc = sqlite3_exec(db, "CREATE TABLE rand_test (id INTEGER PRIMARY KEY, data TEXT, value INTEGER)", 
                     NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        cleanup_perf_vfs();
        return -1;
    }
    
    // Insert initial data
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < SMALL_RECORD_COUNT; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO rand_test (data, value) VALUES ('Initial data %d', %d)", i, i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    start_time = get_time_seconds();
    
    // Random updates (this causes random page writes)
    srand((unsigned int)time(NULL));
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (int i = 0; i < SMALL_RECORD_COUNT * 2; i++) {
        int random_id = (rand() % SMALL_RECORD_COUNT) + 1;
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "UPDATE rand_test SET data = 'Updated random data %d iteration %d', value = %d WHERE id = %d", 
                random_id, i, i, random_id);
        
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("    ERROR: Failed to update record %d: %s\n", random_id, sqlite3_errmsg(db));
            sqlite3_close(db);
            cleanup_perf_vfs();
            return -1;
        }
    }
    
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    end_time = get_time_seconds();
    result->elapsed_time = end_time - start_time;
    
    // Get buffer statistics
    if (buffer_enabled) {
        sqlite3_ccvfs_get_buffer_stats(db, &result->buffer_hits, &result->buffer_flushes,
                                      &result->buffer_merges, &result->total_writes);
    }
    
    // Get file size
    sqlite3_int64 file_size;
    sqlite3_file *pFile = NULL;
    sqlite3_file_control(db, NULL, SQLITE_FCNTL_FILE_POINTER, &pFile);
    if (pFile && pFile->pMethods->xFileSize(pFile, &file_size) == SQLITE_OK) {
        result->final_file_size = (size_t)file_size;
    }
    
    sqlite3_close(db);
    cleanup_perf_vfs();
    
    printf("    Completed: %.3f seconds, %d updates\n", result->elapsed_time, SMALL_RECORD_COUNT * 2);
    
    return 0;
}

/*
 * Test mixed read/write performance
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
    
    // Mixed operations: insert, read, update pattern
    for (int round = 0; round < 100; round++) {
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        
        // Insert some records
        for (int i = 0; i < 10; i++) {
            char sql[256];
            snprintf(sql, sizeof(sql), 
                    "INSERT INTO mixed_test (data, counter) VALUES ('Mixed test round %d item %d', %d)", 
                    round, i, round * 10 + i);
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        }
        
        sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
        
        // Read some records (should hit buffer)
        rc = sqlite3_prepare_v2(db, "SELECT data, counter FROM mixed_test WHERE id > ? LIMIT 5", -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, round * 10);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                // Just reading the data
            }
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
        
        // Update some records
        if (round > 0) {
            sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
            for (int i = 0; i < 3; i++) {
                char sql[256];
                int update_id = (round - 1) * 10 + i + 1;
                snprintf(sql, sizeof(sql), 
                        "UPDATE mixed_test SET data = 'Updated in round %d', counter = counter + 1 WHERE id = %d", 
                        round, update_id);
                sqlite3_exec(db, sql, NULL, NULL, NULL);
            }
            sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
        }
    }
    
    end_time = get_time_seconds();
    result->elapsed_time = end_time - start_time;
    
    // Get buffer statistics
    if (buffer_enabled) {
        sqlite3_ccvfs_get_buffer_stats(db, &result->buffer_hits, &result->buffer_flushes,
                                      &result->buffer_merges, &result->total_writes);
    }
    
    // Get file size
    sqlite3_int64 file_size;
    sqlite3_file *pFile = NULL;
    sqlite3_file_control(db, NULL, SQLITE_FCNTL_FILE_POINTER, &pFile);
    if (pFile && pFile->pMethods->xFileSize(pFile, &file_size) == SQLITE_OK) {
        result->final_file_size = (size_t)file_size;
    }
    
    sqlite3_close(db);
    cleanup_perf_vfs();
    
    printf("    Completed: %.3f seconds, 100 rounds of mixed operations\n", result->elapsed_time);
    
    return 0;
}

/*
 * Test transaction performance
 */
static int run_transaction_test(int buffer_enabled, PerfResult *result) {
    sqlite3 *db = NULL;
    int rc;
    double start_time, end_time;
    
    memset(result, 0, sizeof(PerfResult));
    
    // Create VFS
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
    rc = sqlite3_exec(db, "CREATE TABLE trans_test (id INTEGER PRIMARY KEY, data TEXT, batch INTEGER)", 
                     NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("    ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        cleanup_perf_vfs();
        return -1;
    }
    
    // Multiple large transactions
    for (int batch = 0; batch < 10; batch++) {
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        
        for (int i = 0; i < 200; i++) {
            char sql[256];
            snprintf(sql, sizeof(sql), 
                    "INSERT INTO trans_test (data, batch) VALUES ('Transaction test batch %d record %d', %d)", 
                    batch, i, batch);
            
            rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
            if (rc != SQLITE_OK) {
                printf("    ERROR: Failed to insert in batch %d: %s\n", batch, sqlite3_errmsg(db));
                sqlite3_close(db);
                cleanup_perf_vfs();
                return -1;
            }
        }
        
        sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    }
    
    end_time = get_time_seconds();
    result->elapsed_time = end_time - start_time;
    
    // Get buffer statistics
    if (buffer_enabled) {
        sqlite3_ccvfs_get_buffer_stats(db, &result->buffer_hits, &result->buffer_flushes,
                                      &result->buffer_merges, &result->total_writes);
    }
    
    // Get file size
    sqlite3_int64 file_size;
    sqlite3_file *pFile = NULL;
    sqlite3_file_control(db, NULL, SQLITE_FCNTL_FILE_POINTER, &pFile);
    if (pFile && pFile->pMethods->xFileSize(pFile, &file_size) == SQLITE_OK) {
        result->final_file_size = (size_t)file_size;
    }
    
    sqlite3_close(db);
    cleanup_perf_vfs();
    
    printf("    Completed: %.3f seconds, 10 transactions with 200 records each\n", result->elapsed_time);
    
    return 0;
}

/*
 * Create performance test VFS
 */
static int create_perf_vfs(int buffer_enabled) {
    int rc;
    
    // Clean up any existing VFS
    cleanup_perf_vfs();
    
    // Create new CCVFS
    rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    // Configure write buffer
    if (buffer_enabled) {
        rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 64, 2*1024*1024, 32);
    } else {
        rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 0, 0, 0, 0);
    }
    
    if (rc != SQLITE_OK) {
        sqlite3_ccvfs_destroy(TEST_VFS_NAME);
        return rc;
    }
    
    return SQLITE_OK;
}

/*
 * Cleanup performance test VFS
 */
static void cleanup_perf_vfs(void) {
    sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    remove(TEST_DB_PATH);
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
    } else {
        printf("    ⚠ Buffering overhead detected (normal for small datasets)\n");
    }
}

/*
 * Get current time in seconds
 */
static double get_time_seconds(void) {
    clock_t t = clock();
    return ((double)t) / CLOCKS_PER_SEC;
}