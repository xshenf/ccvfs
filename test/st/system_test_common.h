/*
 * System Test Common Definitions
 * 
 * Common structures, utilities and helper functions used across
 * all system test implementations.
 */

#ifndef SYSTEM_TEST_COMMON_H
#define SYSTEM_TEST_COMMON_H

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

// Test function pointer type
typedef int (*TestFunction)(TestResult* result);

// Test case structure
typedef struct {
    const char* name;
    const char* description;
    TestFunction function;
} TestCase;

// Utility function to clean up test files
void cleanup_test_files(const char* prefix);

// Initialize algorithms
void init_test_algorithms(void);

#endif // SYSTEM_TEST_COMMON_H