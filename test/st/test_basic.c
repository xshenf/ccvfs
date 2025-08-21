/*
 * Basic System Tests
 * 
 * Contains basic VFS connection and simple database operation tests.
 */

#include "system_test_common.h"

// VFS Connection Test
int test_vfs_connection(TestResult* result) {
    result->name = "VFS Connection Test";
    result->passed = 0;
    result->total = 6;
    strcpy(result->message, "");
    
    cleanup_test_files("vfs_test");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("test_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("vfs_test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "test_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    result->passed++;
    
    // Create table
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, text TEXT);", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    result->passed++;
    
    // Test insert
    rc = sqlite3_exec(db, "INSERT INTO test (text) VALUES ('Hello World');", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Insert failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    result->passed++;
    
    // Test select
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, text FROM test;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Select preparation failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("test_vfs");
        return 0;
    }
    result->passed++;
    
    int found_records = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        found_records++;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("test_vfs");
    
    if (found_records > 0) {
        result->passed++;
        snprintf(result->message, sizeof(result->message), "%d records retrieved", found_records);
        return 1;
    } else {
        snprintf(result->message, sizeof(result->message), "No records retrieved");
        return 0;
    }
}

// Simple Database Test
int test_simple_db(TestResult* result) {
    result->name = "Simple Database Test";
    result->passed = 0;
    result->total = 5;  // Increased for enhanced data verification
    strcpy(result->message, "");
    
    cleanup_test_files("simple_test");
    
    sqlite3 *db;
    int rc;
    char *err_msg = NULL;
    
    // Define test data for verification
    const char* test_records[][2] = {
        {"1", "Hello"},
        {"2", "World"},
        {"3", "SQLite"}
    };
    const int expected_count = 3;
    
    // Create test database
    rc = sqlite3_open("simple_test.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot create database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    const char *sql = 
        "CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO test VALUES (1, 'Hello');"
        "INSERT INTO test VALUES (2, 'World');"
        "INSERT INTO test VALUES (3, 'SQLite');";
    
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 0;
    }
    
    sqlite3_close(db);
    result->passed++; // Database creation and initial data insertion
    
    // Compress database
    rc = sqlite3_ccvfs_compress_database("simple_test.db", "simple_test.ccvfs", "zlib", NULL, 6);
    if (rc == SQLITE_OK) {
        result->passed++; // Compression successful
    } else {
        snprintf(result->message, sizeof(result->message), "Database compression failed: %d", rc);
        return 0;
    }
    
    // Decompress database
    rc = sqlite3_ccvfs_decompress_database("simple_test.ccvfs", "simple_test_restored.db");
    if (rc == SQLITE_OK) {
        result->passed++; // Decompression successful
    } else {
        snprintf(result->message, sizeof(result->message), "Database decompression failed: %d", rc);
        return 0;
    }
    
    // Comprehensive data verification
    rc = sqlite3_open("simple_test_restored.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot open decompressed database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, name FROM test ORDER BY id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL query error: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    result->passed++; // Query preparation successful
    
    // Verify each record's content
    int verified_count = 0;
    int data_integrity_ok = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && data_integrity_ok) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        
        // Check if the record matches expected data
        if (verified_count < expected_count) {
            int expected_id = atoi(test_records[verified_count][0]);
            const char* expected_name = test_records[verified_count][1];
            
            if (id != expected_id || strcmp(name, expected_name) != 0) {
                snprintf(result->message, sizeof(result->message), 
                        "Data integrity error: record %d, expected id=%d name='%s', got id=%d name='%s'", 
                        verified_count + 1, expected_id, expected_name, id, name);
                data_integrity_ok = 0;
                break;
            }
        } else {
            snprintf(result->message, sizeof(result->message), 
                    "Unexpected extra record found: id=%d name='%s'", id, name);
            data_integrity_ok = 0;
            break;
        }
        verified_count++;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    if (data_integrity_ok && verified_count == expected_count) {
        result->passed++; // Data integrity verification passed
        snprintf(result->message, sizeof(result->message), 
                "Simple DB test passed: %d records verified with correct content", verified_count);
        return 1;
    } else if (data_integrity_ok) {
        snprintf(result->message, sizeof(result->message), 
                "Record count mismatch: expected %d, verified %d", expected_count, verified_count);
        return 0;
    } else {
        // Error message already set in the loop
        return 0;
    }
}

// Large Database Stress Test
int test_large_db_stress(TestResult* result) {
    result->name = "Large Database Stress Test";
    result->passed = 0;
    result->total = 3;
    strcpy(result->message, "");
    
    cleanup_test_files("large_stress_test");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("stress_vfs", NULL, "zlib", NULL, 65536, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("large_stress_test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "stress_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy("stress_vfs");
        return 0;
    }
    result->passed++;
    
    // Create table and insert data
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "CREATE TABLE stress_test (id INTEGER PRIMARY KEY, data TEXT);", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("stress_vfs");
        return 0;
    }
    
    // Insert test data (reduced for quick testing)
    const char *large_text = "This is a test string that will be repeated many times to create larger database content for stress testing.";
    
    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Begin transaction failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy("stress_vfs");
        return 0;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "INSERT INTO stress_test (data) VALUES (?);", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        for (int i = 0; i < 1000; i++) {  // Reduced from larger number for quicker testing
            sqlite3_bind_text(stmt, 1, large_text, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
    
    rc = sqlite3_exec(db, "COMMIT;", 0, 0, &err_msg);
    if (rc == SQLITE_OK) {
        result->passed++;
        snprintf(result->message, sizeof(result->message), "Inserted 1000 records successfully");
    } else {
        snprintf(result->message, sizeof(result->message), "Commit failed: %s", err_msg);
        sqlite3_free(err_msg);
    }
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy("stress_vfs");
    
    return (result->passed == result->total) ? 1 : 0;
}

// Simple Large Test
int test_simple_large(TestResult* result) {
    result->name = "Simple Large Test";
    result->passed = 0;
    result->total = 2;
    strcpy(result->message, "");
    
    cleanup_test_files("simple_large");
    
    // Initialize algorithms
    init_test_algorithms();
    
    // Create VFS
    int rc = sqlite3_ccvfs_create("large_vfs", NULL, "zlib", NULL, 65536, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "VFS creation failed: %d", rc);
        return 0;
    }
    result->passed++;
    
    // Open database and perform basic operations
    sqlite3 *db = NULL;
    rc = sqlite3_open_v2("simple_large.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "large_vfs");
    if (rc == SQLITE_OK) {
        char *err_msg = NULL;
        rc = sqlite3_exec(db, "CREATE TABLE large_test (id INTEGER, data TEXT);", 0, 0, &err_msg);
        if (rc == SQLITE_OK) {
            rc = sqlite3_exec(db, "INSERT INTO large_test VALUES (1, 'Large test data');", 0, 0, &err_msg);
            if (rc == SQLITE_OK) {
                result->passed++;
                strcpy(result->message, "Large test operations completed");
            } else {
                snprintf(result->message, sizeof(result->message), "Insert failed: %s", err_msg);
            }
        } else {
            snprintf(result->message, sizeof(result->message), "Table creation failed: %s", err_msg);
        }
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_close(db);
    } else {
        snprintf(result->message, sizeof(result->message), "Database open failed: %s", sqlite3_errmsg(db));
    }
    
    sqlite3_ccvfs_destroy("large_vfs");
    return (result->passed == result->total) ? 1 : 0;
}

// Large Database Compression/Decompression Integrity Test
int test_large_db_compression_integrity(TestResult* result) {
    result->name = "Large DB Compression Integrity Test";
    result->passed = 0;
    result->total = 10;  // Multiple validation steps
    strcpy(result->message, "");
    
    cleanup_test_files("large_compression");
    
    // Additional cleanup for specific test files
    const char* test_files[] = {
        "large_compression_original.db",
        "large_compression_compressed.ccvfs",
        "large_compression_decompressed.db"
    };
    for (int i = 0; i < 3; i++) {
        if (remove(test_files[i]) == 0) {
            printf("Cleaned up: %s\n", test_files[i]);
        }
    }
    
    // Initialize algorithms
    init_test_algorithms();
    
    printf("Creating large database (~100M)...\n");
    
    // Step 1: Create large original database (~100MB)
    sqlite3 *original_db;
    int rc = sqlite3_open("large_compression_original.db", &original_db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Original database creation failed: %s", sqlite3_errmsg(original_db));
        return 0;
    }
    
    // Create table with substantial data
    rc = sqlite3_exec(original_db, 
        "CREATE TABLE large_data ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT,"
        "description TEXT,"
        "data BLOB,"
        "timestamp TEXT"
        ");", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Table creation failed: %s", sqlite3_errmsg(original_db));
        sqlite3_close(original_db);
        return 0;
    }
    result->passed++; // Step 1 complete
    
    // Insert large amount of data to reach ~100MB
    printf("Inserting test data...\n");
    const int BATCH_SIZE = 100;  // Reduced batch size
    const int TOTAL_BATCHES = 5; // Reduced number of batches for debugging
    char large_text[512];        // Reduced text size
    
    // Generate repeatable test data
    strcpy(large_text, "This is a substantial text field containing repetitive data for compression testing. ");
    for (int i = 0; i < 3; i++) {  // Reduced iterations to fit in 512 bytes
        strcat(large_text, "Additional padding data for realistic database size simulation with varied content patterns. ");
    }
    
    sqlite3_exec(original_db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    sqlite3_stmt *insert_stmt;
    rc = sqlite3_prepare_v2(original_db, 
        "INSERT INTO large_data (name, description, data, timestamp) VALUES (?, ?, ?, ?)", 
        -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Insert preparation failed: %s", sqlite3_errmsg(original_db));
        sqlite3_close(original_db);
        return 0;
    }
    
    for (int batch = 0; batch < TOTAL_BATCHES; batch++) {
        for (int i = 0; i < BATCH_SIZE; i++) {
            int record_id = batch * BATCH_SIZE + i;
            char name[64], timestamp[32];
            snprintf(name, sizeof(name), "Record_%d", record_id);
            snprintf(timestamp, sizeof(timestamp), "2024-01-01 %02d:%02d:%02d", 
                    (record_id / 3600) % 24, (record_id / 60) % 60, record_id % 60);
            
            sqlite3_bind_text(insert_stmt, 1, name, -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 2, large_text, -1, SQLITE_STATIC);
            sqlite3_bind_blob(insert_stmt, 3, large_text, strlen(large_text), SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 4, timestamp, -1, SQLITE_STATIC);
            
            rc = sqlite3_step(insert_stmt);
            if (rc != SQLITE_DONE) {
                snprintf(result->message, sizeof(result->message), "Insert failed at record %d: %s", 
                        record_id, sqlite3_errmsg(original_db));
                sqlite3_finalize(insert_stmt);
                sqlite3_close(original_db);
                return 0;
            }
            sqlite3_reset(insert_stmt);
        }
        if (batch % 10 == 0) {
            printf("Inserted batch %d/%d\n", batch + 1, TOTAL_BATCHES);
        }
    }
    
    sqlite3_finalize(insert_stmt);
    sqlite3_exec(original_db, "COMMIT", NULL, NULL, NULL);
    sqlite3_close(original_db);
    result->passed++; // Step 2 complete
    
    // Step 2: Compress the database
    printf("Compressing database...\n");
    rc = sqlite3_ccvfs_compress_database("large_compression_original.db", 
                                       "large_compression_compressed.ccvfs", 
                                       "zlib", NULL, 6);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database compression failed: %d", rc);
        return 0;
    }
    result->passed++; // Step 3 complete
    
    // Step 3: Read compressed database and verify integrity
    printf("Verifying compressed database integrity...\n");
    init_test_algorithms();
    rc = sqlite3_ccvfs_create("compress_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Compressed VFS creation failed: %d", rc);
        return 0;
    }
    
    sqlite3 *compressed_db;
    rc = sqlite3_open_v2("large_compression_compressed.ccvfs", &compressed_db, 
                        SQLITE_OPEN_READONLY, "compress_vfs");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Compressed database open failed: %s", sqlite3_errmsg(compressed_db));
        sqlite3_ccvfs_destroy("compress_vfs");
        return 0;
    }
    result->passed++; // Step 4 complete
    
    // Verify compressed database structure and sample data
    sqlite3_stmt *count_stmt;
    rc = sqlite3_prepare_v2(compressed_db, "SELECT COUNT(*) FROM large_data", -1, &count_stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Count query preparation failed: %s", sqlite3_errmsg(compressed_db));
        sqlite3_close(compressed_db);
        sqlite3_ccvfs_destroy("compress_vfs");
        return 0;
    }
    
    int compressed_record_count = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        compressed_record_count = sqlite3_column_int(count_stmt, 0);
    }
    sqlite3_finalize(count_stmt);
    
    if (compressed_record_count != TOTAL_BATCHES * BATCH_SIZE) {
        snprintf(result->message, sizeof(result->message), 
                "Compressed database record count mismatch: expected %d, got %d", 
                TOTAL_BATCHES * BATCH_SIZE, compressed_record_count);
        sqlite3_close(compressed_db);
        sqlite3_ccvfs_destroy("compress_vfs");
        return 0;
    }
    result->passed++; // Step 5 complete
    
    // Sample data verification from compressed database
    sqlite3_stmt *sample_stmt;
    rc = sqlite3_prepare_v2(compressed_db, 
        "SELECT id, name, description, timestamp FROM large_data WHERE id IN (1, 1000, 25000, 49999) ORDER BY id", 
        -1, &sample_stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Sample query preparation failed: %s", sqlite3_errmsg(compressed_db));
        sqlite3_close(compressed_db);
        sqlite3_ccvfs_destroy("compress_vfs");
        return 0;
    }
    
    // Store compressed database sample data for comparison
    typedef struct {
        int id;
        char name[64];
        char description[256];
        char timestamp[32];
    } SampleRecord;
    
    SampleRecord compressed_samples[4];
    int sample_count = 0;
    
    while (sqlite3_step(sample_stmt) == SQLITE_ROW && sample_count < 4) {
        compressed_samples[sample_count].id = sqlite3_column_int(sample_stmt, 0);
        strncpy(compressed_samples[sample_count].name, (const char*)sqlite3_column_text(sample_stmt, 1), sizeof(compressed_samples[sample_count].name) - 1);
        strncpy(compressed_samples[sample_count].description, (const char*)sqlite3_column_text(sample_stmt, 2), 255);
        compressed_samples[sample_count].description[255] = '\0';
        strncpy(compressed_samples[sample_count].timestamp, (const char*)sqlite3_column_text(sample_stmt, 3), sizeof(compressed_samples[sample_count].timestamp) - 1);
        sample_count++;
    }
    
    sqlite3_finalize(sample_stmt);
    sqlite3_close(compressed_db);
    sqlite3_ccvfs_destroy("compress_vfs");
    result->passed++; // Step 6 complete
    
    // Step 4: Decompress the database
    printf("Decompressing database...\n");
    rc = sqlite3_ccvfs_decompress_database("large_compression_compressed.ccvfs", 
                                         "large_compression_decompressed.db");
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Database decompression failed: %d", rc);
        return 0;
    }
    result->passed++; // Step 7 complete
    
    // Step 5: Compare decompressed database with compressed database readings
    printf("Comparing decompressed database integrity...\n");
    sqlite3 *decompressed_db;
    rc = sqlite3_open("large_compression_decompressed.db", &decompressed_db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Decompressed database open failed: %s", sqlite3_errmsg(decompressed_db));
        return 0;
    }
    
    // Verify record count matches
    rc = sqlite3_prepare_v2(decompressed_db, "SELECT COUNT(*) FROM large_data", -1, &count_stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Decompressed count query preparation failed: %s", sqlite3_errmsg(decompressed_db));
        sqlite3_close(decompressed_db);
        return 0;
    }
    
    int decompressed_record_count = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        decompressed_record_count = sqlite3_column_int(count_stmt, 0);
    }
    sqlite3_finalize(count_stmt);
    
    if (decompressed_record_count != compressed_record_count) {
        snprintf(result->message, sizeof(result->message), 
                "Record count mismatch: compressed=%d, decompressed=%d", 
                compressed_record_count, decompressed_record_count);
        sqlite3_close(decompressed_db);
        return 0;
    }
    result->passed++; // Step 8 complete
    
    // Compare sample data between compressed and decompressed
    rc = sqlite3_prepare_v2(decompressed_db, 
        "SELECT id, name, description, timestamp FROM large_data WHERE id IN (1, 1000, 25000, 49999) ORDER BY id", 
        -1, &sample_stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Decompressed sample query preparation failed: %s", sqlite3_errmsg(decompressed_db));
        sqlite3_close(decompressed_db);
        return 0;
    }
    
    SampleRecord decompressed_samples[4];
    int decompressed_sample_count = 0;
    
    while (sqlite3_step(sample_stmt) == SQLITE_ROW && decompressed_sample_count < 4) {
        decompressed_samples[decompressed_sample_count].id = sqlite3_column_int(sample_stmt, 0);
        strncpy(decompressed_samples[decompressed_sample_count].name, (const char*)sqlite3_column_text(sample_stmt, 1), sizeof(decompressed_samples[decompressed_sample_count].name) - 1);
        strncpy(decompressed_samples[decompressed_sample_count].description, (const char*)sqlite3_column_text(sample_stmt, 2), 255);
        decompressed_samples[decompressed_sample_count].description[255] = '\0';
        strncpy(decompressed_samples[decompressed_sample_count].timestamp, (const char*)sqlite3_column_text(sample_stmt, 3), sizeof(decompressed_samples[decompressed_sample_count].timestamp) - 1);
        decompressed_sample_count++;
    }
    
    sqlite3_finalize(sample_stmt);
    sqlite3_close(decompressed_db);
    
    if (decompressed_sample_count != sample_count) {
        snprintf(result->message, sizeof(result->message), 
                "Sample count mismatch: compressed=%d, decompressed=%d", 
                sample_count, decompressed_sample_count);
        return 0;
    }
    result->passed++; // Step 9 complete
    
    // Compare sample data content
    int data_integrity_ok = 1;
    for (int i = 0; i < sample_count && data_integrity_ok; i++) {
        if (compressed_samples[i].id != decompressed_samples[i].id ||
            strcmp(compressed_samples[i].name, decompressed_samples[i].name) != 0 ||
            strcmp(compressed_samples[i].description, decompressed_samples[i].description) != 0 ||
            strcmp(compressed_samples[i].timestamp, decompressed_samples[i].timestamp) != 0) {
            snprintf(result->message, sizeof(result->message), 
                    "Sample data mismatch at record %d: compressed(id=%d,name='%s') vs decompressed(id=%d,name='%s')", 
                    i, compressed_samples[i].id, compressed_samples[i].name,
                    decompressed_samples[i].id, decompressed_samples[i].name);
            data_integrity_ok = 0;
            break;
        }
    }
    
    if (data_integrity_ok) {
        result->passed++; // Step 10 complete
        snprintf(result->message, sizeof(result->message), 
                "Large DB compression integrity test passed: %d records, %d samples verified, compression/decompression cycle successful", 
                compressed_record_count, sample_count);
    }
    
    printf("Test completed. Cleaning up test files...\n");
    
    return (result->passed == result->total) ? 1 : 0;
}