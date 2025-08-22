#include "unit_test_framework.h"
#include <time.h>

// Global test result tracker
UnitTestResult g_test_result = {0};

// Memory tracking for leak detection
static int allocated_blocks = 0;
static size_t total_allocated = 0;

void unit_test_init(void) {
    memset(&g_test_result, 0, sizeof(g_test_result));
    allocated_blocks = 0;
    total_allocated = 0;
    
    printf("\n");
    printf("===============================================================\n");
    printf("                   CCVFS Unit Test Framework                   \n");
    printf("===============================================================\n");
    printf("\n");
}

void unit_test_cleanup(void) {
    ut_cleanup_temp_files();
    ut_check_memory_leaks();
}

int run_single_test(const UnitTestCase* test_case) {
    if (!test_case || !test_case->function) {
        printf("[ERROR] Invalid test case\n");
        return 0;
    }
    
    printf("\n---------------------------------------------------------------\n");
    printf("[TEST] Test: %s\n", test_case->name);
    if (test_case->description && strlen(test_case->description) > 0) {
        printf("[DESC] Description: %s\n", test_case->description);
    }
    printf("---------------------------------------------------------------\n");
    
    int initial_passed = g_test_result.passed_tests;
    int initial_failed = g_test_result.failed_tests;
    
    clock_t start_time = clock();
    int result = test_case->function(&g_test_result);
    clock_t end_time = clock();
    
    double duration = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    int test_passed = g_test_result.passed_tests - initial_passed;
    int test_failed = g_test_result.failed_tests - initial_failed;
    
    if (result && test_failed == 0) {
        printf("\n[PASS] TEST PASSED: %s (%.3fs, %d assertions)\n", 
               test_case->name, duration, test_passed);
        return 1;
    } else {
        printf("\n[FAIL] TEST FAILED: %s (%.3fs, %d passed, %d failed)\n", 
               test_case->name, duration, test_passed, test_failed);
        if (strlen(g_test_result.failure_message) > 0) {
            printf("[ERROR] Last failure: %s\n", g_test_result.failure_message);
        }
        return 0;
    }
}

int run_all_tests(const UnitTestCase* test_cases, int num_tests) {
    if (!test_cases || num_tests <= 0) {
        printf("[ERROR] No test cases provided\n");
        return 0;
    }
    
    int tests_passed = 0;
    int tests_failed = 0;
    
    printf("[INFO] Running %d unit tests...\n", num_tests);
    
    for (int i = 0; i < num_tests; i++) {
        if (run_single_test(&test_cases[i])) {
            tests_passed++;
        } else {
            tests_failed++;
        }
    }
    
    printf("\n===============================================================\n");
    printf("                        TEST SUMMARY                          \n");
    printf("===============================================================\n");
    printf("[STATS] Test Cases: %d passed, %d failed, %d total\n", 
           tests_passed, tests_failed, num_tests);
    printf("[STATS] Assertions: %d passed, %d failed, %d total\n", 
           g_test_result.passed_tests, g_test_result.failed_tests, g_test_result.total_tests);
    
    if (tests_failed == 0) {
        printf("[SUCCESS] All tests passed!\n");
    } else {
        printf("[FAILED] %d test(s) failed\n", tests_failed);
    }
    printf("===============================================================\n\n");
    
    return tests_failed == 0 ? 1 : 0;
}

void print_test_summary(void) {
    printf("\n[SUMMARY] Final Summary:\n");
    printf("   Total Assertions: %d\n", g_test_result.total_tests);
    printf("   Passed: %d\n", g_test_result.passed_tests);
    printf("   Failed: %d\n", g_test_result.failed_tests);
    
    if (g_test_result.failed_tests == 0) {
        printf("   Status: [PASS] ALL TESTS PASSED\n");
    } else {
        printf("   Status: [FAIL] %d FAILURES\n", g_test_result.failed_tests);
    }
}

void* ut_malloc(size_t size) {
    void* ptr = malloc(size);
    if (ptr) {
        allocated_blocks++;
        total_allocated += size;
    }
    return ptr;
}

void ut_free(void* ptr) {
    if (ptr) {
        free(ptr);
        allocated_blocks--;
    }
}

void ut_check_memory_leaks(void) {
    if (allocated_blocks > 0) {
        printf("\n[WARNING] Memory leak detected: %d blocks still allocated (%zu bytes)\n", 
               allocated_blocks, total_allocated);
    } else {
        printf("\n[OK] No memory leaks detected\n");
    }
}

char* ut_create_temp_filename(const char* prefix) {
    static char filename[256];
    static int counter = 0;
    
    time_t now = time(NULL);
    snprintf(filename, sizeof(filename), "%s_test_%ld_%d.db", 
             prefix ? prefix : "temp", now, ++counter);
    
    return filename;
}

void ut_cleanup_temp_files(void) {
    printf("[CLEANUP] Cleaning up temporary test files...\n");
}