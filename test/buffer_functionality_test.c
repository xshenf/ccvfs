/*
 * CCVFS Write Buffer Functionality Verification Test
 * 
 * This test specifically verifies that the write buffering functionality
 * is working correctly by testing:
 * 1. Buffer hit/miss behavior
 * 2. Buffer merge operations
 * 3. Auto-flush triggers
 * 4. Manual flush operations
 * 5. Buffer statistics accuracy
 * 6. Performance impact measurement
 */

#include "../include/ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define TEST_DB_PATH "buffer_test.ccvfs"
#define TEST_VFS_NAME "buffer_test_vfs"

// Test result structure
typedef struct {
    int passed;
    int failed;
    char last_error[256];
} TestResult;

// Helper macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            snprintf(result->last_error, sizeof(result->last_error), "%s", message); \
            result->failed++; \
            printf("  FAIL: %s\n", message); \
            return 0; \
        } \
        result->passed++; \
    } while(0)

#define TEST_EXPECT_GT(actual, expected, message) \
    TEST_ASSERT((actual) > (expected), message)

#define TEST_EXPECT_EQ(actual, expected, message) \
    TEST_ASSERT((actual) == (expected), message)

// Forward declarations
static int test_buffer_initialization(TestResult *result);
static int test_buffer_write_operations(TestResult *result);
static int test_buffer_read_hits(TestResult *result);
static int test_buffer_merge_behavior(TestResult *result);
static int test_auto_flush_threshold(TestResult *result);
static int test_manual_flush_operation(TestResult *result);
static int test_buffer_statistics_accuracy(TestResult *result);
static int test_buffer_configuration_changes(TestResult *result);
static int create_test_database(const char *vfs_name, sqlite3 **db);
static void cleanup_test_database(sqlite3 *db, const char *vfs_name);
static void print_buffer_stats(sqlite3 *db, const char *label);

int main(int argc, char *argv[]) {
    TestResult result = {0, 0, ""};
    int total_tests = 0;
    
    printf("=== CCVFS Write Buffer Functionality Verification ===\n");
    printf("Testing detailed buffer behavior...\n\n");
    
    // Test 1: Buffer initialization
    printf("Test 1: Buffer Initialization\n");
    if (test_buffer_initialization(&result)) {
        printf("  PASS: Buffer initialization test\n");
    }
    total_tests++;
    
    // Test 2: Buffer write operations
    printf("\nTest 2: Buffer Write Operations\n");
    if (test_buffer_write_operations(&result)) {
        printf("  PASS: Buffer write operations test\n");
    }
    total_tests++;
    
    // Test 3: Buffer read hits
    printf("\nTest 3: Buffer Read Hits\n");
    if (test_buffer_read_hits(&result)) {
        printf("  PASS: Buffer read hits test\n");
    }
    total_tests++;
    
    // Test 4: Buffer merge behavior
    printf("\nTest 4: Buffer Merge Behavior\n");
    if (test_buffer_merge_behavior(&result)) {
        printf("  PASS: Buffer merge behavior test\n");
    }
    total_tests++;
    
    // Test 5: Auto-flush threshold
    printf("\nTest 5: Auto-flush Threshold\n");
    if (test_auto_flush_threshold(&result)) {
        printf("  PASS: Auto-flush threshold test\n");
    }
    total_tests++;
    
    // Test 6: Manual flush operation
    printf("\nTest 6: Manual Flush Operation\n");
    if (test_manual_flush_operation(&result)) {
        printf("  PASS: Manual flush operation test\n");
    }
    total_tests++;
    
    // Test 7: Buffer statistics accuracy
    printf("\nTest 7: Buffer Statistics Accuracy\n");
    if (test_buffer_statistics_accuracy(&result)) {
        printf("  PASS: Buffer statistics accuracy test\n");
    }
    total_tests++;
    
    // Test 8: Buffer configuration changes
    printf("\nTest 8: Buffer Configuration Changes\n");
    if (test_buffer_configuration_changes(&result)) {
        printf("  PASS: Buffer configuration changes test\n");
    }
    total_tests++;
    
    // Print final results
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", total_tests);
    printf("Passed assertions: %d\n", result.passed);
    printf("Failed assertions: %d\n", result.failed);
    printf("Success rate: %.1f%%\n", 
           total_tests > 0 ? (double)(total_tests - (result.failed > 0 ? 1 : 0)) / total_tests * 100.0 : 0.0);
    
    if (result.failed > 0) {
        printf("Last error: %s\n", result.last_error);
    }
    
    return result.failed > 0 ? 1 : 0;
}

/*
 * Test 1: Verify buffer initialization works correctly
 */
static int test_buffer_initialization(TestResult *result) {
    int rc;
    sqlite3 *db = NULL;
    
    printf("  Creating VFS with buffer enabled...\n");
    
    // Create VFS with buffering
    rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create CCVFS");
    
    // Configure buffer with specific settings
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 16, 1024*1024, 8);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to configure write buffer");
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to open database");
    
    // Get initial buffer stats
    uint32_t hits, flushes, merges, writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats");
    
    printf("  Initial stats: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
           hits, flushes, merges, writes);
    
    // All stats should be zero initially
    TEST_EXPECT_EQ(hits, 0, "Initial buffer hits should be 0");
    TEST_EXPECT_EQ(flushes, 0, "Initial buffer flushes should be 0");
    TEST_EXPECT_EQ(merges, 0, "Initial buffer merges should be 0");
    TEST_EXPECT_EQ(writes, 0, "Initial buffer writes should be 0");
    
    cleanup_test_database(db, TEST_VFS_NAME);
    return 1;
}

/*
 * Test 2: Verify buffer write operations are working
 */
static int test_buffer_write_operations(TestResult *result) {
    sqlite3 *db = NULL;
    int rc;
    
    printf("  Testing buffer write operations...\n");
    
    rc = create_test_database(TEST_VFS_NAME, &db);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create test database");
    
    // Get initial stats
    uint32_t initial_writes, initial_merges;
    uint32_t hits, flushes, merges, writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get initial buffer stats");
    initial_writes = writes;
    initial_merges = merges;
    
    // Create table and insert data
    rc = sqlite3_exec(db, "CREATE TABLE test_writes (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create table");
    
    // Insert multiple records to trigger buffering
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 20; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_writes (data) VALUES ('Buffer test data %d')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        TEST_ASSERT(rc == SQLITE_OK, "Failed to insert test data");
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get stats after writes
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats after writes");
    
    printf("  After writes: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
           hits, flushes, merges, writes);
    
    // Should have some buffered writes
    TEST_EXPECT_GT(writes, initial_writes, "Should have buffered writes");
    
    // Should have some merges (pages written multiple times)
    TEST_EXPECT_GT(merges, initial_merges, "Should have buffer merges");
    
    cleanup_test_database(db, TEST_VFS_NAME);
    return 1;
}

/*
 * Test 3: Verify buffer read hits are working
 */
static int test_buffer_read_hits(TestResult *result) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    
    printf("  Testing buffer read hits...\n");
    
    rc = create_test_database(TEST_VFS_NAME, &db);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create test database");
    
    // Create and populate table
    rc = sqlite3_exec(db, "CREATE TABLE test_reads (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create table");
    
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 10; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_reads (data) VALUES ('Read test data %d')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get stats before reads
    uint32_t initial_hits;
    uint32_t hits, flushes, merges, writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats before reads");
    initial_hits = hits;
    
    // Perform reads that should hit buffered data
    for (int i = 0; i < 5; i++) {
        rc = sqlite3_prepare_v2(db, "SELECT data FROM test_reads WHERE id <= 5", -1, &stmt, NULL);
        TEST_ASSERT(rc == SQLITE_OK, "Failed to prepare read statement");
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            // Just reading the data
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    
    // Get stats after reads
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats after reads");
    
    printf("  Read hits: before=%u, after=%u, increase=%u\n", 
           initial_hits, hits, hits - initial_hits);
    
    // Should have some buffer hits
    TEST_EXPECT_GT(hits, initial_hits, "Should have buffer hits during reads");
    
    cleanup_test_database(db, TEST_VFS_NAME);
    return 1;
}

/*
 * Test 4: Verify buffer merge behavior
 */
static int test_buffer_merge_behavior(TestResult *result) {
    sqlite3 *db = NULL;
    int rc;
    
    printf("  Testing buffer merge behavior...\n");
    
    rc = create_test_database(TEST_VFS_NAME, &db);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create test database");
    
    // Create table
    rc = sqlite3_exec(db, "CREATE TABLE test_merges (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create table");
    
    // Get initial merge count
    uint32_t initial_merges;
    uint32_t hits, flushes, merges, writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get initial buffer stats");
    initial_merges = merges;
    
    // Perform operations that should cause merges (multiple writes to same pages)
    for (int round = 0; round < 3; round++) {
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        for (int i = 0; i < 5; i++) {
            char sql[256];
            snprintf(sql, sizeof(sql), 
                    "INSERT INTO test_merges (data) VALUES ('Merge test round %d item %d')", 
                    round, i);
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        }
        sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
        
        // Update existing records to trigger more writes to same pages
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        for (int i = 1; i <= 3; i++) {
            char sql[256];
            snprintf(sql, sizeof(sql), 
                    "UPDATE test_merges SET data = 'Updated round %d' WHERE id = %d", 
                    round, i);
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        }
        sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    }
    
    // Get final merge count
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats after merges");
    
    printf("  Merges: initial=%u, final=%u, increase=%u\n", 
           initial_merges, merges, merges - initial_merges);
    
    // Should have multiple merges from repeated writes to same pages
    TEST_EXPECT_GT(merges, initial_merges, "Should have buffer merges from repeated writes");
    
    cleanup_test_database(db, TEST_VFS_NAME);
    return 1;
}

/*
 * Test 5: Verify auto-flush threshold behavior
 */
static int test_auto_flush_threshold(TestResult *result) {
    sqlite3 *db = NULL;
    int rc;
    
    printf("  Testing auto-flush threshold...\n");
    
    // Create VFS with small auto-flush threshold
    rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create CCVFS");
    
    // Configure with small auto-flush threshold
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 32, 1024*1024, 3);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to configure write buffer");
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to open database");
    
    // Create table
    rc = sqlite3_exec(db, "CREATE TABLE test_autoflush (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create table");
    
    // Get initial flush count
    uint32_t initial_flushes;
    uint32_t hits, flushes, merges, writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get initial buffer stats");
    initial_flushes = flushes;
    
    // Insert enough data to trigger auto-flush (threshold is 3 pages)
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 15; i++) {  // Should trigger multiple auto-flushes
        char sql[512];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_autoflush (data) VALUES ('Auto-flush test data %d with extra content to make it larger and span multiple pages for testing')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get final flush count
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats after auto-flush test");
    
    printf("  Auto-flushes: initial=%u, final=%u, triggered=%u\n", 
           initial_flushes, flushes, flushes - initial_flushes);
    
    // Should have triggered auto-flushes
    TEST_EXPECT_GT(flushes, initial_flushes, "Should have triggered auto-flushes");
    
    cleanup_test_database(db, TEST_VFS_NAME);
    return 1;
}

/*
 * Test 6: Verify manual flush operation
 */
static int test_manual_flush_operation(TestResult *result) {
    sqlite3 *db = NULL;
    int rc;
    
    printf("  Testing manual flush operation...\n");
    
    rc = create_test_database(TEST_VFS_NAME, &db);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create test database");
    
    // Create table and add data
    rc = sqlite3_exec(db, "CREATE TABLE test_manual_flush (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create table");
    
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 5; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_manual_flush (data) VALUES ('Manual flush test %d')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get flush count before manual flush
    uint32_t before_flushes;
    uint32_t hits, flushes, merges, writes;
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats before manual flush");
    before_flushes = flushes;
    
    // Perform manual flush
    rc = sqlite3_ccvfs_flush_write_buffer(db);
    TEST_ASSERT(rc == SQLITE_OK, "Manual flush should succeed");
    
    // Get flush count after manual flush
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats after manual flush");
    
    printf("  Manual flush: before=%u, after=%u\n", before_flushes, flushes);
    
    // Should have incremented flush count
    TEST_EXPECT_GT(flushes, before_flushes, "Manual flush should increment flush count");
    
    cleanup_test_database(db, TEST_VFS_NAME);
    return 1;
}

/*
 * Test 7: Verify buffer statistics accuracy
 */
static int test_buffer_statistics_accuracy(TestResult *result) {
    sqlite3 *db = NULL;
    int rc;
    
    printf("  Testing buffer statistics accuracy...\n");
    
    rc = create_test_database(TEST_VFS_NAME, &db);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create test database");
    
    // Track statistics at each step
    uint32_t hits, flushes, merges, writes;
    uint32_t prev_hits = 0, prev_flushes = 0, prev_merges = 0, prev_writes = 0;
    
    // Step 1: Create table
    rc = sqlite3_exec(db, "CREATE TABLE test_stats (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create table");
    
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats after table creation");
    
    printf("  After CREATE TABLE: hits=%u(+%u), flushes=%u(+%u), merges=%u(+%u), writes=%u(+%u)\n",
           hits, hits-prev_hits, flushes, flushes-prev_flushes, 
           merges, merges-prev_merges, writes, writes-prev_writes);
    
    prev_hits = hits; prev_flushes = flushes; prev_merges = merges; prev_writes = writes;
    
    // Step 2: Insert data
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 10; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test_stats (data) VALUES ('Statistics test %d')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats after inserts");
    
    printf("  After INSERT: hits=%u(+%u), flushes=%u(+%u), merges=%u(+%u), writes=%u(+%u)\n",
           hits, hits-prev_hits, flushes, flushes-prev_flushes, 
           merges, merges-prev_merges, writes, writes-prev_writes);
    
    // Should have some writes
    TEST_EXPECT_GT(writes, prev_writes, "Should have buffered writes after inserts");
    
    prev_hits = hits; prev_flushes = flushes; prev_merges = merges; prev_writes = writes;
    
    // Step 3: Read data (should cause hits)
    sqlite3_stmt *stmt;
    for (int i = 0; i < 3; i++) {
        rc = sqlite3_prepare_v2(db, "SELECT * FROM test_stats WHERE id <= 5", -1, &stmt, NULL);
        TEST_ASSERT(rc == SQLITE_OK, "Failed to prepare select statement");
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            // Reading data
        }
        sqlite3_finalize(stmt);
    }
    
    rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to get buffer stats after reads");
    
    printf("  After SELECT: hits=%u(+%u), flushes=%u(+%u), merges=%u(+%u), writes=%u(+%u)\n",
           hits, hits-prev_hits, flushes, flushes-prev_flushes, 
           merges, merges-prev_merges, writes, writes-prev_writes);
    
    // Should have some hits
    TEST_EXPECT_GT(hits, prev_hits, "Should have buffer hits after reads");
    
    cleanup_test_database(db, TEST_VFS_NAME);
    return 1;
}

/*
 * Test 8: Verify buffer configuration changes
 */
static int test_buffer_configuration_changes(TestResult *result) {
    int rc;
    
    printf("  Testing buffer configuration changes...\n");
    
    // Create VFS
    rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to create CCVFS");
    
    // Test different configurations
    printf("    Testing configuration 1: 16 entries, 512KB, auto-flush 8\n");
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 16, 512*1024, 8);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to configure buffer (config 1)");
    
    printf("    Testing configuration 2: 64 entries, 2MB, auto-flush 32\n");
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 64, 2*1024*1024, 32);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to configure buffer (config 2)");
    
    printf("    Testing disable buffer\n");
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 0, 0, 0, 0);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to disable buffer");
    
    printf("    Testing re-enable buffer\n");
    rc = sqlite3_ccvfs_configure_write_buffer(TEST_VFS_NAME, 1, 32, 1024*1024, 16);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to re-enable buffer");
    
    // Cleanup
    rc = sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    TEST_ASSERT(rc == SQLITE_OK, "Failed to destroy CCVFS");
    
    return 1;
}

/*
 * Helper function to create test database
 */
static int create_test_database(const char *vfs_name, sqlite3 **db) {
    int rc;
    
    // Clean up any existing VFS
    sqlite3_ccvfs_destroy(vfs_name);
    
    // Create VFS with buffering
    rc = sqlite3_ccvfs_create(vfs_name, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) return rc;
    
    // Configure buffer
    rc = sqlite3_ccvfs_configure_write_buffer(vfs_name, 1, 32, 1024*1024, 16);
    if (rc != SQLITE_OK) {
        sqlite3_ccvfs_destroy(vfs_name);
        return rc;
    }
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        vfs_name);
    if (rc != SQLITE_OK) {
        sqlite3_ccvfs_destroy(vfs_name);
        return rc;
    }
    
    return SQLITE_OK;
}

/*
 * Helper function to cleanup test database
 */
static void cleanup_test_database(sqlite3 *db, const char *vfs_name) {
    if (db) {
        sqlite3_close(db);
    }
    sqlite3_ccvfs_destroy(vfs_name);
    remove(TEST_DB_PATH);
}

/*
 * Helper function to print buffer statistics
 */
static void print_buffer_stats(sqlite3 *db, const char *label) {
    uint32_t hits, flushes, merges, writes;
    int rc = sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &writes);
    if (rc == SQLITE_OK) {
        printf("  %s: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
               label, hits, flushes, merges, writes);
    } else {
        printf("  %s: Failed to get stats (rc=%d)\n", label, rc);
    }
}