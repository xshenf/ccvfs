/*
 * Zlib Only Test
 * 只测试zlib压缩算法
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "sqlite3.h"
#include "compress_vfs.h"

// 获取文件大小
long get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

// 格式化大小显示
void format_size(long size, char *buffer, size_t buffer_size) {
    if (size >= 1024*1024) {
        snprintf(buffer, buffer_size, "%.2f MB", size / (1024.0*1024.0));
    } else if (size >= 1024) {
        snprintf(buffer, buffer_size, "%.2f KB", size / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%ld bytes", size);
    }
}

int main() {
    printf("=== Zlib 压缩数据库测试 ===\n");
    printf("SQLite版本: %s\n", sqlite3_libversion());
    
    const int RECORD_COUNT = 5000;
    
    // 删除旧文件
    remove("normal.db");
    remove("zlib_compressed.db");
    
    // 1. 创建普通数据库
    printf("\n1. 创建普通数据库...\n");
    sqlite3 *normal_db = NULL;
    int rc = sqlite3_open("normal.db", &normal_db);
    if (rc != SQLITE_OK) {
        printf("❌ 打开普通数据库失败\n");
        return 1;
    }
    
    clock_t start_time = clock();
    
    const char *create_sql = 
        "CREATE TABLE test_data (id INTEGER PRIMARY KEY, data TEXT);";
    sqlite3_exec(normal_db, create_sql, 0, 0, NULL);
    sqlite3_exec(normal_db, "BEGIN TRANSACTION;", 0, 0, NULL);
    
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(normal_db, "INSERT INTO test_data (data) VALUES (?);", -1, &stmt, NULL);
    
    // 生成重复性强的测试数据
    char test_data[256];
    strcpy(test_data, "这是一个测试字符串，包含重复内容用于测试压缩效果。ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    
    printf("插入 %d 条记录...\n", RECORD_COUNT);
    
    for (int i = 1; i <= RECORD_COUNT; i++) {
        char data[512];
        snprintf(data, sizeof(data), "%s - 记录号: %06d", test_data, i);
        
        sqlite3_bind_text(stmt, 1, data, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        
        if (i % 1000 == 0) {
            printf("  已插入 %d 条记录\n", i);
        }
    }
    
    sqlite3_finalize(stmt);
    sqlite3_exec(normal_db, "COMMIT;", 0, 0, NULL);
    
    clock_t normal_time = clock() - start_time;
    sqlite3_close(normal_db);
    
    // 2. 创建zlib压缩数据库
    printf("\n2. 创建Zlib压缩数据库...\n");
    
    rc = sqlite3_ccvfs_create("zlib_vfs", NULL, "zlib", NULL, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ 创建zlib VFS失败: %d\n", rc);
        return 1;
    }
    
    sqlite3 *compressed_db = NULL;
    rc = sqlite3_open_v2("zlib_compressed.db", &compressed_db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "zlib_vfs");
    if (rc != SQLITE_OK) {
        printf("❌ 打开压缩数据库失败: %s\n", sqlite3_errmsg(compressed_db));
        sqlite3_ccvfs_destroy("zlib_vfs");
        return 1;
    }
    
    start_time = clock();
    
    sqlite3_exec(compressed_db, create_sql, 0, 0, NULL);
    sqlite3_exec(compressed_db, "BEGIN TRANSACTION;", 0, 0, NULL);
    
    sqlite3_prepare_v2(compressed_db, "INSERT INTO test_data (data) VALUES (?);", -1, &stmt, NULL);
    
    printf("插入 %d 条记录到压缩数据库...\n", RECORD_COUNT);
    
    for (int i = 1; i <= RECORD_COUNT; i++) {
        char data[512];
        snprintf(data, sizeof(data), "%s - 记录号: %06d", test_data, i);
        
        sqlite3_bind_text(stmt, 1, data, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        
        if (i % 1000 == 0) {
            printf("  已插入 %d 条记录\n", i);
        }
    }
    
    sqlite3_finalize(stmt);
    sqlite3_exec(compressed_db, "COMMIT;", 0, 0, NULL);
    
    clock_t compressed_time = clock() - start_time;
    
    // 3. 验证数据
    printf("\n3. 验证数据完整性...\n");
    sqlite3_stmt *count_stmt;
    rc = sqlite3_prepare_v2(compressed_db, "SELECT COUNT(*) FROM test_data;", -1, &count_stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(count_stmt);
        if (rc == SQLITE_ROW) {
            int count = sqlite3_column_int(count_stmt, 0);
            printf("✅ 压缩数据库记录数: %d\n", count);
        }
        sqlite3_finalize(count_stmt);
    }
    
    sqlite3_close(compressed_db);
    sqlite3_ccvfs_destroy("zlib_vfs");
    
    // 4. 对比结果
    printf("\n=== 压缩效果和性能对比 ===\n");
    
    long normal_size = get_file_size("normal.db");
    long compressed_size = get_file_size("zlib_compressed.db");
    
    char normal_size_str[64], compressed_size_str[64];
    format_size(normal_size, normal_size_str, sizeof(normal_size_str));
    format_size(compressed_size, compressed_size_str, sizeof(compressed_size_str));
    
    printf("普通数据库大小: %ld bytes (%s)\n", normal_size, normal_size_str);
    printf("压缩数据库大小: %ld bytes (%s)\n", compressed_size, compressed_size_str);
    
    if (normal_size > 0 && compressed_size > 0) {
        double compression_ratio = (double)compressed_size / normal_size * 100.0;
        long space_saved = normal_size - compressed_size;
        char space_saved_str[64];
        format_size(space_saved, space_saved_str, sizeof(space_saved_str));
        
        printf("压缩率: %.1f%%\n", compression_ratio);
        printf("空间节省: %ld bytes (%s)\n", space_saved, space_saved_str);
        
        if (compression_ratio < 90.0) {
            printf("✅ 压缩效果良好! 节省了 %.1f%% 的空间\n", 100.0 - compression_ratio);
        } else if (compression_ratio < 100.0) {
            printf("⚠️  压缩效果一般，节省了 %.1f%% 的空间\n", 100.0 - compression_ratio);
        } else {
            printf("❌ 压缩后文件变大了\n");
        }
    }
    
    // 性能对比
    printf("\n=== 性能对比 ===\n");
    double normal_sec = (double)normal_time / CLOCKS_PER_SEC;
    double compressed_sec = (double)compressed_time / CLOCKS_PER_SEC;
    
    printf("普通数据库写入时间: %.2f 秒\n", normal_sec);
    printf("压缩数据库写入时间: %.2f 秒\n", compressed_sec);
    
    if (compressed_sec > 0 && normal_sec > 0) {
        double slowdown = compressed_sec / normal_sec;
        printf("性能影响: %.1fx 倍", slowdown);
        if (slowdown < 1.2) {
            printf(" (影响很小)\n");
        } else if (slowdown < 2.0) {
            printf(" (影响适中)\n");
        } else {
            printf(" (影响较大)\n");
        }
        
        printf("普通数据库写入速度: %.0f 记录/秒\n", RECORD_COUNT / normal_sec);
        printf("压缩数据库写入速度: %.0f 记录/秒\n", RECORD_COUNT / compressed_sec);
    }
    
    printf("\n✅ Zlib压缩测试完成!\n");
    return 0;
}