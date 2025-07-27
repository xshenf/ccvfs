cmake_minimum_required(VERSION 3.10)
project(compressvfs C)

set(CMAKE_C_STANDARD 99)

# 设置包含目录
include_directories(include)
include_directories(sqlite3)

# 查找SQLite3库
find_package(SQLite3)

# 添加编译选项
option(ENABLE_DEBUG "Enable debug mode" OFF)
option(ENABLE_VERBOSE "Enable verbose mode" OFF)

if(ENABLE_DEBUG)
    add_compile_definitions(DEBUG)
endif()

if(ENABLE_VERBOSE)
    add_compile_definitions(VERBOSE)
endif()

# 创建共享库
add_library(compressvfs SHARED
    src/compress_vfs.c
)

# 链接SQLite3库
if(SQLite3_FOUND)
    target_link_libraries(compressvfs SQLite::SQLite3)
else()
    # 如果找不到系统SQLite3库，则使用内置的
    target_sources(compressvfs PRIVATE sqlite3/sqlite3.c)
endif()

# 创建测试可执行文件
add_executable(test_compressvfs
    test/main.c
)

# 创建shell可执行文件，包含SQLite源码和我们的VFS代码
add_executable(sqlite3cc
    src/shell.c
    sqlite3/shell.c
    sqlite3/sqlite3.c
)

# 定义shell入口点
target_compile_definitions(sqlite3cc PRIVATE SQLITE_ENABLE_CEROD=1)

# 链接库和包含目录
target_link_libraries(test_compressvfs compressvfs)

# 在Windows上可能需要链接额外的库
if(WIN32)
    target_link_libraries(sqlite3cc)
else()
    if(SQLite3_FOUND)
        target_link_libraries(sqlite3cc SQLite::SQLite3)
    else()
        target_link_libraries(sqlite3cc)
    endif()
endif()

# 设置输出目录并添加编译选项
set_target_properties(compressvfs PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# 添加编译定义以启用新API
target_compile_definitions(compressvfs PRIVATE -DNEW_API_ENABLED)

set_target_properties(test_compressvfs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(sqlite3cc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)