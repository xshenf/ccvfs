#include "unit_test_framework.h"
#include "ccvfs.h"

// Test VFS-level key management functions

static int test_basic_key_management(void) {
    unsigned char test_key[16] = "test_secret_key";
    unsigned char retrieved_key[32];
    int result;
    
    // Create a test VFS first
    int create_rc = sqlite3_ccvfs_create("test_vfs", NULL, NULL, NULL, 0, 0);
    UT_ASSERT_EQUAL(SQLITE_OK, create_rc, "Should create VFS successfully");
    
    int set_rc = sqlite3_ccvfs_set_key("test_vfs", test_key, 16);
    UT_ASSERT_EQUAL(SQLITE_OK, set_rc, "Should set key successfully");
    
    result = sqlite3_ccvfs_get_key("test_vfs", retrieved_key, sizeof(retrieved_key));
    
    UT_ASSERT_EQUAL(16, result, "Should return correct key length");
    UT_ASSERT_BYTES_EQUAL(test_key, retrieved_key, 16, "Retrieved key should match set key");
    
    // Cleanup
    sqlite3_ccvfs_destroy("test_vfs");
    
    return 1;
}

static int test_null_parameter_handling(void) {
    unsigned char test_key[16] = "test_key_123456";
    unsigned char buffer[32];
    int result;
    
    // Test with non-existent VFS
    result = sqlite3_ccvfs_get_key("non_existent", buffer, sizeof(buffer));
    UT_ASSERT_EQUAL(0, result, "Should return 0 for non-existent VFS");
    
    // Test with NULL VFS name
    result = sqlite3_ccvfs_set_key(NULL, test_key, 16);
    UT_ASSERT_EQUAL(SQLITE_ERROR, result, "Should return error for NULL VFS name");
    
    result = sqlite3_ccvfs_get_key(NULL, buffer, 16);
    UT_ASSERT_EQUAL(0, result, "Should return 0 for NULL VFS name");
    
    // Create test VFS
    sqlite3_ccvfs_create("test_vfs2", NULL, NULL, NULL, 0, 0);
    
    // Test with NULL key buffer
    result = sqlite3_ccvfs_get_key("test_vfs2", NULL, 16);
    UT_ASSERT_EQUAL(0, result, "Should return 0 for NULL buffer");
    
    // Test with zero-length key
    result = sqlite3_ccvfs_set_key("test_vfs2", test_key, 0);
    UT_ASSERT_EQUAL(SQLITE_ERROR, result, "Should return error for zero-length key");
    
    // Cleanup
    sqlite3_ccvfs_destroy("test_vfs2");
    
    return 1;
}

static int test_key_overwriting(void) {
    unsigned char key1[16] = "first_key_123456";
    unsigned char key2[16] = "second_key_67890";
    unsigned char retrieved_key[32];
    int result;
    
    // Create test VFS
    sqlite3_ccvfs_create("test_vfs3", NULL, NULL, NULL, 0, 0);
    
    sqlite3_ccvfs_set_key("test_vfs3", key1, 16);
    result = sqlite3_ccvfs_get_key("test_vfs3", retrieved_key, sizeof(retrieved_key));
    UT_ASSERT_EQUAL(16, result, "Should get first key");
    UT_ASSERT_BYTES_EQUAL(key1, retrieved_key, 16, "Should match first key");
    
    sqlite3_ccvfs_set_key("test_vfs3", key2, 16);
    result = sqlite3_ccvfs_get_key("test_vfs3", retrieved_key, sizeof(retrieved_key));
    UT_ASSERT_EQUAL(16, result, "Should get second key");
    UT_ASSERT_BYTES_EQUAL(key2, retrieved_key, 16, "Should match second key");
    
    // Test key clearing
    sqlite3_ccvfs_clear_key("test_vfs3");
    result = sqlite3_ccvfs_get_key("test_vfs3", retrieved_key, sizeof(retrieved_key));
    UT_ASSERT_EQUAL(0, result, "Should return 0 after clearing key");
    
    // Cleanup
    sqlite3_ccvfs_destroy("test_vfs3");
    
    return 1;
}

int test_encryption_key_mgmt(UnitTestResult* result) {
    UT_BEGIN_TEST("VFS-level Encryption Key Management");
    
    if (!test_basic_key_management()) return 0;
    if (!test_null_parameter_handling()) return 0;
    if (!test_key_overwriting()) return 0;
    
    UT_END_TEST();
    return 1;
}