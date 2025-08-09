/*
 * Simple CCVFS Write Buffer Verification Test
 * 
 * This test verifies that the write buffering functionality is working
 * by checking key indicators:
 * 1. Buffer statistics are being tracked
 * 2. Buffer hits occur during reads
 * 3. Buffer merges occur during repeated writes
 * 4. Auto-flush triggers work
 * 5. Manual flush works
 */

#include "../include/ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST_DB_PATH "simple_buffer_test.ccvfs"
#define TEST_VFS_NAME "simple_buffer_vfs"

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

static int setup_test_vfs(int buffer_enabled) {
    int rc;
    
    // Clean up any existing VFS
    sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    
    // Create new CCVFS
    rc = sqlite3_ccvfs_create(TEST_VFS_NAME, NULL, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create CCVFS: %d\n", rc);
        return rc;
    }
    
    // Configure batch writer
    if (buffer_enabled) {
        rc = sqlite3_ccvfs_configure_batch_writer(TEST_VFS_NAME, 1, 16, 1024*1024, 8);
    } else {
        rc = sqlite3_ccvfs_configure_batch_writer(TEST_VFS_NAME, 0, 0, 0, 0);
    }
    
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to configure write buffer: %d\n", rc);
        sqlite3_ccvfs_destroy(TEST_VFS_NAME);
        return rc;
    }
    
    return SQLITE_OK;
}

static void cleanup_test_vfs(void) {
    sqlite3_ccvfs_destroy(TEST_VFS_NAME);
    remove(TEST_DB_PATH);
}

static void test_buffer_statistics_tracking(void) {
    printf("\nTest 1: Buffer Statistics Tracking\n");
    
    sqlite3 *db = NULL;
    int rc;
    uint32_t hits, flushes, merges, writes, memory_used, page_count;
    
    // Setup VFS with buffering
    rc = setup_test_vfs(1);
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Get initial stats
    rc = sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes, &merges, &writes, &memory_used, &page_count);
    VERIFY(rc == SQLITE_OK, "Batch writer stats API works");
    VERIFY(hits == 0 && flushes == 0 && merges == 0 && writes == 0, 
           "Initial stats are zero");
    
    // Create table and insert data
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    VERIFY(rc == SQLITE_OK, "Table creation successful");
    
    // Insert some records
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 10; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('Test record %d')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Check stats after writes
    rc = sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes, &merges, &writes, &memory_used, &page_count);
    VERIFY(rc == SQLITE_OK, "Batch writer stats retrieved after writes");
    VERIFY(writes > 0, "Batch writer writes were recorded");
    
    printf("  Stats after writes: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
           hits, flushes, merges, writes);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_buffer_hits_during_reads(void) {
    printf("\nTest 2: Buffer Hits During Reads\n");
    
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    uint32_t hits_before, hits_after, flushes, merges, writes, memory_used, page_count;
    
    // Setup VFS with buffering
    rc = setup_test_vfs(1);
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Create and populate table
    sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 5; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO test (data) VALUES ('Read test %d')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get hits before reads
    sqlite3_ccvfs_get_batch_writer_stats(db, &hits_before, &flushes, &merges, &writes, &memory_used, &page_count);
    
    // Perform reads that should hit buffered data
    for (int i = 0; i < 3; i++) {
        rc = sqlite3_prepare_v2(db, "SELECT data FROM test WHERE id <= 3", -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                // Just reading the data
            }
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }
    
    // Get hits after reads
    sqlite3_ccvfs_get_batch_writer_stats(db, &hits_after, &flushes, &merges, &writes, &memory_used, &page_count);
    
    VERIFY(hits_after > hits_before, "Buffer hits increased during reads");
    printf("  Buffer hits: before=%u, after=%u, increase=%u\n", 
           hits_before, hits_after, hits_after - hits_before);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_buffer_merges(void) {
    printf("\nTest 3: Buffer Merges During Repeated Writes\n");
    
    sqlite3 *db = NULL;
    int rc;
    uint32_t hits, flushes, merges_before, merges_after, writes, memory_used, page_count;
    
    // Setup VFS with buffering
    rc = setup_test_vfs(1);
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Create table
    sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    
    // Get initial merge count
    sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes, &merges_before, &writes, &memory_used, &page_count);
    
    // Perform operations that should cause merges (multiple writes to same pages)
    for (int round = 0; round < 3; round++) {
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        
        // Insert records
        for (int i = 0; i < 3; i++) {
            char sql[256];
            snprintf(sql, sizeof(sql), 
                    "INSERT INTO test (data) VALUES ('Merge test round %d item %d')", round, i);
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        }
        
        // Update existing records (causes writes to same pages)
        if (round > 0) {
            for (int i = 1; i <= 2; i++) {
                char sql[256];
                snprintf(sql, sizeof(sql), 
                        "UPDATE test SET data = 'Updated in round %d' WHERE id = %d", round, i);
                sqlite3_exec(db, sql, NULL, NULL, NULL);
            }
        }
        
        sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    }
    
    // Get final merge count
    sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes, &merges_after, &writes, &memory_used, &page_count);
    
    VERIFY(merges_after > merges_before, "Buffer merges occurred");
    printf("  Buffer merges: before=%u, after=%u, increase=%u\n", 
           merges_before, merges_after, merges_after - merges_before);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_auto_flush_behavior(void) {
    printf("\nTest 4: Auto-flush Behavior\n");
    
    sqlite3 *db = NULL;
    int rc;
    uint32_t hits, flushes_before, flushes_after, merges, writes, memory_used, page_count;
    
    // Setup VFS with small auto-flush threshold
    rc = setup_test_vfs(1);
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    // Configure with small auto-flush threshold (1 page)
    rc = sqlite3_ccvfs_configure_batch_writer(TEST_VFS_NAME, 1, 16, 1024*1024, 1);
    VERIFY(rc == SQLITE_OK, "Auto-flush threshold configured");
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Get initial flush count BEFORE any operations
    sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes_before, &merges, &writes, &memory_used, &page_count);
    
    // Create table (this might trigger auto-flush)
    sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    
    // Insert data to trigger auto-flush (threshold is 1 page)
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    for (int i = 0; i < 5; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test (data) VALUES ('Auto-flush test record %d with extra content to make it larger')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL);
    
    // Get final flush count
    sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes_after, &merges, &writes, &memory_used, &page_count);
    
    VERIFY(flushes_after > flushes_before, "Auto-flush was triggered");
    printf("  Auto-flushes: before=%u, after=%u, triggered=%u\n", 
           flushes_before, flushes_after, flushes_after - flushes_before);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_manual_flush(void) {
    printf("\nTest 5: Manual Flush Operation\n");
    
    sqlite3 *db = NULL;
    int rc;
    uint32_t hits, flushes_before, flushes_after, merges, writes, memory_used, page_count;
    
    // Setup VFS with buffering
    rc = setup_test_vfs(1);
    VERIFY(rc == SQLITE_OK, "VFS setup successful");
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened successfully");
    
    // Create table and add some data
    sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT INTO test (data) VALUES ('Manual flush test')", NULL, NULL, NULL);
    
    // Get flush count before manual flush
    sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes_before, &merges, &writes, &memory_used, &page_count);
    
    // Perform manual flush
    rc = sqlite3_ccvfs_flush_batch_writer(db);
    VERIFY(rc == SQLITE_OK, "Manual flush succeeded");
    
    // Get flush count after manual flush
    sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes_after, &merges, &writes, &memory_used, &page_count);
    
    VERIFY(flushes_after > flushes_before, "Manual flush incremented flush count");
    printf("  Manual flush: before=%u, after=%u\n", flushes_before, flushes_after);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

static void test_buffer_disabled_vs_enabled(void) {
    printf("\nTest 6: Buffer Disabled vs Enabled Comparison\n");
    
    sqlite3 *db = NULL;
    int rc;
    uint32_t hits, flushes, merges, writes, memory_used, page_count;
    
    // Test with buffer DISABLED
    printf("  Testing with buffer DISABLED...\n");
    rc = setup_test_vfs(0);  // Buffer disabled
    VERIFY(rc == SQLITE_OK, "VFS setup (disabled) successful");
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened (disabled) successfully");
    
    // Create table and insert data
    sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT INTO test (data) VALUES ('Disabled buffer test')", NULL, NULL, NULL);
    
    // Check stats - should be zero or minimal
    rc = sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes, &merges, &writes, &memory_used, &page_count);
    VERIFY(rc == SQLITE_OK, "Batch writer stats API works when disabled");
    printf("    Disabled stats: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
           hits, flushes, merges, writes);
    
    sqlite3_close(db);
    cleanup_test_vfs();
    
    // Test with buffer ENABLED
    printf("  Testing with buffer ENABLED...\n");
    rc = setup_test_vfs(1);  // Buffer enabled
    VERIFY(rc == SQLITE_OK, "VFS setup (enabled) successful");
    
    rc = sqlite3_open_v2(TEST_DB_PATH, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        TEST_VFS_NAME);
    VERIFY(rc == SQLITE_OK, "Database opened (enabled) successfully");
    
    // Create table and insert data
    sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT INTO test (data) VALUES ('Enabled buffer test')", NULL, NULL, NULL);
    
    // Check stats - should show activity
    rc = sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes, &merges, &writes, &memory_used, &page_count);
    VERIFY(rc == SQLITE_OK, "Batch writer stats API works when enabled");
    VERIFY(writes > 0, "Batch writer shows write activity when enabled");
    printf("    Enabled stats: hits=%u, flushes=%u, merges=%u, writes=%u\n", 
           hits, flushes, merges, writes);
    
    sqlite3_close(db);
    cleanup_test_vfs();
}

int main(int argc, char *argv[]) {
    printf("=== CCVFS Write Buffer Verification Test ===\n");
    printf("Testing core buffer functionality...\n");
    
    // Run all tests
    test_buffer_statistics_tracking();
    test_buffer_hits_during_reads();
    test_buffer_merges();
    test_auto_flush_behavior();
    test_manual_flush();
    test_buffer_disabled_vs_enabled();
    
    // Print summary
    printf("\n=== Test Summary ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", 
           (tests_passed + tests_failed) > 0 ? 
           (double)tests_passed / (tests_passed + tests_failed) * 100.0 : 0.0);
    
    if (tests_failed == 0) {
        printf("\n✓ All buffer functionality tests PASSED!\n");
        printf("✓ Write buffering is working correctly.\n");
        printf("✓ Buffer statistics are being tracked properly.\n");
        printf("✓ Buffer hits, merges, and flushes are functioning.\n");
        return 0;
    } else {
        printf("\n✗ Some tests FAILED. Buffer functionality may have issues.\n");
        return 1;
    }
}