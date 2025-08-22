#include "unit_test_framework.h"

// Test function declarations
int test_hex_key_parsing(UnitTestResult* result);
int test_algorithm_registry(UnitTestResult* result);
int test_encryption_key_mgmt(UnitTestResult* result);

// Test case registry
static const UnitTestCase test_cases[] = {
    {
        "hex_key_parsing",
        "Test hexadecimal key parsing functionality",
        test_hex_key_parsing
    },
    {
        "algorithm_registry", 
        "Test compression and encryption algorithm registry",
        test_algorithm_registry
    },
    {
        "encryption_key_mgmt",
        "Test encryption key management functions", 
        test_encryption_key_mgmt
    }
};

static const int num_test_cases = sizeof(test_cases) / sizeof(test_cases[0]);

void print_usage(const char* program_name) {
    printf("Usage: %s [test_name | --all | --list | --help]\n", program_name);
    printf("\nAvailable tests:\n");
    for (int i = 0; i < num_test_cases; i++) {
        printf("  %-20s %s\n", test_cases[i].name, test_cases[i].description);
    }
    printf("\n");
}

const UnitTestCase* find_test_case(const char* name) {
    for (int i = 0; i < num_test_cases; i++) {
        if (strcmp(test_cases[i].name, name) == 0) {
            return &test_cases[i];
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    int exit_code = 0;
    
    unit_test_init();
    ccvfs_init_builtin_algorithms();
    
    if (argc < 2) {
        print_usage(argv[0]);
        exit_code = 1;
        goto cleanup;
    }
    
    const char* command = argv[1];
    
    if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
        goto cleanup;
    }
    
    if (strcmp(command, "--list") == 0 || strcmp(command, "-l") == 0) {
        printf("Available unit tests:\n");
        for (int i = 0; i < num_test_cases; i++) {
            printf("  %-20s %s\n", test_cases[i].name, test_cases[i].description);
        }
        goto cleanup;
    }
    
    if (strcmp(command, "--all") == 0 || strcmp(command, "-a") == 0) {
        printf("Running all unit tests...\n");
        if (!run_all_tests(test_cases, num_test_cases)) {
            exit_code = 1;
        }
        goto cleanup;
    }
    
    const UnitTestCase* test_case = find_test_case(command);
    if (!test_case) {
        printf("Unknown test case: %s\n", command);
        printf("\nUse --list to see available tests.\n");
        exit_code = 1;
        goto cleanup;
    }
    
    printf("Running single test: %s\n", test_case->name);
    if (!run_single_test(test_case)) {
        exit_code = 1;
    }
    
cleanup:
    print_test_summary();
    unit_test_cleanup();
    
    printf("\nUnit tests completed with exit code: %d\n", exit_code);
    return exit_code;
}