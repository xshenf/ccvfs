cmake_minimum_required(VERSION 3.10)
project(sqlitecc C)

set(CMAKE_C_STANDARD 99)

# 添加编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSQLITE_ENABLE_FTS5")

# 添加调试和详细输出选项
option(ENABLE_DEBUG "Enable debug output" OFF)
option(ENABLE_VERBOSE "Enable verbose output" OFF)

# 算法选择选项
option(ENABLE_ZLIB "Enable zlib compression algorithm" ON)
option(ENABLE_OPENSSL "Enable OpenSSL encryption algorithms" ON)

if (ENABLE_DEBUG)
    add_definitions(-DDEBUG)
endif ()

if (ENABLE_VERBOSE)
    add_definitions(-DVERBOSE)
endif ()

if (ENABLE_ZLIB)
    find_package(ZLIB)
    if (ZLIB_FOUND)
        message(STATUS "Found ZLIB: ${ZLIB_VERSION}")
        add_compile_definitions(HAVE_ZLIB)
    else ()
        message(WARNING "ZLIB not found, compress algorithms will be limited")
    endif ()
endif ()


# 查找OpenSSL
if (ENABLE_OPENSSL)
    find_package(OpenSSL)
    if (OpenSSL_FOUND)
        message(STATUS "Found OpenSSL: ${OPENSSL_VERSION}")
        add_compile_definitions(HAVE_OPENSSL)
    else ()
        message(WARNING "OpenSSL not found, encryption algorithms will be limited")
    endif ()
endif ()

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
        src/db_compress_tool.c
)

# 主库
add_library(sqlitecc STATIC ${SQLITE3_SRC} ${CCVFS_SRC})
target_compile_definitions(sqlitecc PRIVATE SQLITE_ENABLE_CEROD=1)

# 根据用户选择的算法添加支持
if (ZLIB_FOUND)
    target_link_libraries(sqlitecc ZLIB::ZLIB)
    message(STATUS "Zlib compression algorithm enabled")
endif ()

if (OpenSSL_FOUND)
    target_link_libraries(sqlitecc OpenSSL::Crypto)
    message(STATUS "OpenSSL encryption algorithms enabled")
endif ()

# Enable testing support
enable_testing()

# Essential tools (keep only essential ones)

# Shell程序 - 使用SQLite原始shell加上我们的扩展
add_executable(shell sqlite3/shell.c src/shell.c)
target_link_libraries(shell sqlitecc)
target_compile_definitions(shell PRIVATE SQLITE_ENABLE_CEROD=1)

# 数据库压缩解压工具
add_executable(db_tool test/tool/db_tool.c test/tool/db_compare.c test/tool/db_generator.c)
target_link_libraries(db_tool sqlitecc)

# Add test subdirectories
add_subdirectory(test/ut)  # Unit tests
add_subdirectory(test/st)  # System tests

# Link math library for Unix platforms (except macOS)
if (UNIX AND NOT APPLE)
    target_link_libraries(shell m)
    target_link_libraries(db_tool m)
endif ()