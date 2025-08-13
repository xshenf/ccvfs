#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External test registration functions
extern void register_basic_tests(void);
extern void register_ccvfs_core_tests(void);
extern void register_compression_tests(void);
extern void register_batch_writer_tests(void);
extern void register_integration_tests(void);
extern void register_algorithm_tests(void);
extern void register_utils_tests(void);
extern void register_page_tests(void);
extern void register_db_tools_tests(void);

// Function to print usage information
void print_usage(const char *program_name) {
    printf("Usage: %s [options] [test_suite]\n", program_name);
    printf("\nOptions:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -l, --list     List all available test suites\n");
    printf("  -v, --verbose  Enable verbose output\n");
    printf("\nTest Suites:\n");
    printf("  CCVFS_Core     Core VFS functionality tests\n");
    printf("  Compression    Compression and decompression tests\n");
    printf("  Batch_Writer   Batch writer functionality tests\n");
    printf("  Integration    Integration and end-to-end tests\n");
    printf("  all            Run all test suites (default)\n");
    printf("\nExamples:\n");
    printf("  %s                    # Run all tests\n", program_name);
    printf("  %s CCVFS_Core         # Run only core tests\n", program_name);
    printf("  %s Compression        # Run only compression tests\n", program_name);
    printf("  %s -l                 # List available test suites\n", program_name);
}

// Function to list available test suites
void list_test_suites(void) {
    printf(COLOR_BLUE "Available Test Suites:" COLOR_RESET "\n");
    printf("======================\n");
    printf("1. " COLOR_CYAN "CCVFS_Core" COLOR_RESET "     - Core VFS functionality\n");
    printf("   - VFS creation and destruction\n");
    printf("   - Parameter validation\n");
    printf("   - Basic database operations\n");
    printf("   - Error handling\n");
    printf("   - Multiple VFS instances\n");
    printf("\n");
    
    printf("2. " COLOR_CYAN "Compression" COLOR_RESET "    - Compression and decompression\n");
    printf("   - Basic compression/decompression\n");
    printf("   - Different algorithms (zlib, lz4, rle)\n");
    printf("   - Various page sizes\n");
    printf("   - Compression levels\n");
    printf("   - Statistics and error handling\n");
    printf("\n");
    
    printf("3. " COLOR_CYAN "Batch_Writer" COLOR_RESET "   - Batch writer functionality\n");
    printf("   - Configuration and basic operations\n");
    printf("   - Statistics and flush operations\n");
    printf("   - Different configurations\n");
    printf("   - Performance characteristics\n");
    printf("   - Error handling\n");
    printf("\n");
    
    printf("4. " COLOR_CYAN "Integration" COLOR_RESET "    - End-to-end integration tests\n");
    printf("   - Complete workflow testing\n");
    printf("   - Real-time compression with batch writer\n");
    printf("   - Mixed read/write operations\n");
    printf("   - Error recovery and consistency\n");
    printf("   - Performance under load\n");
    printf("\n");
    
    printf("Use '" COLOR_GREEN "%s <suite_name>" COLOR_RESET "' to run a specific suite.\n", "test_runner");
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    int verbose = 0;
    const char *target_suite = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_test_suites();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (argv[i][0] != '-') {
            target_suite = argv[i];
        } else {
            printf(COLOR_RED "Unknown option: %s" COLOR_RESET "\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Initialize test framework
    init_test_framework();
    
    // Register all test suites
    printf(COLOR_BLUE "🔧 Registering Test Suites..." COLOR_RESET "\n");
    register_basic_tests();
    register_ccvfs_core_tests();
    register_compression_tests();
    register_batch_writer_tests();
    register_integration_tests();
    register_algorithm_tests();
    register_utils_tests();
    register_page_tests();
    register_db_tools_tests();
    
    printf(COLOR_GREEN "✅ All test suites registered" COLOR_RESET "\n");
    
    // Run tests
    int result = 0;
    
    if (target_suite == NULL || strcmp(target_suite, "all") == 0) {
        // Run all tests
        printf(COLOR_BLUE "\n🚀 Running All Test Suites" COLOR_RESET "\n");
        printf("===========================\n");
        result = run_all_tests();
    } else {
        // Run specific test suite
        printf(COLOR_BLUE "\n🎯 Running Test Suite: %s" COLOR_RESET "\n", target_suite);
        printf("================================\n");
        
        // Validate suite name
        const char *valid_suites[] = {"Basic", "CCVFS_Core", "Compression", "Batch_Writer", "Integration", "Algorithm", "Utils", "Page", "DB_Tools"};
        int valid = 0;
        
        for (int i = 0; i < 9; i++) {
            if (strcmp(target_suite, valid_suites[i]) == 0) {
                valid = 1;
                break;
            }
        }
        
        if (!valid) {
            printf(COLOR_RED "❌ Invalid test suite: %s" COLOR_RESET "\n", target_suite);
            printf("Use -l to list available test suites.\n");
            cleanup_test_framework();
            return 1;
        }
        
        int suite_result = run_test_suite(target_suite);
        result = suite_result ? 0 : 1;
        
        // Print individual suite summary
        print_test_summary();
    }
    
    // Cleanup
    cleanup_test_framework();
    
    // Print final result
    if (result == 0) {
        printf(COLOR_GREEN "\n🎉 All tests completed successfully!" COLOR_RESET "\n");
    } else {
        printf(COLOR_RED "\n💥 Some tests failed!" COLOR_RESET "\n");
    }
    
    return result;
}