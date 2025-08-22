#ifndef UNIT_TEST_FRAMEWORK_H
#define UNIT_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sqlite3.h>
#include "ccvfs.h"

// Test result counters
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    char current_test[256];
    char failure_message[1024];
} UnitTestResult;

// Test function pointer type
typedef int (*UnitTestFunction)(UnitTestResult* result);

// Test case structure
typedef struct {
    const char* name;
    const char* description;
    UnitTestFunction function;
} UnitTestCase;

// Global test result tracker
extern UnitTestResult g_test_result;

// Test framework functions
void unit_test_init(void);
void unit_test_cleanup(void);
int run_single_test(const UnitTestCase* test_case);
int run_all_tests(const UnitTestCase* test_cases, int num_tests);
void print_test_summary(void);

// Simple assertion macros (without emojis to avoid encoding issues)
#define UT_ASSERT(condition, message) do { \
    g_test_result.total_tests++; \
    if (!(condition)) { \
        snprintf(g_test_result.failure_message, sizeof(g_test_result.failure_message), \
                "ASSERTION FAILED: %s at %s:%d - %s", #condition, __FILE__, __LINE__, message); \
        printf("  [FAIL] %s\n", g_test_result.failure_message); \
        g_test_result.failed_tests++; \
        return 0; \
    } else { \
        printf("  [PASS] %s\n", message); \
        g_test_result.passed_tests++; \
    } \
} while(0)

#define UT_ASSERT_EQUAL(expected, actual, message) do { \
    g_test_result.total_tests++; \
    if ((expected) != (actual)) { \
        snprintf(g_test_result.failure_message, sizeof(g_test_result.failure_message), \
                "ASSERTION FAILED: Expected %d, got %d at %s:%d - %s", \
                (int)(expected), (int)(actual), __FILE__, __LINE__, message); \
        printf("  [FAIL] %s\n", g_test_result.failure_message); \
        g_test_result.failed_tests++; \
        return 0; \
    } else { \
        printf("  [PASS] %s (expected: %d, actual: %d)\n", message, (int)(expected), (int)(actual)); \
        g_test_result.passed_tests++; \
    } \
} while(0)

#define UT_ASSERT_NOT_NULL(ptr, message) do { \
    g_test_result.total_tests++; \
    if ((ptr) == NULL) { \
        snprintf(g_test_result.failure_message, sizeof(g_test_result.failure_message), \
                "ASSERTION FAILED: Expected non-NULL pointer at %s:%d - %s", \
                __FILE__, __LINE__, message); \
        printf("  [FAIL] %s\n", g_test_result.failure_message); \
        g_test_result.failed_tests++; \
        return 0; \
    } else { \
        printf("  [PASS] %s\n", message); \
        g_test_result.passed_tests++; \
    } \
} while(0)

#define UT_ASSERT_NULL(ptr, message) do { \
    g_test_result.total_tests++; \
    if ((ptr) != NULL) { \
        snprintf(g_test_result.failure_message, sizeof(g_test_result.failure_message), \
                "ASSERTION FAILED: Expected NULL pointer at %s:%d - %s", \
                __FILE__, __LINE__, message); \
        printf("  [FAIL] %s\n", g_test_result.failure_message); \
        g_test_result.failed_tests++; \
        return 0; \
    } else { \
        printf("  [PASS] %s\n", message); \
        g_test_result.passed_tests++; \
    } \
} while(0)

#define UT_ASSERT_STRING_EQUAL(expected, actual, message) do { \
    g_test_result.total_tests++; \
    if (strcmp((expected), (actual)) != 0) { \
        snprintf(g_test_result.failure_message, sizeof(g_test_result.failure_message), \
                "ASSERTION FAILED: Expected '%s', got '%s' at %s:%d - %s", \
                expected, actual, __FILE__, __LINE__, message); \
        printf("  [FAIL] %s\n", g_test_result.failure_message); \
        g_test_result.failed_tests++; \
        return 0; \
    } else { \
        printf("  [PASS] %s\n", message); \
        g_test_result.passed_tests++; \
    } \
} while(0)

#define UT_ASSERT_BYTES_EQUAL(expected, actual, length, message) do { \
    g_test_result.total_tests++; \
    if (memcmp((expected), (actual), (length)) != 0) { \
        snprintf(g_test_result.failure_message, sizeof(g_test_result.failure_message), \
                "ASSERTION FAILED: Byte arrays differ at %s:%d - %s", \
                __FILE__, __LINE__, message); \
        printf("  [FAIL] %s\n", g_test_result.failure_message); \
        g_test_result.failed_tests++; \
        return 0; \
    } else { \
        printf("  [PASS] %s\n", message); \
        g_test_result.passed_tests++; \
    } \
} while(0)

// Test helper macros  
#define UT_BEGIN_TEST(test_name) do { \
    snprintf(g_test_result.current_test, sizeof(g_test_result.current_test), "%s", test_name); \
    printf("\n[TEST] Running test: %s\n", test_name); \
} while(0)

#define UT_END_TEST() do { \
    printf("[DONE] Test completed: %s\n", g_test_result.current_test); \
} while(0)

// Memory management helpers for tests
void* ut_malloc(size_t size);
void ut_free(void* ptr);
void ut_check_memory_leaks(void);

// Test utility functions
char* ut_create_temp_filename(const char* prefix);
void ut_cleanup_temp_files(void);

#endif // UNIT_TEST_FRAMEWORK_H