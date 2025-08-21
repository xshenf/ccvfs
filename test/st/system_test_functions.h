/*
 * System Test Function Declarations
 * 
 * Forward declarations for all test functions used in the system test suite.
 */

#ifndef SYSTEM_TEST_FUNCTIONS_H
#define SYSTEM_TEST_FUNCTIONS_H

#include "system_test_common.h"

// Basic tests (test_basic.c)
int test_vfs_connection(TestResult* result);
int test_simple_db(TestResult* result);
int test_large_db_stress(TestResult* result);
int test_simple_large(TestResult* result);

// Storage tests (test_storage.c)
int test_hole_detection(TestResult* result);
int test_simple_hole(TestResult* result);

// Buffer tests (test_buffer.c)
int test_batch_write_buffer(TestResult* result);
int test_simple_buffer(TestResult* result);

// Tools tests (test_tools.c)
int test_db_tools(TestResult* result);

#endif // SYSTEM_TEST_FUNCTIONS_H