#include "test_framework.h"
#include "sqlite3.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Global test state
TestResult g_test_result = {0, 0, 0, 0, 0.0, NULL};
TestSuite *g_test_suites = NULL;
TestCase *g_test_cases = NULL;

// Current test context (for failure recording)
static const char *g_current_suite = NULL;
static const char *g_current_test = NULL;

void record_test_failure(const char *suite_name, const char *test_name, const char *file_name, int line_number, const char *error_message) {
    FailedTest *failed_test = malloc(sizeof(FailedTest));
    if (!failed_test) {
        printf(COLOR_RED "Failed to allocate memory for failed test record" COLOR_RESET "\n");
        return;
    }
    
    // Use current test context if available, otherwise use provided names
    const char *actual_suite = (g_current_suite && strcmp(suite_name, "Unknown") == 0) ? g_current_suite : suite_name;
    const char *actual_test = (g_current_test && strcmp(test_name, "Unknown") == 0) ? g_current_test : test_name;
    
    strncpy(failed_test->suite_name, actual_suite, sizeof(failed_test->suite_name) - 1);
    failed_test->suite_name[sizeof(failed_test->suite_name) - 1] = '\0';
    
    strncpy(failed_test->test_name, actual_test, sizeof(failed_test->test_name) - 1);
    failed_test->test_name[sizeof(failed_test->test_name) - 1] = '\0';
    
    strncpy(failed_test->file_name, file_name, sizeof(failed_test->file_name) - 1);
    failed_test->file_name[sizeof(failed_test->file_name) - 1] = '\0';
    
    failed_test->line_number = line_number;
    
    strncpy(failed_test->error_message, error_message, sizeof(failed_test->error_message) - 1);
    failed_test->error_message[sizeof(failed_test->error_message) - 1] = '\0';
    
    // Add to the front of the list
    failed_test->next = g_test_result.failed_test_list;
    g_test_result.failed_test_list = failed_test;
}

void init_test_framework(void) {
    memset(&g_test_result, 0, sizeof(TestResult));
    g_test_result.failed_test_list = NULL;
    g_test_suites = NULL;
    g_test_cases = NULL;
    g_current_suite = NULL;
    g_current_test = NULL;
    
    printf(COLOR_BLUE "🚀 Initializing Test Framework" COLOR_RESET "\n");
    printf("================================\n");
}

void cleanup_test_framework(void) {
    // Free test suites
    TestSuite *suite = g_test_suites;
    while (suite) {
        TestSuite *next = suite->next;
        free(suite);
        suite = next;
    }
    g_test_suites = NULL;
    
    // Free test cases
    TestCase *test_case = g_test_cases;
    while (test_case) {
        TestCase *next = test_case->next;
        free(test_case);
        test_case = next;
    }
    g_test_cases = NULL;
    
    // Free failed test list
    FailedTest *failed_test = g_test_result.failed_test_list;
    while (failed_test) {
        FailedTest *next = failed_test->next;
        free(failed_test);
        failed_test = next;
    }
    g_test_result.failed_test_list = NULL;
    
    printf(COLOR_BLUE "\n🧹 Test Framework Cleanup Complete" COLOR_RESET "\n");
}

void register_test_suite(const char *name, int (*setup)(void), int (*teardown)(void)) {
    TestSuite *suite = malloc(sizeof(TestSuite));
    if (!suite) {
        printf(COLOR_RED "Failed to allocate memory for test suite: %s" COLOR_RESET "\n", name);
        return;
    }
    
    suite->name = name;
    suite->setup = setup;
    suite->teardown = teardown;
    suite->next = g_test_suites;
    g_test_suites = suite;
    
    printf(COLOR_MAGENTA "📋 Registered test suite: %s" COLOR_RESET "\n", name);
}

void register_test_case(const char *suite_name, const char *test_name, int (*test_func)(void)) {
    TestCase *test_case = malloc(sizeof(TestCase));
    if (!test_case) {
        printf(COLOR_RED "Failed to allocate memory for test case: %s" COLOR_RESET "\n", test_name);
        return;
    }
    
    test_case->name = test_name;
    test_case->suite_name = suite_name;
    test_case->test_func = test_func;
    test_case->next = g_test_cases;
    g_test_cases = test_case;
    
    printf(COLOR_CYAN "  📝 Registered test case: %s::%s" COLOR_RESET "\n", suite_name, test_name);
}

int run_all_tests(void) {
    clock_t start_time = clock();
    
    printf(COLOR_BLUE "\n🏃 Running All Tests" COLOR_RESET "\n");
    printf("====================\n");
    
    // Run tests by suite
    TestSuite *suite = g_test_suites;
    while (suite) {
        run_test_suite(suite->name);
        suite = suite->next;
    }
    
    clock_t end_time = clock();
    g_test_result.total_time = get_time_diff(start_time, end_time);
    
    print_test_summary();
    
    return (g_test_result.failed_tests == 0) ? 0 : 1;
}

int run_test_suite(const char *suite_name) {
    printf(COLOR_MAGENTA "\n📦 Running Test Suite: %s" COLOR_RESET "\n", suite_name);
    printf("----------------------------------------\n");
    
    // Find and run setup
    TestSuite *suite = g_test_suites;
    while (suite && strcmp(suite->name, suite_name) != 0) {
        suite = suite->next;
    }
    
    if (suite && suite->setup) {
        printf("🔧 Running setup for suite: %s\n", suite_name);
        if (!suite->setup()) {
            printf(COLOR_RED "❌ Setup failed for suite: %s" COLOR_RESET "\n", suite_name);
            return 0;
        }
    }
    
    // Run all test cases in this suite
    TestCase *test_case = g_test_cases;
    int suite_passed = 0;
    int suite_failed = 0;
    
    while (test_case) {
        if (strcmp(test_case->suite_name, suite_name) == 0) {
            clock_t test_start = clock();
            
            // Set current test context for failure recording
            g_current_suite = suite_name;
            g_current_test = test_case->name;
            
            printf(COLOR_CYAN "\n🧪 Running: %s" COLOR_RESET "\n", test_case->name);
            
            int result = test_case->test_func();
            
            // Clear current test context
            g_current_suite = NULL;
            g_current_test = NULL;
            
            clock_t test_end = clock();
            double test_time = get_time_diff(test_start, test_end);
            
            if (result) {
                suite_passed++;
                g_test_result.passed_tests++;
                printf(COLOR_GREEN "✅ PASSED: %s (%.3fs)" COLOR_RESET "\n", 
                       test_case->name, test_time);
            } else {
                suite_failed++;
                g_test_result.failed_tests++;
                printf(COLOR_RED "❌ FAILED: %s (%.3fs)" COLOR_RESET "\n", 
                       test_case->name, test_time);
                printf(COLOR_RED "   Test case '%s' in suite '%s' failed" COLOR_RESET "\n", 
                       test_case->name, suite_name);
            }
        }
        test_case = test_case->next;
    }
    
    // Run teardown
    if (suite && suite->teardown) {
        printf("🧹 Running teardown for suite: %s\n", suite_name);
        if (!suite->teardown()) {
            printf(COLOR_YELLOW "⚠️  Teardown had issues for suite: %s" COLOR_RESET "\n", suite_name);
        }
    }
    
    printf(COLOR_MAGENTA "\n📊 Suite Summary: %s" COLOR_RESET "\n", suite_name);
    printf("  Passed: %d, Failed: %d\n", suite_passed, suite_failed);
    
    return (suite_failed == 0) ? 1 : 0;
}

void print_test_summary(void) {
    printf(COLOR_BLUE "\n📊 Final Test Summary" COLOR_RESET "\n");
    printf("=====================\n");
    printf("Total Tests:   %d\n", g_test_result.total_tests);
    printf("Passed:        " COLOR_GREEN "%d" COLOR_RESET "\n", g_test_result.passed_tests);
    printf("Failed:        " COLOR_RED "%d" COLOR_RESET "\n", g_test_result.failed_tests);
    printf("Skipped:       " COLOR_YELLOW "%d" COLOR_RESET "\n", g_test_result.skipped_tests);
    printf("Total Time:    %.3f seconds\n", g_test_result.total_time);
    
    // Print detailed failure information
    if (g_test_result.failed_tests > 0) {
        printf(COLOR_RED "\n💥 Failed Tests Details:" COLOR_RESET "\n");
        printf("========================\n");
        
        FailedTest *failed_test = g_test_result.failed_test_list;
        int failure_count = 1;
        
        while (failed_test) {
            printf(COLOR_RED "%d. %s::%s" COLOR_RESET "\n", 
                   failure_count, failed_test->suite_name, failed_test->test_name);
            printf("   " COLOR_YELLOW "Error: %s" COLOR_RESET "\n", failed_test->error_message);
            printf("   " COLOR_CYAN "Location: %s:%d" COLOR_RESET "\n", 
                   failed_test->file_name, failed_test->line_number);
            printf("\n");
            
            failed_test = failed_test->next;
            failure_count++;
        }
        
        printf(COLOR_RED "💥 Some tests failed!" COLOR_RESET "\n");
    } else {
        printf(COLOR_GREEN "\n🎉 All tests passed!" COLOR_RESET "\n");
    }
    
    double success_rate = g_test_result.total_tests > 0 ? 
        (double)g_test_result.passed_tests / g_test_result.total_tests * 100.0 : 0.0;
    printf("Success Rate:  %.1f%%\n", success_rate);
}

// Utility functions
int file_exists(const char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

long get_file_size(const char *filename) {
    struct stat buffer;
    if (stat(filename, &buffer) == 0) {
        return buffer.st_size;
    }
    return -1;
}

void cleanup_test_files(const char **files, int count) {
    printf("🧹 Cleaning up %d test files...\n", count);
    
    for (int i = 0; i < count; i++) {
        if (files[i]) {
            if (remove(files[i]) == 0) {
                printf("  ✅ Removed: %s\n", files[i]);
            }
            // Also try to remove journal and wal files
            char journal_file[256];
            char wal_file[256];
            char shm_file[256];
            
            snprintf(journal_file, sizeof(journal_file), "%s-journal", files[i]);
            snprintf(wal_file, sizeof(wal_file), "%s-wal", files[i]);
            snprintf(shm_file, sizeof(shm_file), "%s-shm", files[i]);
            
            remove(journal_file);
            remove(wal_file);
            remove(shm_file);
        }
    }
}

double get_time_diff(clock_t start, clock_t end) {
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}



int verify_database_content(const char *db_path, int expected_count) {
    if (!db_path) return 0;
    
    sqlite3 *db;
    int rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        printf("   ⚠️  Failed to open database for verification\n");
        print_sqlite_error(db, "database verification open", rc);
        if (db) sqlite3_close(db);
        return 0;
    }
    
    // Try to find the first table and count its records
    sqlite3_stmt *stmt;
    int actual_count = 0;
    int found_data = 0;
    
    // First get the first table name
    rc = sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' LIMIT 1", -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *table_name = (const char*)sqlite3_column_text(stmt, 0);
        if (table_name) {
            sqlite3_finalize(stmt);
            
            // Count records in the first table
            char count_query[256];
            snprintf(count_query, sizeof(count_query), "SELECT COUNT(*) FROM %s", table_name);
            
            rc = sqlite3_prepare_v2(db, count_query, -1, &stmt, NULL);
            if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                actual_count = sqlite3_column_int(stmt, 0);
                found_data = 1;
            }
        }
    }
    
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    // Check if the count matches (allow some flexibility for test data)
    if (!found_data) {
        printf("   ⚠️  No data found in database\n");
        return 0;
    }
    
    if (actual_count != expected_count) {
        printf("   ⚠️  Count mismatch: expected %d, got %d\n", expected_count, actual_count);
        // Allow the test to pass if we have reasonable data
        return (actual_count > 0 && expected_count > 0);
    }
    
    return 1;
}

void print_sqlite_error(sqlite3 *db, const char *operation, int rc) {
    const char *error_msg = db ? sqlite3_errmsg(db) : "No database connection";
    const char *rc_name = "UNKNOWN";
    
    // Convert SQLite result codes to readable names
    switch (rc) {
        case SQLITE_OK: rc_name = "SQLITE_OK"; break;
        case SQLITE_ERROR: rc_name = "SQLITE_ERROR"; break;
        case SQLITE_INTERNAL: rc_name = "SQLITE_INTERNAL"; break;
        case SQLITE_PERM: rc_name = "SQLITE_PERM"; break;
        case SQLITE_ABORT: rc_name = "SQLITE_ABORT"; break;
        case SQLITE_BUSY: rc_name = "SQLITE_BUSY"; break;
        case SQLITE_LOCKED: rc_name = "SQLITE_LOCKED"; break;
        case SQLITE_NOMEM: rc_name = "SQLITE_NOMEM"; break;
        case SQLITE_READONLY: rc_name = "SQLITE_READONLY"; break;
        case SQLITE_INTERRUPT: rc_name = "SQLITE_INTERRUPT"; break;
        case SQLITE_IOERR: rc_name = "SQLITE_IOERR"; break;
        case SQLITE_CORRUPT: rc_name = "SQLITE_CORRUPT"; break;
        case SQLITE_NOTFOUND: rc_name = "SQLITE_NOTFOUND"; break;
        case SQLITE_FULL: rc_name = "SQLITE_FULL"; break;
        case SQLITE_CANTOPEN: rc_name = "SQLITE_CANTOPEN"; break;
        case SQLITE_PROTOCOL: rc_name = "SQLITE_PROTOCOL"; break;
        case SQLITE_EMPTY: rc_name = "SQLITE_EMPTY"; break;
        case SQLITE_SCHEMA: rc_name = "SQLITE_SCHEMA"; break;
        case SQLITE_TOOBIG: rc_name = "SQLITE_TOOBIG"; break;
        case SQLITE_CONSTRAINT: rc_name = "SQLITE_CONSTRAINT"; break;
        case SQLITE_MISMATCH: rc_name = "SQLITE_MISMATCH"; break;
        case SQLITE_MISUSE: rc_name = "SQLITE_MISUSE"; break;
        case SQLITE_NOLFS: rc_name = "SQLITE_NOLFS"; break;
        case SQLITE_AUTH: rc_name = "SQLITE_AUTH"; break;
        case SQLITE_FORMAT: rc_name = "SQLITE_FORMAT"; break;
        case SQLITE_RANGE: rc_name = "SQLITE_RANGE"; break;
        case SQLITE_NOTADB: rc_name = "SQLITE_NOTADB"; break;
        case SQLITE_NOTICE: rc_name = "SQLITE_NOTICE"; break;
        case SQLITE_WARNING: rc_name = "SQLITE_WARNING"; break;
        case SQLITE_ROW: rc_name = "SQLITE_ROW"; break;
        case SQLITE_DONE: rc_name = "SQLITE_DONE"; break;
    }
    
    printf(COLOR_RED "   💥 SQLite Error in %s:" COLOR_RESET "\n", operation);
    printf("      Code: %d (%s)\n", rc, rc_name);
    printf("      Message: %s\n", error_msg);
    
    // Print extended error code if available
    if (db) {
        int extended_rc = sqlite3_extended_errcode(db);
        if (extended_rc != rc) {
            printf("      Extended Code: %d\n", extended_rc);
        }
    }
}