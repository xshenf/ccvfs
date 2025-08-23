# Unit Tests CMakeLists.txt
# This file configures the unit test suite

cmake_minimum_required(VERSION 3.10)

# Enable testing
enable_testing()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../sqlite3)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# Build the unified unit test executable
add_executable(unit_tests 
    unit_test_main.c
    unit_test_framework.c
    test_hex_key_parsing.c
    test_encryption_key_mgmt.c
)

# Link with the main sqlitecc library
target_link_libraries(unit_tests sqlitecc)

# Add individual unit test cases for ctest
# Each test case can be run independently

# Hex Key Parsing Test
add_test(
    NAME UnitTest_Hex_Key_Parsing
    COMMAND unit_tests hex_key_parsing
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Encryption Key Management Test
add_test(
    NAME UnitTest_Encryption_Key_Mgmt
    COMMAND unit_tests encryption_key_mgmt
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Add a test for running all unit tests
add_test(
    NAME UnitTest_All
    COMMAND unit_tests --all
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Set test properties
set_tests_properties(
    UnitTest_Hex_Key_Parsing
    UnitTest_Encryption_Key_Mgmt
    PROPERTIES
    TIMEOUT 60  # 1 minute timeout for each unit test
)

# Set timeout for all tests
set_tests_properties(
    UnitTest_All
    PROPERTIES
    TIMEOUT 180  # 3 minutes timeout for all unit tests
)

# Create test categories using labels
set_tests_properties(
    UnitTest_Hex_Key_Parsing
    UnitTest_Encryption_Key_Mgmt
    PROPERTIES
    LABELS "Security"
)

set_tests_properties(
    UnitTest_All
    PROPERTIES
    LABELS "UnitTest"
)

# Add memory leak detection on supported platforms
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(UNIX AND NOT APPLE)
        # Use Valgrind for memory leak detection on Linux
        find_program(VALGRIND_PROGRAM valgrind)
        if(VALGRIND_PROGRAM)
            add_test(
                NAME UnitTest_Memory_Leak_Check
                COMMAND ${VALGRIND_PROGRAM} --leak-check=full --error-exitcode=1 $<TARGET_FILE:unit_tests> --all
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            )
            set_tests_properties(UnitTest_Memory_Leak_Check PROPERTIES LABELS "MemoryCheck")
        endif()
    endif()
endif()