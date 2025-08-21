/*
 * System Test Suite - Unified Entry Point
 * 
 * This file provides a unified entry point for all system tests.
 * It replaces the scattered individual test programs with a single
 * test runner that can be controlled via command line arguments.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "ccvfs.h"
#include "ccvfs_algorithm.h"

// Test result structure
typedef struct {
    const char* name;
    int passed;
    int total;
    char message[512];
} TestResult;

// Forward declarations for test functions
int test_vfs_connection(TestResult* result);
int test_simple_db(TestResult* result);
int test_large_db_stress(TestResult* result);
int test_simple_large(TestResult* result);
int test_hole_detection(TestResult* result);
int test_simple_hole(TestResult* result);
int test_batch_write_buffer(TestResult* result);
int test_simple_buffer(TestResult* result);
int test_db_tools(TestResult* result);

// Test registry
typedef struct {
    const char* name;
    const char* description;
    int (*test_func)(TestResult*);
} TestCase;

static TestCase test_cases[] = {
    {"vfs_connection", "VFS connection and basic operations", test_vfs_connection},
    {"simple_db", "Simple database operations with compression", test_simple_db},
    {"large_db_stress", "Large database stress testing", test_large_db_stress},
    {"simple_large", "Simple large data operations", test_simple_large},
    {"hole_detection", "Space hole detection functionality", test_hole_detection},
    {"simple_hole", "Simple hole management test", test_simple_hole},
    {"batch_write_buffer", "Batch write buffer functionality", test_batch_write_buffer},
    {"simple_buffer", "Simple buffer operations", test_simple_buffer},
    {"db_tools", "Database tools integration test", test_db_tools},
    {NULL, NULL, NULL} // Terminator
};

void print_usage(const char* program_name) {
    printf("Usage: %s [options] [test_name]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help      Show this help message\n");
    printf("  -l, --list      List all available tests\n");
    printf("  -v, --verbose   Enable verbose output\n");
    printf("  -a, --all       Run all tests (default)\n");
    printf("\nAvailable tests:\n");
    
    for (int i = 0; test_cases[i].name != NULL; i++) {
        printf("  %-20s %s\n", test_cases[i].name, test_cases[i].description);
    }
}

void print_test_list() {
    printf("Available system tests:\n");
    for (int i = 0; test_cases[i].name != NULL; i++) {
        printf("  %-20s %s\n", test_cases[i].name, test_cases[i].description);
    }
}

int run_single_test(const char* test_name, int verbose) {
    TestResult result = {0};
    
    // Find the test
    TestCase* test = NULL;
    for (int i = 0; test_cases[i].name != NULL; i++) {
        if (strcmp(test_cases[i].name, test_name) == 0) {
            test = &test_cases[i];
            break;
        }
    }
    
    if (!test) {
        printf("Error: Test '%s' not found\n", test_name);
        return 1;
    }
    
    printf("Running test: %s\n", test->description);
    if (verbose) {
        printf("Test name: %s\n", test->name);
    }
    
    int success = test->test_func(&result);
    
    printf("Test result: %s - %d/%d passed", 
           success ? "PASS" : "FAIL", 
           result.passed, 
           result.total);
    
    if (strlen(result.message) > 0) {
        printf(" - %s", result.message);
    }
    printf("\n");
    
    return success ? 0 : 1;
}

int run_all_tests(int verbose) {
    int total_tests = 0;
    int passed_tests = 0;
    
    printf("Running all system tests...\n");
    printf("========================================\n");
    
    for (int i = 0; test_cases[i].name != NULL; i++) {
        TestResult result = {0};
        
        printf("\n[%d] %s\n", i + 1, test_cases[i].description);
        if (verbose) {
            printf("    Test: %s\n", test_cases[i].name);
        }
        
        int success = test_cases[i].test_func(&result);
        total_tests++;
        
        if (success) {
            passed_tests++;
            printf("    Result: PASS - %d/%d passed", result.passed, result.total);
        } else {
            printf("    Result: FAIL - %d/%d passed", result.passed, result.total);
        }
        
        if (strlen(result.message) > 0) {
            printf(" - %s", result.message);
        }
        printf("\n");
    }
    
    printf("\n========================================\n");
    printf("Overall result: %d/%d tests passed\n", passed_tests, total_tests);
    
    return (passed_tests == total_tests) ? 0 : 1;
}

int main(int argc, char* argv[]) {
    int verbose = 0;
    int run_all = 1;
    const char* specific_test = NULL;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            print_test_list();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            run_all = 1;
        } else if (argv[i][0] != '-') {
            // This is a test name
            specific_test = argv[i];
            run_all = 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Initialize built-in algorithms
    ccvfs_init_builtin_algorithms();
    
    // Run tests
    if (run_all) {
        return run_all_tests(verbose);
    } else if (specific_test) {
        return run_single_test(specific_test, verbose);
    } else {
        printf("No test specified. Use --help for usage information.\n");
        return 1;
    }
}