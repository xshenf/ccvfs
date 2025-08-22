#include "unit_test_framework.h"

// External declarations for key management functions
void ccvfs_set_encryption_key(const unsigned char *key, int keyLen);
int ccvfs_get_encryption_key(unsigned char *key, int maxLen);

static int test_basic_key_management(void) {
    unsigned char test_key[16] = "test_secret_key";
    unsigned char retrieved_key[32];
    int result;
    
    ccvfs_set_encryption_key(test_key, 16);
    result = ccvfs_get_encryption_key(retrieved_key, sizeof(retrieved_key));
    
    UT_ASSERT_EQUAL(16, result, "Should return correct key length");
    UT_ASSERT_BYTES_EQUAL(test_key, retrieved_key, 16, "Retrieved key should match set key");
    
    return 1;
}

static int test_null_parameter_handling(void) {
    unsigned char test_key[16] = "test_key_123456";
    unsigned char buffer[32];
    int result;
    
    ccvfs_set_encryption_key(NULL, 16);
    result = ccvfs_get_encryption_key(buffer, sizeof(buffer));
    UT_ASSERT_EQUAL(0, result, "Should return 0 for NULL key");
    
    ccvfs_set_encryption_key(test_key, 0);
    result = ccvfs_get_encryption_key(buffer, sizeof(buffer));
    UT_ASSERT_EQUAL(0, result, "Should return 0 for zero-length key");
    
    ccvfs_set_encryption_key(test_key, 16);
    result = ccvfs_get_encryption_key(NULL, 16);
    UT_ASSERT_EQUAL(0, result, "Should return 0 for NULL buffer");
    
    return 1;
}

static int test_key_overwriting(void) {
    unsigned char key1[16] = "first_key_123456";
    unsigned char key2[16] = "second_key_67890";
    unsigned char retrieved_key[32];
    int result;
    
    ccvfs_set_encryption_key(key1, 16);
    result = ccvfs_get_encryption_key(retrieved_key, sizeof(retrieved_key));
    UT_ASSERT_EQUAL(16, result, "Should get first key");
    UT_ASSERT_BYTES_EQUAL(key1, retrieved_key, 16, "Should match first key");
    
    ccvfs_set_encryption_key(key2, 16);
    result = ccvfs_get_encryption_key(retrieved_key, sizeof(retrieved_key));
    UT_ASSERT_EQUAL(16, result, "Should get second key");
    UT_ASSERT_BYTES_EQUAL(key2, retrieved_key, 16, "Should match second key");
    
    return 1;
}

int test_encryption_key_mgmt(UnitTestResult* result) {
    UT_BEGIN_TEST("Encryption Key Management");
    
    if (!test_basic_key_management()) return 0;
    if (!test_null_parameter_handling()) return 0;
    if (!test_key_overwriting()) return 0;
    
    UT_END_TEST();
    return 1;
}