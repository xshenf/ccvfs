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
        src/ccvfs.c
    src/ccvfs_core.c
    src/ccvfs_io.c  
    src/ccvfs_algorithm.c
        src/ccvfs_page.c
    src/ccvfs_utils.c
    src/ccvfs_batch_writer.c
    src/db_compress_tool.c
)

# 主库
add_library(sqlitecc STATIC ${SQLITE3_SRC} ${CCVFS_SRC})
target_link_libraries(sqlitecc ZLIB::ZLIB)
target_compile_definitions(sqlitecc PRIVATE SQLITE_ENABLE_CEROD=1)

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

add_executable(vfs_connection_test test/vfs_connection_test.c)
target_link_libraries(vfs_connection_test sqlitecc)

add_executable(large_db_stress_test test/large_db_stress_test.c)
target_link_libraries(large_db_stress_test sqlitecc)

add_executable(simple_large_test test/simple_large_test.c)
target_link_libraries(simple_large_test sqlitecc)

# Shell程序 - 使用SQLite原始shell加上我们的扩展
add_executable(shell sqlite3/shell.c src/shell.c)
target_link_libraries(shell sqlitecc)
target_compile_definitions(shell PRIVATE SQLITE_ENABLE_CEROD=1)

# 数据库压缩解压工具
add_executable(db_tool src/db_tool.c)
target_link_libraries(db_tool sqlitecc)

# 数据库生成工具
add_executable(db_generator src/db_generator.c)
target_link_libraries(db_generator sqlitecc)

# 简单数据库测试程序
add_executable(simple_db_test test/simple_db_test.c)
target_link_libraries(simple_db_test sqlitecc)

# 数据库比对工具
add_executable(db_compare src/db_compare.c)
target_link_libraries(db_compare sqlitecc)

# 空洞检测测试程序
add_executable(test_hole_detection test/test_hole_detection.c)
target_link_libraries(test_hole_detection sqlitecc)

# 简化空洞检测测试程序
add_executable(simple_hole_test test/simple_hole_test.c)
target_link_libraries(simple_hole_test sqlitecc)

# 批量写入缓冲测试程序
add_executable(batch_write_buffer_test test/batch_write_buffer_test.c)
target_link_libraries(batch_write_buffer_test sqlitecc)

# 简单缓冲测试程序
add_executable(simple_buffer_test test/simple_buffer_test.c)
target_link_libraries(simple_buffer_test sqlitecc)

# 改进的批量写入器测试程序
add_executable(improved_batch_writer_test test/improved_batch_writer_test.c)
target_link_libraries(improved_batch_writer_test sqlitecc)

# 缓冲功能验证测试程序
add_executable(buffer_functionality_test test/buffer_functionality_test.c)
target_link_libraries(buffer_functionality_test sqlitecc)

# 简化缓冲验证测试程序
add_executable(simple_buffer_verification test/simple_buffer_verification.c)
target_link_libraries(simple_buffer_verification sqlitecc)

# 链接数学库（某些平台需要）
if(UNIX AND NOT APPLE)
    target_link_libraries(shell m)
    target_link_libraries(db_tool m)
    target_link_libraries(db_generator m)
    target_link_libraries(simple_db_test m)
    target_link_libraries(db_compare m)
    target_link_libraries(test_hole_detection m)
    target_link_libraries(simple_hole_test m)
    target_link_libraries(batch_write_buffer_test m)
    target_link_libraries(improved_batch_writer_test m)
    target_link_libraries(buffer_functionality_test m)
    target_link_libraries(simple_buffer_verification m)
endif()