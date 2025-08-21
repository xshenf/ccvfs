# System Test Suite

This directory contains the unified system test framework for CCVFS.

## Overview

The system test suite replaces the previously scattered individual test programs with a single, unified test runner that can be controlled via command line arguments and integrated with ctest.

## Files

- `system_test_main.c` - Main test runner with command line interface
- `system_test_impl.c` - Test implementations
- `CMakeLists.txt` - Build configuration for ctest integration

## Usage

### Direct Test Runner

```bash
# Show help
./system_tests --help

# List all available tests
./system_tests --list

# Run all tests
./system_tests --all

# Run a specific test
./system_tests vfs_connection

# Run with verbose output
./system_tests --all --verbose
```

### CTest Integration

```bash
# Run all system tests
ctest -R SystemTest

# Run by category
ctest -L Basic          # Basic functionality tests
ctest -L Performance    # Performance and stress tests
ctest -L Storage        # Hole detection and storage tests
ctest -L Buffer         # Buffer management tests
ctest -L Tools          # Database tools tests

# Run specific tests
ctest -R SystemTest_VFS_Connection
ctest -R SystemTest_Simple_DB

# Run with verbose output
ctest -R SystemTest -V
```

## Test Categories

- **Basic**: Essential VFS and database functionality
- **Performance**: Stress testing and large database operations
- **Storage**: Space hole detection and management
- **Buffer**: Write buffer functionality
- **Tools**: Database tools integration
- **Integration**: Comprehensive system test

## Benefits

1. **Unified Interface**: Single entry point for all system tests
2. **CTest Integration**: Works seamlessly with CMake's testing framework
3. **Categorization**: Tests are organized by functionality
4. **Parallel Execution**: Tests can be run in parallel via ctest
5. **Consistent Output**: Standardized test result reporting
6. **Easy Maintenance**: New tests can be easily added to the framework

## Adding New Tests

1. Add the test function declaration to `system_test_main.c`
2. Implement the test function in `system_test_impl.c`
3. Add the test to the `test_cases` array in `system_test_main.c`
4. Add a corresponding ctest entry in `CMakeLists.txt`
5. Assign appropriate category labels