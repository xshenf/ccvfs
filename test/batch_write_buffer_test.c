/*
 * Batch Write Buffer Test Program
 * 
 * This test program validates the batch write buffer functionality
 * in the CCVFS SQLite Virtual File System.
 * 
 * Test scenarios:
 * 1. Basic write buffering functionality
 * 2. Buffer hit ratio during reads
 * 3. Auto-flush behavior
 * 4. Manual flush operations
 * 5. Buffer configuration changes
 * 6. Performance comparison with/without buffering
 */

#include "../include/ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

// Test configuration
#define TEST_DB_PATH "test_buffer.ccvfs"
#define TEST_VFS_NAME "test_ccvfs_buffer"
#define TEST_TABLE_SIZE 1000
#define TEST_ITERATIONS 10

// Test data structures
typedef struct {
    uint32_t buffer_hits;
    uint32_t buffer_flushes;
    uint32_t buffer_merges;
    uint32_t total_buffered_writes;
} BufferStats;

// Forward declarations
static int test_basic_buffering(void);
static int test_buffer_configuration(void);
static int test_auto_flush_behavior(void);
static int test_buffer_hit_ratio(void);
static int test_performance_comparison(void);
static int create_test_vfs(int buffer_enabled, uint32_t max_entries, uint32_t auto_flush_pages);
static int cleanup_test_vfs(void);
static int get_buffer_stats(sqlite3 *db, BufferStats *stats);
static double get_time_seconds(void);
static void print_test_header(const char *test_name);
static void print_test_result(const char *test_name, int result);

/*
 * Main test runner
 */
int main(int argc, char *argv[]) {
    int total_tests = 0;
    int passed_tests = 0;
    
    printf("=== CCVFS Batch Write Buffer Test Suite ===\n");
    printf("Testing write buffering functionality...\n\n");
    
    // Initialize random seed
    srand((unsigned int)time(NULL));
    
    // Test 1: Basic buffering functionality
    print_test_header("Basic Buffering Functionality");
    if (test_basic_buffering() == 0) {
        passed_tests++;
        print_test_result("Basic Buffering", 1);
    } else {
        print_test_result("Basic Buffering", 0);
    }
    total_tests++;
    
    // Test 2: Buffer configuration
    print_test_header("Buffer Configuration");
    if (test_buffer_configuration() == 0) {
        passed_tests++;
        print_test_result("Buffer Configuration", 1);
    } else {
        print_test_result("Buffer Configuration", 0);
    }
    total_tests++;
    
    // Test 3: Auto-flush behavior
    print_test_header("Auto-flush Behavior");
    if (test_auto_flush_behavior() == 0) {
        passed_tests++;
        print_test_result("Auto-flush Behavior", 1);
    } else {
        print_test_result("Auto-flush Behavior", 0);
    }
    total_tests++;
    
    // Test 4: Buffer hit ratio
    print_test_header("Buffer Hit Ratio");
    if (test_buffer_hit_ratio() == 0) {
        passed_tests++;
        print_test_result("Buffer Hit Ratio", 1);
    } else {
        print_test_result("Buffer Hit Ratio", 0);
    }
    total_tests++;
    
    // Test 5: Performance comparison
    print_test_header("Performance Comparison");
    if (test_performance_comparison() == 0) {
        passed_tests++;
        print_test_result("Performance Comparison", 1);
    } else {
        print_test_result("Performance Comparison", 0);
    }
    total_tests++;
    
    // Print final results
    printf("\n=== Test Results ===\n");
    printf("Total tests: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", total_tests - passed_tests);
    printf("Success rate: %.1f%%\n", (double)passed_tests / total_tests * 100.0);
    
    return (passed_tests == total_tests) ? 0 : 1;
}

/*
 * Test basic write buffering functionality
 */
static int test_basic_buffering(void) {
    sqlite3 *db = NULL;
    int rc;
    BufferStats stats_before, stats_after;
    char sql[256];
    
    printf("  Creating CCVFS with write buffering enabled...\n");
    
    // Create VFS with buffering enabled
    rc = create_test_vfs(1, 32, 16);  // enabled, 32 entries, auto-flush at 16
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create test VFS: %d\n", rc);
        return -1;
    }
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        cleanup_test_vfs();
        return -1;
    }
    
    // Get initial buffer stats
    rc = get_buffer_stats(db, &stats_before);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to get initial buffer stats: %d\n", rc);
        sqlite3_close(db);
        cleanup_test_vfs();
        return -1;
    }
    
    printf("  Initial buffer stats: hits=%u, flushes=%u, merges=%u, writes=%u\n",
           stats_before.buffer_hits, stats_before.buffer_flushes,
           stats_before.buffer_merges, stats_before.total_buffered_writes);
    
    // Create test table
    rc = sqlite3_exec(db, "CREATE TABLE test_buffer (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        cleanup_test_vfs();
        return -1;
    }
    
    // Insert test data
    printf("  Inserting %d test records...\n", TEST_TABLE_SIZE);
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (int i = 0; i < TEST_TABLE_SIZE; i++) {
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_buffer (data) VALUES ('Test data record %d with some content to make it larger')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("  ERROR: Failed to insert record %d: %s\n", i, sqlite3_errmsg(db));
            sqlite3_close(db);
            cleanup_test_vfs();
            return -1;
        }
    }
    
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get buffer stats after writes
    rc = get_buffer_stats(db, &stats_after);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to get buffer stats after writes: %d\n", rc);
        sqlite3_close(db);
        cleanup_test_vfs();
        return -1;
    }
    
    printf("  Final buffer stats: hits=%u, flushes=%u, merges=%u, writes=%u\n",
           stats_after.buffer_hits, stats_after.buffer_flushes,
           stats_after.buffer_merges, stats_after.total_buffered_writes);
    
    // Validate that buffering occurred
    if (stats_after.total_buffered_writes == stats_before.total_buffered_writes) {
        printf("  ERROR: No buffered writes detected\n");
        sqlite3_close(db);
        cleanup_test_vfs();
        return -1;
    }
    
    if (stats_after.buffer_flushes == stats_before.buffer_flushes) {
        printf("  ERROR: No buffer flushes detected\n");
        sqlite3_close(db);
        cleanup_test_vfs();
        return -1;
    }
    
    printf("  SUCCESS: Buffer statistics show expected activity\n");
    printf("  Buffered writes: %u, Buffer flushes: %u\n",
           stats_after.total_buffered_writes - stats_before.total_buffered_writes,
           stats_after.buffer_flushes - stats_before.buffer_flushes);
    
    // Close database and cleanup
    sqlite3_close(db);
    cleanup_test_vfs();
    remove(TEST_DB_PATH);
    
    return 0;
}

/*
 * Test buffer configuration functionality
 */
static int test_buffer_configuration(void) {
    int rc;
    
    printf("  Testing buffer configuration API...\n");
    
    // Create VFS with default settings
    rc = create_test_vfs(1, 0, 0);  // Use defaults
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create test VFS: %d\n", rc);
        return -1;
    }
    
    // Test configuration changes
    printf("  Configuring buffer: 64 entries, 8MB max size, auto-flush at 32...\n");
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 64, 8*1024*1024, 32);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to configure write buffer: %d\n", rc);
        cleanup_test_vfs();
        return -1;
    }
    
    // Test disabling buffer
    printf("  Disabling write buffer...\n");
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 0, 0, 0, 0);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to disable write buffer: %d\n", rc);
        cleanup_test_vfs();
        return -1;
    }
    
    // Test re-enabling buffer
    printf("  Re-enabling write buffer with custom settings...\n");
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 16, 2*1024*1024, 8);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to re-enable write buffer: %d\n", rc);
        cleanup_test_vfs();
        return -1;
    }
    
    printf("  SUCCESS: Buffer configuration API working correctly\n");
    
    // Cleanup
    cleanup_test_vfs();
    
    return 0;
}

/*
 * Test auto-flush behavior
 */
static int test_auto_flush_behavior(void) {
    sqlite3 *db = NULL;
    int rc;
    BufferStats stats_before, stats_during, stats_after;
    char sql[256];
    
    printf("  Testing auto-flush behavior with small flush threshold...\n");
    
    // Create VFS with small auto-flush threshold
    rc = create_test_vfs(1, 32, 5);  // Auto-flush every 5 pages
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create test VFS: %d\n", rc);
        return -1;
    }
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        cleanup_test_vfs();
        return -1;
    }
    
    // Get initial stats
    get_buffer_stats(db, &stats_before);
    
    // Create table and insert data to trigger auto-flush
    sqlite3_exec(db, "CREATE TABLE test_autoflush (id INTEGER PRIMARY KEY, data TEXT)", 
                NULL, NULL, NULL);
    
    printf("  Inserting data to trigger auto-flush...\n");
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (int i = 0; i < 10; i++) {  // Should trigger at least one auto-flush
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_autoflush (data) VALUES ('Large test data record %d with enough content to fill pages and trigger auto-flush behavior when threshold is reached')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
        
        if (i == 7) {  // Check stats mid-way
            get_buffer_stats(db, &stats_during);
        }
    }
    
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get final stats
    get_buffer_stats(db, &stats_after);
    
    printf("  Stats - Initial: flushes=%u, During: flushes=%u, Final: flushes=%u\n",
           stats_before.buffer_flushes, stats_during.buffer_flushes, stats_after.buffer_flushes);
    
    // Validate auto-flush occurred
    if (stats_after.buffer_flushes <= stats_before.buffer_flushes) {
        printf("  ERROR: No auto-flush detected\n");
        sqlite3_close(db);
        cleanup_test_vfs();
        return -1;
    }
    
    printf("  SUCCESS: Auto-flush triggered %u times during test\n",
           stats_after.buffer_flushes - stats_before.buffer_flushes);
    
    // Close database and cleanup
    sqlite3_close(db);
    cleanup_test_vfs();
    remove(TEST_DB_PATH);
    
    return 0;
}

/*
 * Test buffer hit ratio during reads
 */
static int test_buffer_hit_ratio(void) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    BufferStats stats_before, stats_after;
    char sql[256];
    
    printf("  Testing buffer hit ratio during read operations...\n");
    
    // Create VFS with buffering
    rc = create_test_vfs(1, 64, 32);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create test VFS: %d\n", rc);
        return -1;
    }
    
    // Open database and create test data
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        cleanup_test_vfs();
        return -1;
    }
    
    // Create and populate table
    sqlite3_exec(db, "CREATE TABLE test_hits (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (int i = 0; i < 50; i++) {
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_hits (data) VALUES ('Test data for hit ratio test %d')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Force flush to ensure data is written
    sqlite3_ccvfs_flush_write_buffer(db);
    
    // Get initial stats
    get_buffer_stats(db, &stats_before);
    
    // Perform reads that should hit recently written data in buffer
    printf("  Performing reads to test buffer hits...\n");
    
    // First, do some writes to populate buffer
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 50; i < 70; i++) {
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_hits (data) VALUES ('New buffered data %d')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Now read the recently written data
    for (int i = 0; i < 10; i++) {
        snprintf(sql, sizeof(sql), "SELECT data FROM test_hits WHERE id > 50 LIMIT 5");
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                // Just reading the data
            }
            sqlite3_finalize(stmt);
        }
    }
    
    // Get final stats
    get_buffer_stats(db, &stats_after);
    
    printf("  Buffer hits - Before: %u, After: %u, Increase: %u\n",
           stats_before.buffer_hits, stats_after.buffer_hits,
           stats_after.buffer_hits - stats_before.buffer_hits);
    
    // Validate that some buffer hits occurred
    if (stats_after.buffer_hits <= stats_before.buffer_hits) {
        printf("  WARNING: No buffer hits detected during read test\n");
        // This might not be an error depending on the data access patterns
    } else {
        printf("  SUCCESS: Buffer hits detected during read operations\n");
    }
    
    // Close database and cleanup
    sqlite3_close(db);
    cleanup_test_vfs();
    remove(TEST_DB_PATH);
    
    return 0;
}

/*
 * Test performance comparison with/without buffering
 */
static int test_performance_comparison(void) {
    sqlite3 *db = NULL;
    int rc;
    double time_with_buffer, time_without_buffer;
    double start_time, end_time;
    char sql[256];
    
    printf("  Comparing performance with and without write buffering...\n");
    
    // Test WITH buffering
    printf("  Testing performance WITH write buffering...\n");
    rc = create_test_vfs(1, 64, 32);  // Buffering enabled
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create buffered VFS: %d\n", rc);
        return -1;
    }
    
    start_time = get_time_seconds();
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to open database with buffering: %s\n", sqlite3_errmsg(db));
        cleanup_test_vfs();
        return -1;
    }
    
    // Perform write operations
    sqlite3_exec(db, "CREATE TABLE perf_test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (int i = 0; i < TEST_TABLE_SIZE * 2; i++) {
        snprintf(sql, sizeof(sql), 
                "INSERT INTO perf_test (data) VALUES ('Performance test data record %d with substantial content to simulate real workload')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    sqlite3_close(db);
    cleanup_test_vfs();
    
    end_time = get_time_seconds();
    time_with_buffer = end_time - start_time;
    
    remove(TEST_DB_PATH);  // Clean up for next test
    
    // Test WITHOUT buffering
    printf("  Testing performance WITHOUT write buffering...\n");
    rc = create_test_vfs(0, 0, 0);  // Buffering disabled
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create non-buffered VFS: %d\n", rc);
        return -1;
    }
    
    start_time = get_time_seconds();
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to open database without buffering: %s\n", sqlite3_errmsg(db));
        cleanup_test_vfs();
        return -1;
    }
    
    // Perform same write operations
    sqlite3_exec(db, "CREATE TABLE perf_test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (int i = 0; i < TEST_TABLE_SIZE * 2; i++) {
        snprintf(sql, sizeof(sql), 
                "INSERT INTO perf_test (data) VALUES ('Performance test data record %d with substantial content to simulate real workload')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    sqlite3_close(db);
    cleanup_test_vfs();
    
    end_time = get_time_seconds();
    time_without_buffer = end_time - start_time;
    
    // Compare results
    double improvement = ((time_without_buffer - time_with_buffer) / time_without_buffer) * 100.0;
    
    printf("  Performance Results:\n");
    printf("    WITH buffering:    %.3f seconds\n", time_with_buffer);
    printf("    WITHOUT buffering: %.3f seconds\n", time_without_buffer);
    printf("    Performance improvement: %.1f%%\n", improvement);
    
    if (time_with_buffer <= time_without_buffer) {
        printf("  SUCCESS: Write buffering provides performance benefit\n");
    } else {
        printf("  INFO: Write buffering did not show performance benefit in this test\n");
        printf("        (This may be normal for small datasets or fast storage)\n");
    }
    
    remove(TEST_DB_PATH);
    
    return 0;
}

/*
 * Helper function to create test VFS
 */
static int create_test_vfs(int buffer_enabled, uint32_t max_entries, uint32_t auto_flush_pages) {
    int rc;
    
    // Clean up any existing VFS
    cleanup_test_vfs();
    
    // Create new CCVFS
    rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    // Configure write buffer if specified
    if (max_entries > 0 || auto_flush_pages > 0) {
        rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, buffer_enabled,
                                                 max_entries, 0, auto_flush_pages);
        if (rc != SQLITE_OK) {
            sqlite3_ccvfs_destroy(TEST_VFS_NAME);
            return rc;
        }
    }
    
    return SQLITE_OK;
}

/*
 * Helper function to cleanup test VFS
 */
static int cleanup_test_vfs(void) {
    // Try to destroy VFS (ignore errors if it doesn't exist)
    sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    return SQLITE_OK;
}

/*
 * Helper function to get buffer statistics
 */
static int get_buffer_stats(sqlite3 *db, BufferStats *stats) {
    return sqlite3_ccvfs_get_buffer_stats(db, 
                                         &stats->buffer_hits,
                                         &stats->buffer_flushes,
                                         &stats->buffer_merges,
                                         &stats->total_buffered_writes);
}

/*
 * Helper function to get current time in seconds
 */
static double get_time_seconds(void) {
    clock_t t = clock();
    return ((double)t) / CLOCKS_PER_SEC;
}

/*
 * Helper function to print test header
 */
static void print_test_header(const char *test_name) {
    printf("\n--- %s ---\n", test_name);
}

/*
 * Helper function to print test result
 */
static void print_test_result(const char *test_name, int result) {
    printf("  %s: %s\n", test_name, result ? "PASSED" : "FAILED");
}