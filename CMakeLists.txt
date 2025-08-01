cmake_minimum_required(VERSION 3.10)
project(sqlitecc C)

set(CMAKE_C_STANDARD 99)

# 添加编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSQLITE_ENABLE_FTS5")

# 添加调试和详细输出选项
option(ENABLE_DEBUG "Enable debug output" OFF)
option(ENABLE_VERBOSE "Enable verbose output" OFF)

add_definitions(-DDEBUG)

if(ENABLE_VERBOSE)
    add_definitions(-DVERBOSE)
endif()

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/sqlite3)

# SQLite3源文件
set(SQLITE3_SRC 
    sqlite3/sqlite3.c
)

# 压缩VFS源文件
set(CCVFS_SRC 
    src/compress_vfs.c
    src/ccvfs_core.c
    src/ccvfs_io.c  
    src/ccvfs_algorithm.c
    src/ccvfs_block.c
    src/ccvfs_utils.c
)

# 主库
add_library(sqlitecc STATIC ${SQLITE3_SRC} ${CCVFS_SRC})

# 测试程序
add_executable(test_main test/main.c)
target_link_libraries(test_main sqlitecc)

# 大型数据库测试程序
add_executable(large_db_test test/large_db_test.c)
target_link_libraries(large_db_test sqlitecc)

# 大型数据库测试程序 (UTF-8版本)
add_executable(large_db_test_utf8 test/large_db_test_utf8.c)
target_link_libraries(large_db_test_utf8 sqlitecc)

# 大型数据库测试程序 (使用zlib)
add_executable(large_db_zlib_test test/large_db_zlib_test.c)
target_link_libraries(large_db_zlib_test sqlitecc)

# Shell程序
add_executable(shell src/shell.c)
target_link_libraries(shell sqlitecc)

# 算法测试程序
add_executable(algorithm_test test/algorithm_test.c)
target_link_libraries(algorithm_test sqlitecc)

# 链接数学库（某些平台需要）
if(UNIX AND NOT APPLE)
    target_link_libraries(test_main m)
    target_link_libraries(large_db_test m)
    target_link_libraries(large_db_test_utf8 m)
    target_link_libraries(large_db_zlib_test m)
    target_link_libraries(shell m)
    target_link_libraries(algorithm_test m)
endif()