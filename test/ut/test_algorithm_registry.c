#include "unit_test_framework.h"
#include "ccvfs_algorithm.h"

static int test_compression_algorithm_lookup(void) {
    CompressAlgorithm* alg;
    
    alg = ccvfs_find_compress_algorithm("zlib");
    UT_ASSERT_NOT_NULL(alg, "Should find zlib compression algorithm");
    UT_ASSERT_STRING_EQUAL("zlib", alg->name, "Algorithm name should be 'zlib'");
    
    alg = ccvfs_find_compress_algorithm("nonexistent");
    UT_ASSERT_NULL(alg, "Should not find non-existent algorithm");
    
    alg = ccvfs_find_compress_algorithm(NULL);
    UT_ASSERT_NULL(alg, "Should return NULL for NULL algorithm name");
    
    return 1;
}

static int test_encryption_algorithm_lookup(void) {
    EncryptAlgorithm* alg;
    
    alg = ccvfs_find_encrypt_algorithm("aes256");
    if (alg) {
        UT_ASSERT_NOT_NULL(alg, "Should find aes256 encryption algorithm");
        UT_ASSERT_STRING_EQUAL("aes256", alg->name, "Algorithm name should be 'aes256'");
    } else {
        printf("[INFO] No encryption algorithms found (OpenSSL not available)\n");
    }
    
    alg = ccvfs_find_encrypt_algorithm("nonexistent");
    UT_ASSERT_NULL(alg, "Should not find non-existent algorithm");
    
    return 1;
}

static int test_algorithm_listing(void) {
    char buffer[512];
    int len;
    
    len = ccvfs_list_compress_algorithms(buffer, sizeof(buffer));
    UT_ASSERT(len > 0, "Should have at least one compression algorithm");
    UT_ASSERT(strstr(buffer, "zlib") != NULL, "Should list zlib algorithm");
    
    len = ccvfs_list_encrypt_algorithms(buffer, sizeof(buffer));
    if (len > 0) {
        UT_ASSERT(strstr(buffer, "aes256") != NULL, "Should list aes256 algorithm if available");
    } else {
        printf("[INFO] No encryption algorithms in list (OpenSSL not available)\n");
    }
    
    return 1;
}

int test_algorithm_registry(UnitTestResult* result) {
    UT_BEGIN_TEST("Algorithm Registry");
    
    if (!test_compression_algorithm_lookup()) return 0;
    if (!test_encryption_algorithm_lookup()) return 0;
    if (!test_algorithm_listing()) return 0;
    
    UT_END_TEST();
    return 1;
}