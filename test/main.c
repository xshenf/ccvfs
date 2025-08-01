#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include "../include/compress_vfs.h"
#include "zlib.h"

int main(int argc, char **argv) {
    sqlite3 *db;
    int rc;
    
    // Check for debug mode
    int debug_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            break;
        }
    }
    
    if (debug_mode) {
        printf("Debug mode enabled\n");
    }
    
    printf("SQLite version: %s\n", sqlite3_libversion());
    
    // Create compression VFS with new API
    rc = sqlite3_ccvfs_create("ccvfs", NULL, "rle", "xor", CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create compression VFS: %d\n", rc);
        return 1;
    }
    
    printf("Successfully created compression VFS\n");
    
    // Open database using compression VFS
    rc = sqlite3_open_v2("test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "ccvfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("ccvfs");
        return 1;
    }
    
    printf("Successfully opened database using compression VFS\n");
    
    // Execute SQL operations
    char *errmsg = 0;
    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY, data TEXT);", 
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create table: %s\n", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("ccvfs");
        return 1;
    }
    
    printf("Successfully created table\n");
    
    // Insert test data
    const char *insert_sql = "INSERT INTO test (data) VALUES (?)";
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("ccvfs");
        return 1;
    }
    
    // Insert some test data
    const char *test_data[] = {
        "Hello, World!",
        "This is a test of compression VFS",
        "SQLite with compression and encryption",
        "Multiple rows of data for testing",
        "Performance and functionality verification"
    };
    
    for (int i = 0; i < 5; i++) {
        sqlite3_bind_text(stmt, 1, test_data[i], -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "Failed to insert data: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            sqlite3_ccvfs_destroy("ccvfs");
            return 1;
        }
        sqlite3_reset(stmt);
    }
    
    sqlite3_finalize(stmt);
    printf("Inserted 5 rows of test data\n");
    
    // Query data back
    const char *select_sql = "SELECT id, data FROM test ORDER BY id";
    rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare select statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("ccvfs");
        return 1;
    }
    
    printf("\nQuerying data:\n");
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *data = (const char*)sqlite3_column_text(stmt, 1);
        printf("ID: %d, Data: %s\n", id, data);
    }
    
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error querying data: %s\n", sqlite3_errmsg(db));
    } else {
        printf("Successfully queried all data\n");
    }
    
    sqlite3_finalize(stmt);
    
    // Get compression statistics if available
    CCVFSStats stats;
    rc = sqlite3_ccvfs_get_stats("test.db", &stats);
    if (rc == SQLITE_OK) {
        printf("\nCompression Statistics:\n");
        printf("Original size: %llu bytes\n", (unsigned long long)stats.original_size);
        printf("Compressed size: %llu bytes\n", (unsigned long long)stats.compressed_size);
        printf("Compression ratio: %u%%\n", stats.compression_ratio);
        printf("Total blocks: %u\n", stats.total_blocks);
        printf("Compression algorithm: %s\n", stats.compress_algorithm);
        printf("Encryption algorithm: %s\n", stats.encrypt_algorithm);
    }
    
    // Close database
    sqlite3_close(db);
    printf("Closed database\n");
    
    // Destroy VFS
    sqlite3_ccvfs_destroy("ccvfs");
    printf("Destroyed VFS\n");
    
    printf("Test completed successfully!\n");
    return 0;
}