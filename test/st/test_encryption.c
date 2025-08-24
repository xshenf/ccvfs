/*
 * Encryption/Decryption System Tests
 * 
 * Contains comprehensive tests for encryption and decryption functionality.
 */

#include <sys/stat.h>
#include "system_test_common.h"

// Basic Encryption/Decryption Test
int test_basic_encryption(TestResult* result) {
    result->name = "Basic Encryption/Decryption Test";
    result->passed = 0;
    result->total = 8;
    strcpy(result->message, "");
    
    cleanup_test_files("encrypt_basic");
    
    sqlite3 *db;
    int rc;
    char *err_msg = NULL;
    
    // Define test data for verification
    const char* test_records[][2] = {
        {"1", "Secret Data 1"},
        {"2", "Confidential Info"},
        {"3", "Encrypted Content"},
        {"4", "Private Message"}
    };
    const int expected_count = 4;
    const char* test_key = "1234567890ABCDEF"; // 16-byte hex string representing 8-byte key
    
    // Step 1: Create test database with sensitive data
    rc = sqlite3_open("encrypt_basic.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot create database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    const char *sql = 
        "CREATE TABLE sensitive_data (id INTEGER PRIMARY KEY, content TEXT);"
        "INSERT INTO sensitive_data VALUES (1, 'Secret Data 1');"
        "INSERT INTO sensitive_data VALUES (2, 'Confidential Info');"
        "INSERT INTO sensitive_data VALUES (3, 'Encrypted Content');"
        "INSERT INTO sensitive_data VALUES (4, 'Private Message');";
    
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 0;
    }
    
    sqlite3_close(db);
    result->passed++; // Database creation and initial data insertion
    
    // Step 2: Encrypt database with AES-128
#ifdef HAVE_OPENSSL
    rc = sqlite3_ccvfs_create_and_compress_encrypt(
        "encrypt_vfs",
        NULL,  // No compression
        CCVFS_ENCRYPT_AES128,
        "encrypt_basic.db",
        "encrypt_basic.dbe",
        (const unsigned char*)test_key,
        strlen(test_key),  // Use full length of hex string (16 bytes)
        0  // Default page size
    );
#else
    snprintf(result->message, sizeof(result->message), "OpenSSL not available for encryption");
    return 0;
#endif
    
    if (rc == SQLITE_OK) {
        result->passed++; // Encryption successful
    } else {
        snprintf(result->message, sizeof(result->message), "Database encryption failed: %d", rc);
        return 0;
    }
    
    // Step 3: Verify encrypted file exists and is different from original
    FILE *original = fopen("encrypt_basic.db", "rb");
    FILE *encrypted = fopen("encrypt_basic.dbe", "rb");
    
    if (!original || !encrypted) {
        snprintf(result->message, sizeof(result->message), "Cannot open files for comparison");
        if (original) fclose(original);
        if (encrypted) fclose(encrypted);
        return 0;
    }
    
    // Read first 100 bytes to verify they're different
    unsigned char orig_buf[100], enc_buf[100];
    size_t orig_read = fread(orig_buf, 1, 100, original);
    size_t enc_read = fread(enc_buf, 1, 100, encrypted);
    fclose(original);
    fclose(encrypted);
    
    if (orig_read > 0 && enc_read > 0 && memcmp(orig_buf, enc_buf, orig_read < enc_read ? orig_read : enc_read) != 0) {
        result->passed++; // Files are different (encryption worked)
    } else {
        snprintf(result->message, sizeof(result->message), "Encrypted file is identical to original");
        return 0;
    }
    
    // Step 4: Decrypt database back
#ifdef HAVE_OPENSSL
    rc = sqlite3_ccvfs_create_and_decompress_decrypt(
        "decrypt_vfs",
        NULL,  // No compression
        CCVFS_ENCRYPT_AES128,
        "encrypt_basic.dbe",
        "encrypt_basic_decrypted.db",
        (const unsigned char*)test_key,
        strlen(test_key)  // Use full length of hex string (16 bytes)
    );
#else
    snprintf(result->message, sizeof(result->message), "OpenSSL not available for decryption");
    return 0;
#endif
    
    if (rc == SQLITE_OK) {
        result->passed++; // Decryption successful
    } else {
        snprintf(result->message, sizeof(result->message), "Database decryption failed: %d", rc);
        return 0;
    }
    
    // Step 5: Verify decrypted database content
    rc = sqlite3_open("encrypt_basic_decrypted.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot open decrypted database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, content FROM sensitive_data ORDER BY id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL query error: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    result->passed++; // Query preparation successful
    
    // Step 6: Verify each record's content
    int verified_count = 0;
    int data_integrity_ok = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && data_integrity_ok) {
        int id = sqlite3_column_int(stmt, 0);
        const char* content = (const char*)sqlite3_column_text(stmt, 1);
        
        if (verified_count < expected_count) {
            int expected_id = atoi(test_records[verified_count][0]);
            const char* expected_content = test_records[verified_count][1];
            
            if (id != expected_id || strcmp(content, expected_content) != 0) {
                snprintf(result->message, sizeof(result->message), 
                        "Data integrity error: record %d, expected id=%d content='%s', got id=%d content='%s'", 
                        verified_count + 1, expected_id, expected_content, id, content);
                data_integrity_ok = 0;
                break;
            }
        }
        verified_count++;
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    if (data_integrity_ok && verified_count == expected_count) {
        result->passed++; // Data integrity verification successful
    } else if (data_integrity_ok) {
        snprintf(result->message, sizeof(result->message), 
                "Record count mismatch: expected %d, got %d", expected_count, verified_count);
        return 0;
    }
    
    // Step 7: Verify file sizes are reasonable
    struct stat orig_stat, dec_stat;
    if (stat("encrypt_basic.db", &orig_stat) == 0 && stat("encrypt_basic_decrypted.db", &dec_stat) == 0) {
        if (orig_stat.st_size == dec_stat.st_size) {
            result->passed++; // File sizes match
        } else {
            snprintf(result->message, sizeof(result->message), 
                    "File size mismatch: original=%ld, decrypted=%ld", orig_stat.st_size, dec_stat.st_size);
            return 0;
        }
    } else {
        snprintf(result->message, sizeof(result->message), "Cannot stat files for size comparison");
        return 0;
    }
    
    // Step 8: Test wrong key decryption (should fail)
    const char* wrong_key = "FEDCBA0987654321";
#ifdef HAVE_OPENSSL
    rc = sqlite3_ccvfs_create_and_decompress_decrypt(
        "decrypt_wrong_vfs",
        NULL,
        CCVFS_ENCRYPT_AES128,
        "encrypt_basic.dbe",
        "encrypt_basic_wrong.db",
        (const unsigned char*)wrong_key,
        strlen(wrong_key)  // Use full length of hex string (16 bytes)
    );
#endif
    
    if (rc != SQLITE_OK) {
        result->passed++; // Wrong key correctly failed
        snprintf(result->message, sizeof(result->message), "All encryption/decryption tests passed, wrong key correctly rejected");
        return 1;
    } else {
        snprintf(result->message, sizeof(result->message), "Wrong key should have failed but succeeded");
        return 0;
    }
}

// AES-256 Encryption Test
int test_aes256_encryption(TestResult* result) {
    result->name = "AES-256 Encryption Test";
    result->passed = 0;
    result->total = 6;
    strcpy(result->message, "");
    
    cleanup_test_files("encrypt_aes256");
    sqlite3 *db;
    int rc;
    char *err_msg = NULL;
    
    const char* test_key_256 = "0123456789ABCDEF0123456789ABCDEF"; // 32-byte hex key for AES-256
    
    // Step 1: Create test database
    rc = sqlite3_open("encrypt_aes256.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot create database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    const char *sql = 
        "CREATE TABLE aes256_test (id INTEGER PRIMARY KEY, data BLOB);"
        "INSERT INTO aes256_test VALUES (1, X'DEADBEEF');"
        "INSERT INTO aes256_test VALUES (2, X'CAFEBABE');"
        "INSERT INTO aes256_test VALUES (3, X'FEEDFACE');";
    
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 0;
    }
    
    sqlite3_close(db);
    result->passed++; // Database creation successful
    
    // Step 2: Encrypt with AES-256
#ifdef HAVE_OPENSSL
    rc = sqlite3_ccvfs_create_and_compress_encrypt(
        "encrypt_aes256_vfs",
        NULL,
        CCVFS_ENCRYPT_AES256,
        "encrypt_aes256.db",
        "encrypt_aes256.dbe",
        (const unsigned char*)test_key_256,
        strlen(test_key_256),  // Use full length of hex string (32 bytes)
        0
    );
#else
    snprintf(result->message, sizeof(result->message), "OpenSSL not available for AES-256");
    return 0;
#endif
    
    if (rc == SQLITE_OK) {
        result->passed++; // AES-256 encryption successful
    } else {
        snprintf(result->message, sizeof(result->message), "AES-256 encryption failed: %d", rc);
        return 0;
    }
    
    // Step 3: Verify encryption worked (file is different)
    FILE *original = fopen("encrypt_aes256.db", "rb");
    FILE *encrypted = fopen("encrypt_aes256.dbe", "rb");
    
    if (original && encrypted) {
        unsigned char orig_buf[50], enc_buf[50];
        size_t orig_read = fread(orig_buf, 1, 50, original);
        size_t enc_read = fread(enc_buf, 1, 50, encrypted);
        fclose(original);
        fclose(encrypted);
        
        if (orig_read > 0 && enc_read > 0 && memcmp(orig_buf, enc_buf, orig_read < enc_read ? orig_read : enc_read) != 0) {
            result->passed++; // Files are different
        } else {
            snprintf(result->message, sizeof(result->message), "AES-256 encrypted file identical to original");
            return 0;
        }
    } else {
        snprintf(result->message, sizeof(result->message), "Cannot open files for AES-256 comparison");
        if (original) fclose(original);
        if (encrypted) fclose(encrypted);
        return 0;
    }
    
    // Step 4: Decrypt with AES-256
#ifdef HAVE_OPENSSL
    rc = sqlite3_ccvfs_create_and_decompress_decrypt(
        "decrypt_aes256_vfs",
        NULL,
        CCVFS_ENCRYPT_AES256,
        "encrypt_aes256.dbe",
        "encrypt_aes256_decrypted.db",
        (const unsigned char*)test_key_256,
        strlen(test_key_256)  // Use full length of hex string (32 bytes)
    );
#endif
    
    if (rc == SQLITE_OK) {
        result->passed++; // AES-256 decryption successful
    } else {
        snprintf(result->message, sizeof(result->message), "AES-256 decryption failed: %d", rc);
        return 0;
    }
    
    // Step 5: Verify decrypted data
    rc = sqlite3_open("encrypt_aes256_decrypted.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot open AES-256 decrypted database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM aes256_test", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "AES-256 query error: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    
    int record_count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        record_count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    if (record_count == 3) {
        result->passed++; // Correct record count
    } else {
        snprintf(result->message, sizeof(result->message), "AES-256 record count mismatch: expected 3, got %d", record_count);
        return 0;
    }
    
    // Step 6: Verify file size consistency
    struct stat orig_stat, dec_stat;
    if (stat("encrypt_aes256.db", &orig_stat) == 0 && stat("encrypt_aes256_decrypted.db", &dec_stat) == 0) {
        if (orig_stat.st_size == dec_stat.st_size) {
            result->passed++; // File sizes match
            snprintf(result->message, sizeof(result->message), "AES-256 encryption/decryption successful, all tests passed");
            return 1;
        } else {
            snprintf(result->message, sizeof(result->message), "AES-256 file size mismatch: original=%ld, decrypted=%ld", orig_stat.st_size, dec_stat.st_size);
            return 0;
        }
    } else {
        snprintf(result->message, sizeof(result->message), "Cannot stat AES-256 files");
        return 0;
    }
}

// Key Length Auto-Completion Test
int test_key_auto_completion(TestResult* result) {
    result->name = "Key Length Auto-Completion Test";
    result->passed = 0;
    result->total = 5;
    strcpy(result->message, "");
    
    cleanup_test_files("key_completion");
    
    sqlite3 *db;
    int rc;
    char *err_msg = NULL;
    
    // Test different key lengths that should auto-complete
    const char* short_key = "1230"; // 2 bytes, should repeat to fill 16 bytes
    
    // Step 1: Create test database
    rc = sqlite3_open("key_completion.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot create database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    const char *sql = 
        "CREATE TABLE key_test (id INTEGER, message TEXT);"
        "INSERT INTO key_test VALUES (1, 'Short key test message');";
    
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 0;
    }
    
    sqlite3_close(db);
    result->passed++; // Database creation successful
    
    // Step 2: Test encryption with short key (should auto-complete)
#ifdef HAVE_OPENSSL
    rc = sqlite3_ccvfs_create_and_compress_encrypt(
        "short_key_vfs",
        NULL,
        CCVFS_ENCRYPT_AES128,
        "key_completion.db",
        "key_completion_short.dbe",
        (const unsigned char*)short_key,
        strlen(short_key) / 2,  // 2 bytes
        0
    );
#else
    snprintf(result->message, sizeof(result->message), "OpenSSL not available");
    return 0;
#endif
    
    if (rc == SQLITE_OK) {
        result->passed++; // Short key encryption successful
    } else {
        snprintf(result->message, sizeof(result->message), "Short key encryption failed: %d", rc);
        return 0;
    }
    
    // Step 3: Test decryption with same short key
#ifdef HAVE_OPENSSL
    rc = sqlite3_ccvfs_create_and_decompress_decrypt(
        "short_key_decrypt_vfs",
        NULL,
        CCVFS_ENCRYPT_AES128,
        "key_completion_short.dbe",
        "key_completion_short_dec.db",
        (const unsigned char*)short_key,
        strlen(short_key) / 2
    );
#endif
    
    if (rc == SQLITE_OK) {
        result->passed++; // Short key decryption successful
    } else {
        snprintf(result->message, sizeof(result->message), "Short key decryption failed: %d", rc);
        return 0;
    }
    
    // Step 4: Verify decrypted content
    rc = sqlite3_open("key_completion_short_dec.db", &db);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Cannot open short key decrypted database: %s", sqlite3_errmsg(db));
        return 0;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT message FROM key_test WHERE id = 1", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(result->message, sizeof(result->message), "Short key query error: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    
    int found_message = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* message = (const char*)sqlite3_column_text(stmt, 0);
        if (message && strcmp(message, "Short key test message") == 0) {
            found_message = 1;
        }
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    if (found_message) {
        result->passed++; // Message retrieved correctly
    } else {
        snprintf(result->message, sizeof(result->message), "Short key decrypted message incorrect");
        return 0;
    }
    
    // Step 5: Test that equivalent expanded key works the same way
    // The 2-byte key "1230" should expand to "12121212121212121212121212121212" (16 bytes)
    // Using the repetitive padding algorithm: expanded_key[i] = key[i % key_len]
    const char* expanded_key = "12121212121212121212121212121212"; // 16 bytes, equivalent to repeating "12"
    
#ifdef HAVE_OPENSSL
    rc = sqlite3_ccvfs_create_and_decompress_decrypt(
        "expanded_key_vfs",
        NULL,
        CCVFS_ENCRYPT_AES128,
        "key_completion_short.dbe",  // Same encrypted file
        "key_completion_expanded_dec.db",
        (const unsigned char*)expanded_key,
        strlen(expanded_key) / 2  // 16 bytes
    );
#endif
    
    if (rc == SQLITE_OK) {
        result->passed++; // Expanded key decryption successful
        snprintf(result->message, sizeof(result->message), "Key auto-completion works correctly with different key lengths");
        return 1;
    } else {
        snprintf(result->message, sizeof(result->message), "Expanded key decryption failed: %d", rc);
        return 0;
    }
}