# System Tests CMakeLists.txt
# This file configures the unified system test suite

cmake_minimum_required(VERSION 3.10)

# Enable testing
enable_testing()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../sqlite3)

# Build the unified system test executable
add_executable(system_tests 
    system_test_main.c
    system_test_common.c
    test_basic.c
    test_storage.c
    test_buffer.c
    test_tools.c
)

# Link with the main sqlitecc library
target_link_libraries(system_tests sqlitecc)

# Add individual test cases for ctest
# Each test case can be run independently

# VFS Connection Test
add_test(
    NAME SystemTest_VFS_Connection
    COMMAND system_tests vfs_connection
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Simple Database Test
add_test(
    NAME SystemTest_Simple_DB
    COMMAND system_tests simple_db
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Large Database Stress Test
add_test(
    NAME SystemTest_Large_DB_Stress
    COMMAND system_tests large_db_stress
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Simple Large Test
add_test(
    NAME SystemTest_Simple_Large
    COMMAND system_tests simple_large
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Large Database Compression Integrity Test
add_test(
    NAME SystemTest_Large_DB_Compression_Integrity
    COMMAND system_tests large_db_compression_integrity
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Hole Detection Test
add_test(
    NAME SystemTest_Hole_Detection
    COMMAND system_tests hole_detection
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Simple Hole Test
add_test(
    NAME SystemTest_Simple_Hole
    COMMAND system_tests simple_hole
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Batch Write Buffer Test
add_test(
    NAME SystemTest_Batch_Write_Buffer
    COMMAND system_tests batch_write_buffer
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Simple Buffer Test
add_test(
    NAME SystemTest_Simple_Buffer
    COMMAND system_tests simple_buffer
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Database Tools Test
add_test(
    NAME SystemTest_DB_Tools
    COMMAND system_tests db_tools
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Add a test for running all system tests
add_test(
    NAME SystemTest_All
    COMMAND system_tests --all
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Set test properties
set_tests_properties(
    SystemTest_VFS_Connection
    SystemTest_Simple_DB
    SystemTest_Large_DB_Stress
    SystemTest_Simple_Large
    SystemTest_Large_DB_Compression_Integrity
    SystemTest_Hole_Detection
    SystemTest_Simple_Hole
    SystemTest_Batch_Write_Buffer
    SystemTest_Simple_Buffer
    SystemTest_DB_Tools
    PROPERTIES
    TIMEOUT 300  # 5 minutes timeout for each test
)

# Set longer timeout for stress test and all tests
set_tests_properties(
    SystemTest_Large_DB_Stress
    SystemTest_Large_DB_Compression_Integrity
    SystemTest_All
    PROPERTIES
    TIMEOUT 600  # 10 minutes timeout
)

# Create test categories using labels
set_tests_properties(
    SystemTest_VFS_Connection
    SystemTest_Simple_DB
    PROPERTIES
    LABELS "Basic"
)

set_tests_properties(
    SystemTest_Large_DB_Stress
    SystemTest_Simple_Large
    SystemTest_Large_DB_Compression_Integrity
    PROPERTIES
    LABELS "Performance"
)

set_tests_properties(
    SystemTest_Hole_Detection
    SystemTest_Simple_Hole
    PROPERTIES
    LABELS "Storage"
)

set_tests_properties(
    SystemTest_Batch_Write_Buffer
    SystemTest_Simple_Buffer
    PROPERTIES
    LABELS "Buffer"
)

set_tests_properties(
    SystemTest_DB_Tools
    PROPERTIES
    LABELS "Tools"
)

set_tests_properties(
    SystemTest_All
    PROPERTIES
    LABELS "Integration"
)