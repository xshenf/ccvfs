/*
 * Simple Zlib Database Test
 * 简单的zlib数据库压缩测试
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

int test_compression(const char *algorithm, int record_count) {
    sqlite3 *db = NULL;
    char db_name[64];
    char vfs_name[64];
    
    snprintf(db_name, sizeof(db_name), "test_%s.db", algorithm);
    snprintf(vfs_name, sizeof(vfs_name), "%s_vfs", algorithm);
    
    // 删除旧文件
    remove(db_name);
    
    printf("\n=== 测试 %s 压缩算法 ===\n", algorithm);
    
    // 创建VFS
    int rc = sqlite3_ccvfs_create(vfs_name, NULL, algorithm, NULL, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ 创建 %s VFS 失败: %d\n", algorithm, rc);
        return rc;
    }
    
    // 打开数据库
    rc = sqlite3_open_v2(db_name, &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs_name);
    if (rc != SQLITE_OK) {
        printf("❌ 打开数据库失败: %s\n", sqlite3_errmsg(db));
        sqlite3_ccvfs_destroy(vfs_name);
        return rc;
    }
    
    clock_t start_time = clock();
    
    // 创建表
    const char *create_sql = 
        "CREATE TABLE test_data ("
        "id INTEGER PRIMARY KEY, "
        "data TEXT"
        ");";
    
    char *err_msg = NULL;
    rc = sqlite3_exec(db, create_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ 创建表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return rc;
    }
    
    // 开始事务
    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ 开始事务失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return rc;
    }
    
    // 插入数据
    sqlite3_stmt *stmt;
    const char *insert_sql = "INSERT INTO test_data (data) VALUES (?);";
    
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ 准备语句失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return rc;
    }
    
    printf("插入 %d 条记录...\n", record_count);
    
    // 生成重复性强的测试数据
    char test_data[512];
    strcpy(test_data, "这是一个测试字符串，包含重复内容用于测试压缩效果。ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    strcat(test_data, test_data);  // 重复一次增加压缩潜力
    
    for (int i = 1; i <= record_count; i++) {
        char data[1024];
        snprintf(data, sizeof(data), "%s - 记录号: %06d", test_data, i);
        
        sqlite3_bind_text(stmt, 1, data, -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            printf("❌ 插入记录 %d 失败: %s\n", i, sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            sqlite3_ccvfs_destroy(vfs_name);
            return rc;
        }
        
        sqlite3_reset(stmt);
        
        if (i % 2000 == 0) {
            printf("  已插入 %d 条记录\n", i);
        }
    }
    
    sqlite3_finalize(stmt);
    
    // 提交事务
    rc = sqlite3_exec(db, "COMMIT;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("❌ 提交事务失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        sqlite3_ccvfs_destroy(vfs_name);
        return rc;
    }
    
    clock_t end_time = clock();
    double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    // 验证数据
    sqlite3_stmt *count_stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test_data;", -1, &count_stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(count_stmt);
        if (rc == SQLITE_ROW) {
            int count = sqlite3_column_int(count_stmt, 0);
            printf("✅ 数据验证: %d 条记录\n", count);
        }
        sqlite3_finalize(count_stmt);
    }
    
    sqlite3_close(db);
    sqlite3_ccvfs_destroy(vfs_name);
    
    // 检查文件大小
    long file_size = get_file_size(db_name);
    char size_str[64];
    format_size(file_size, size_str, sizeof(size_str));
    
    printf("文件大小: %ld bytes (%s)\n", file_size, size_str);
    printf("写入时间: %.2f 秒\n", elapsed);
    printf("写入速度: %.0f 记录/秒\n", record_count / elapsed);
    
    return SQLITE_OK;
}

int main() {
    printf("=== SQLite VFS 压缩算法性能测试 ===\n");
    printf("SQLite版本: %s\n", sqlite3_libversion());
    
    const int RECORD_COUNT = 10000;
    
    // 测试所有压缩算法
    test_compression("rle", RECORD_COUNT);
    test_compression("lz4", RECORD_COUNT);  
    test_compression("zlib", RECORD_COUNT);
    
    // 创建普通数据库进行对比
    printf("\n=== 普通数据库对比 ===\n");
    
    sqlite3 *normal_db = NULL;
    remove("normal.db");
    
    int rc = sqlite3_open("normal.db", &normal_db);
    if (rc == SQLITE_OK) {
        clock_t start_time = clock();
        
        const char *create_sql = 
            "CREATE TABLE test_data (id INTEGER PRIMARY KEY, data TEXT);";
        sqlite3_exec(normal_db, create_sql, 0, 0, NULL);
        sqlite3_exec(normal_db, "BEGIN TRANSACTION;", 0, 0, NULL);
        
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(normal_db, "INSERT INTO test_data (data) VALUES (?);", -1, &stmt, NULL);
        
        char test_data[512];
        strcpy(test_data, "这是一个测试字符串，包含重复内容用于测试压缩效果。ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        strcat(test_data, test_data);
        
        printf("插入 %d 条记录...\n", RECORD_COUNT);
        
        for (int i = 1; i <= RECORD_COUNT; i++) {
            char data[1024];
            snprintf(data, sizeof(data), "%s - 记录号: %06d", test_data, i);
            
            sqlite3_bind_text(stmt, 1, data, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        
        sqlite3_finalize(stmt);
        sqlite3_exec(normal_db, "COMMIT;", 0, 0, NULL);
        
        clock_t end_time = clock();
        double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        
        sqlite3_close(normal_db);
        
        long file_size = get_file_size("normal.db");
        char size_str[64];
        format_size(file_size, size_str, sizeof(size_str));
        
        printf("文件大小: %ld bytes (%s)\n", file_size, size_str);
        printf("写入时间: %.2f 秒\n", elapsed);
        printf("写入速度: %.0f 记录/秒\n", RECORD_COUNT / elapsed);
        
        // 计算压缩比较
        printf("\n=== 压缩效果对比 ===\n");
        
        const char* algorithms[] = {"rle", "lz4", "zlib"};
        for (int i = 0; i < 3; i++) {
            char db_name[64];
            snprintf(db_name, sizeof(db_name), "test_%s.db", algorithms[i]);
            
            long compressed_size = get_file_size(db_name);
            if (compressed_size > 0) {
                double ratio = (double)compressed_size / file_size * 100.0;
                long saved = file_size - compressed_size;
                char saved_str[64];
                format_size(saved, saved_str, sizeof(saved_str));
                
                printf("%s: %.1f%% (节省 %s)\n", algorithms[i], ratio, saved_str);
            }
        }
    }
    
    printf("\n✅ 测试完成!\n");
    return 0;
}