# Unit test sources
set(UT_SOURCES
    test_framework.c
    test_basic.c
    test_ccvfs_core.c
    test_compression.c
    test_batch_writer.c
    test_integration.c
    test_ccvfs_algorithm.c
    test_ccvfs_utils.c
    test_ccvfs_page.c
    test_db_tools.c
    main.c
)

# Create unit test executable
add_executable(unit_tests ${UT_SOURCES})

# Link with the main library
target_link_libraries(unit_tests sqlitecc)

# Include directories
target_include_directories(unit_tests PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sqlite3
)

# Add compiler definitions for testing
target_compile_definitions(unit_tests PRIVATE 
    SQLITE_ENABLE_CEROD=1
    UNIT_TESTING=1
)

# Link math library on Unix systems
if(UNIX AND NOT APPLE)
    target_link_libraries(unit_tests m)
endif()

# CTest integration - Add individual test suites
add_test(NAME BasicTests 
    COMMAND unit_tests Basic
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME CoreTests 
    COMMAND unit_tests CCVFS_Core
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME CompressionTests 
    COMMAND unit_tests Compression
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME BatchWriterTests 
    COMMAND unit_tests Batch_Writer
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME IntegrationTests 
    COMMAND unit_tests Integration
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME AllUnitTests 
    COMMAND unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Set test properties with timeouts
set_tests_properties(BasicTests PROPERTIES TIMEOUT 30)
set_tests_properties(CoreTests PROPERTIES TIMEOUT 60)
set_tests_properties(CompressionTests PROPERTIES TIMEOUT 120)
set_tests_properties(BatchWriterTests PROPERTIES TIMEOUT 90)
set_tests_properties(IntegrationTests PROPERTIES TIMEOUT 180)
set_tests_properties(AllUnitTests PROPERTIES TIMEOUT 300)

# Set test labels for organization
set_tests_properties(BasicTests PROPERTIES LABELS "unit;basic")
set_tests_properties(CoreTests PROPERTIES LABELS "unit;core")
set_tests_properties(CompressionTests PROPERTIES LABELS "unit;compression")
set_tests_properties(BatchWriterTests PROPERTIES LABELS "unit;batch")
set_tests_properties(IntegrationTests PROPERTIES LABELS "unit;integration")
set_tests_properties(AllUnitTests PROPERTIES LABELS "unit;all")