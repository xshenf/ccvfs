cmake_minimum_required(VERSION 3.10)
project(sqlitecc C)

set(CMAKE_C_STANDARD 99)

# 添加编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSQLITE_ENABLE_FTS5")

# 添加调试和详细输出选项
option(ENABLE_DEBUG "Enable debug output" OFF)
option(ENABLE_VERBOSE "Enable verbose output" OFF)

if(ENABLE_DEBUG)
    add_definitions(-DDEBUG)
endif()

if(ENABLE_VERBOSE)
    add_definitions(-DVERBOSE)
endif()

find_package(ZLIB REQUIRED)

# 尝试查找其他压缩库
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LZ4 QUIET liblz4)
    pkg_check_modules(LZMA QUIET liblzma)
    pkg_check_modules(BROTLI QUIET libbrotlidec libbrotlienc)
endif()

# 手动查找LZ4库
find_path(LZ4_INCLUDE_DIR lz4.h)
find_library(LZ4_LIBRARY lz4)
if(LZ4_INCLUDE_DIR AND LZ4_LIBRARY)
    set(LZ4_FOUND TRUE)
    message(STATUS "Found LZ4: ${LZ4_LIBRARY}")
endif()

# 手动查找LZMA库
find_path(LZMA_INCLUDE_DIR lzma.h)
find_library(LZMA_LIBRARY lzma)
if(LZMA_INCLUDE_DIR AND LZMA_LIBRARY)
    set(LZMA_FOUND TRUE)
    message(STATUS "Found LZMA: ${LZMA_LIBRARY}")
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
target_link_libraries(sqlitecc ZLIB::ZLIB)

# 根据找到的库添加支持
if(LZ4_FOUND)
    target_include_directories(sqlitecc PRIVATE ${LZ4_INCLUDE_DIR})
    target_link_libraries(sqlitecc ${LZ4_LIBRARY})
    target_compile_definitions(sqlitecc PRIVATE HAVE_LZ4)
    message(STATUS "LZ4 support enabled")
endif()

if(LZMA_FOUND)
    target_include_directories(sqlitecc PRIVATE ${LZMA_INCLUDE_DIR})
    target_link_libraries(sqlitecc ${LZMA_LIBRARY})
    target_compile_definitions(sqlitecc PRIVATE HAVE_LZMA)
    message(STATUS "LZMA support enabled")
endif()

# Essential test programs
add_executable(core_algorithm_test test/core_algorithm_test.c)
target_link_libraries(core_algorithm_test sqlitecc)

add_executable(vfs_connection_test test/vfs_connection_test.c)
target_link_libraries(vfs_connection_test sqlitecc)

add_executable(compression_ratio_test test/compression_ratio_test.c)
target_link_libraries(compression_ratio_test sqlitecc)

# Shell程序
add_executable(shell src/shell.c)
target_link_libraries(shell sqlitecc)

# 链接数学库（某些平台需要）
if(UNIX AND NOT APPLE)
    target_link_libraries(test_main m)
    target_link_libraries(large_db_test m)
    target_link_libraries(large_db_test_utf8 m)
    target_link_libraries(large_db_zlib_test m)
    target_link_libraries(shell m)
endif()