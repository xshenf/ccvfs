#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include "sqlite3.h"

#ifdef __cplusplus
extern "C" {
#endif

// Failed test information
typedef struct FailedTest {
    char suite_name[128];
    char test_name[128];
    char file_name[256];
    int line_number;
    char error_message[512];
    struct FailedTest *next;
} FailedTest;

// Test result structure
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    int skipped_tests;
    double total_time;
    FailedTest *failed_test_list;
} TestResult;

// Test suite structure
typedef struct TestSuite {
    const char *name;
    int (*setup)(void);
    int (*teardown)(void);
    struct TestSuite *next;
} TestSuite;

// Test case structure
typedef struct TestCase {
    const char *name;
    const char *suite_name;
    int (*test_func)(void);
    struct TestCase *next;
} TestCase;

// Global test state
extern TestResult g_test_result;
extern TestSuite *g_test_suites;
extern TestCase *g_test_cases;

// Color codes for output
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_RESET   "\033[0m"

// Test macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf(COLOR_RED "❌ FAIL: %s" COLOR_RESET "\n", message); \
            printf("   File: %s, Line: %d\n", __FILE__, __LINE__); \
            record_test_failure("Unknown", "Unknown", __FILE__, __LINE__, message); \
            return 0; \
        } else { \
            printf(COLOR_GREEN "✅ PASS: %s" COLOR_RESET "\n", message); \
        } \
    } while(0)

#define TEST_ASSERT_EQ(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            printf(COLOR_RED "❌ FAIL: %s" COLOR_RESET "\n", message); \
            printf("   Expected: %d, Actual: %d\n", (int)(expected), (int)(actual)); \
            printf("   File: %s, Line: %d\n", __FILE__, __LINE__); \
            char detailed_msg[512]; \
            snprintf(detailed_msg, sizeof(detailed_msg), "%s (Expected: %d, Actual: %d)", message, (int)(expected), (int)(actual)); \
            record_test_failure("Unknown", "Unknown", __FILE__, __LINE__, detailed_msg); \
            return 0; \
        } else { \
            printf(COLOR_GREEN "✅ PASS: %s" COLOR_RESET "\n", message); \
        } \
    } while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, message) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            printf(COLOR_RED "❌ FAIL: %s" COLOR_RESET "\n", message); \
            printf("   Expected: \"%s\", Actual: \"%s\"\n", (expected), (actual)); \
            printf("   File: %s, Line: %d\n", __FILE__, __LINE__); \
            return 0; \
        } else { \
            printf(COLOR_GREEN "✅ PASS: %s" COLOR_RESET "\n", message); \
        } \
    } while(0)

#define TEST_ASSERT_NULL(ptr, message) \
    do { \
        if ((ptr) != NULL) { \
            printf(COLOR_RED "❌ FAIL: %s" COLOR_RESET "\n", message); \
            printf("   Expected NULL, got %p\n", (ptr)); \
            printf("   File: %s, Line: %d\n", __FILE__, __LINE__); \
            return 0; \
        } else { \
            printf(COLOR_GREEN "✅ PASS: %s" COLOR_RESET "\n", message); \
        } \
    } while(0)

#define TEST_ASSERT_NOT_NULL(ptr, message) \
    do { \
        if ((ptr) == NULL) { \
            printf(COLOR_RED "❌ FAIL: %s" COLOR_RESET "\n", message); \
            printf("   Expected non-NULL pointer\n"); \
            printf("   File: %s, Line: %d\n", __FILE__, __LINE__); \
            return 0; \
        } else { \
            printf(COLOR_GREEN "✅ PASS: %s" COLOR_RESET "\n", message); \
        } \
    } while(0)

#define TEST_START(test_name) \
    printf(COLOR_CYAN "\n🧪 Running test: %s" COLOR_RESET "\n", test_name); \
    g_test_result.total_tests++

#define TEST_END() \
    printf("   Test completed\n")

#define TEST_SKIP(message) \
    do { \
        printf(COLOR_YELLOW "⏭️  SKIP: %s" COLOR_RESET "\n", message); \
        g_test_result.skipped_tests++; \
        return 1; \
    } while(0)

// Test registration macros
#define REGISTER_TEST_SUITE(suite_name, setup_func, teardown_func) \
    register_test_suite(suite_name, setup_func, teardown_func)

#define REGISTER_TEST_CASE(suite_name, test_name, test_func) \
    register_test_case(suite_name, test_name, test_func)

// Function declarations
void init_test_framework(void);
void cleanup_test_framework(void);
void register_test_suite(const char *name, int (*setup)(void), int (*teardown)(void));
void register_test_case(const char *suite_name, const char *test_name, int (*test_func)(void));
int run_all_tests(void);
int run_test_suite(const char *suite_name);
void print_test_summary(void);
void record_test_failure(const char *suite_name, const char *test_name, const char *file_name, int line_number, const char *error_message);

// Utility functions
int file_exists(const char *filename);
long get_file_size(const char *filename);
void cleanup_test_files(const char **files, int count);
double get_time_diff(clock_t start, clock_t end);
int verify_database_content(const char *db_path, int expected_count);
void print_sqlite_error(sqlite3 *db, const char *operation, int rc);

#ifdef __cplusplus
}
#endif

#endif /* TEST_FRAMEWORK_H */