/*
 * Real Zlib Compression Database Test Program
 * 测试使用真正zlib库的数据库压缩功能
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "sqlite3.h"
#include "compress_vfs.h"

// 获取文件大小的辅助函数
long get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

// 格式化文件大小显示
void format_size(long size, char *buffer, size_t buffer_size) {
    if (size >= 1024*1024*1024) {
        snprintf(buffer, buffer_size, "%.2f GB", size / (1024.0*1024.0*1024.0));
    } else if (size >= 1024*1024) {
        snprintf(buffer, buffer_size, "%.2f MB", size / (1024.0*1024.0));
    } else if (size >= 1024) {
        snprintf(buffer, buffer_size, "%.2f KB", size / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%ld bytes", size);
    }
}

// 创建测试数据
int create_test_data(sqlite3 *db, int record_count) {
    char *err_msg = 0;
    char sql[1024];
    
    // 创建测试表
    const char *create_table = 
        "CREATE TABLE IF NOT EXISTS test_data ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT, "
        "description TEXT, "
        "data BLOB, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    
    int rc = sqlite3_exec(db, create_table, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        return rc;
    }
    
    // 开始事务以提高插入性能
    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "开始事务失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        return rc;
    }
    
    // 插入测试数据
    sqlite3_stmt *stmt;
    const char *insert_sql = "INSERT INTO test_data (name, description, data) VALUES (?, ?, ?);";
    
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备语句失败: %s\n", sqlite3_errmsg(db));
        return rc;
    }
    
    printf("正在插入 %d 条记录...\n", record_count);
    
    for (int i = 1; i <= record_count; i++) {
        char name[64];
        char desc[256];
        char blob_data[512];
        
        // 生成测试数据
        snprintf(name, sizeof(name), "user_%06d", i);
        snprintf(desc, sizeof(desc), 
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ 123456789", i);
        
        // 生成一些重复的二进制数据
        memset(blob_data, i % 256, sizeof(blob_data));
        
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, desc, -1, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 3, blob_data, sizeof(blob_data), SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "插入记录 %d 失败: %s\n", i, sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return rc;
        }
        
        sqlite3_reset(stmt);
        
        if (i % 5000 == 0) {
            printf("已插入 %d 条记录\n", i);
        }
    }
    
    sqlite3_finalize(stmt);
    
    // 提交事务
    rc = sqlite3_exec(db, "COMMIT;", 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "提交事务失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        return rc;
    }
    
    return SQLITE_OK;
}

// 验证数据完整性
int verify_data(sqlite3 *db, int expected_count) {
    sqlite3_stmt *stmt;
    const char *count_sql = "SELECT COUNT(*) FROM test_data;";
    
    int rc = sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备计数语句失败: %s\n", sqlite3_errmsg(db));
        return rc;
    }
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        printf("数据验证: 期望 %d 条记录, 实际 %d 条记录\n", expected_count, count);
        
        if (count != expected_count) {
            printf("❌ 数据验证失败!\n");
            sqlite3_finalize(stmt);
            return SQLITE_ERROR;
        } else {
            printf("✅ 数据验证成功!\n");
        }
    } else {
        fprintf(stderr, "执行计数查询失败: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }
    
    sqlite3_finalize(stmt);
    
    // 随机验证几条记录的内容
    const char *sample_sql = "SELECT name, description FROM test_data WHERE id = ?;";
    rc = sqlite3_prepare_v2(db, sample_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备采样语句失败: %s\n", sqlite3_errmsg(db));
        return rc;
    }
    
    printf("验证部分记录内容...\n");
    for (int i = 0; i < 3; i++) {
        int test_id = (rand() % expected_count) + 1;
        sqlite3_bind_int(stmt, 1, test_id);
        
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            const char *name = (const char*)sqlite3_column_text(stmt, 0);
            const char *desc = (const char*)sqlite3_column_text(stmt, 1);
            printf("  记录 %d: %s - %.50s...\n", test_id, name, desc);
        }
        
        sqlite3_reset(stmt);
    }
    
    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

int main() {
    printf("=== 真正的Zlib压缩数据库测试 ===\n\n");
    printf("SQLite版本: %s\n", sqlite3_libversion());
    
    // 初始化随机种子
    srand(time(NULL));
    
    sqlite3 *normal_db = NULL;
    sqlite3 *compressed_db = NULL;
    int rc;
    const int RECORD_COUNT = 20000;  // 测试记录数量
    
    // 删除旧的测试文件
    remove("normal.db");
    remove("compressed_zlib.db");
    
    // 1. 创建普通数据库
    printf("1. 创建普通数据库...\n");
    rc = sqlite3_open("normal.db", &normal_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开普通数据库: %s\n", sqlite3_errmsg(normal_db));
        return 1;
    }
    
    clock_t start_time = clock();
    rc = create_test_data(normal_db, RECORD_COUNT);
    if (rc != SQLITE_OK) {
        sqlite3_close(normal_db);
        return 1;
    }
    clock_t normal_time = clock() - start_time;
    
    sqlite3_close(normal_db);
    
    // 2. 创建压缩VFS并测试
    printf("\n2. 创建真正的Zlib压缩VFS...\n");
    
    // 注册zlib压缩VFS
    rc = sqlite3_ccvfs_create("zlib_vfs", NULL, "zlib", NULL, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建压缩VFS失败: %d\n", rc);
        return 1;
    }
    
    // 3. 创建压缩数据库
    printf("\n3. 创建压缩数据库...\n");
    rc = sqlite3_open_v2("compressed_zlib.db", &compressed_db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "zlib_vfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开压缩数据库: %s\n", sqlite3_errmsg(compressed_db));
        sqlite3_ccvfs_destroy("zlib_vfs");
        return 1;
    }
    
    start_time = clock();
    rc = create_test_data(compressed_db, RECORD_COUNT);
    if (rc != SQLITE_OK) {
        sqlite3_close(compressed_db);
        sqlite3_ccvfs_destroy("zlib_vfs");
        return 1;
    }
    clock_t compressed_time = clock() - start_time;
    
    // 4. 验证压缩数据库的数据完整性
    printf("\n4. 验证压缩数据库数据完整性...\n");
    rc = verify_data(compressed_db, RECORD_COUNT);
    if (rc != SQLITE_OK) {
        sqlite3_close(compressed_db);
        sqlite3_ccvfs_destroy("zlib_vfs");
        return 1;
    }
    
    sqlite3_close(compressed_db);
    sqlite3_ccvfs_destroy("zlib_vfs");
    
    // 5. 比较文件大小和性能
    printf("\n=== 压缩效果和性能对比 ===\n");
    
    long normal_size = get_file_size("normal.db");
    long compressed_size = get_file_size("compressed_zlib.db");
    
    char normal_size_str[64], compressed_size_str[64];
    format_size(normal_size, normal_size_str, sizeof(normal_size_str));
    format_size(compressed_size, compressed_size_str, sizeof(compressed_size_str));
    
    printf("普通数据库大小: %ld bytes (%s)\n", normal_size, normal_size_str);
    printf("压缩数据库大小: %ld bytes (%s)\n", compressed_size, compressed_size_str);
    
    if (normal_size > 0) {
        double compression_ratio = (double)compressed_size / normal_size * 100.0;
        long space_saved = normal_size - compressed_size;
        char space_saved_str[64];
        format_size(space_saved, space_saved_str, sizeof(space_saved_str));
        
        printf("压缩率: %.2f%%\n", compression_ratio);
        printf("空间节省: %ld bytes (%s)\n", space_saved, space_saved_str);
        
        if (compression_ratio < 100.0) {
            printf("✅ 压缩成功! 节省了 %.1f%% 的空间\n", 100.0 - compression_ratio);
        } else {
            printf("⚠️  压缩效果不明显或文件变大\n");
        }
    }
    
    // 性能对比
    printf("\n=== 性能对比 ===\n");
    double normal_sec = (double)normal_time / CLOCKS_PER_SEC;
    double compressed_sec = (double)compressed_time / CLOCKS_PER_SEC;
    
    printf("普通数据库写入时间: %.2f 秒\n", normal_sec);
    printf("压缩数据库写入时间: %.2f 秒\n", compressed_sec);
    
    if (compressed_sec > 0) {
        double slowdown = compressed_sec / normal_sec;
        printf("性能影响: %.1fx 倍 ", slowdown);
        if (slowdown < 1.5) {
            printf("(影响很小)\n");
        } else if (slowdown < 2.0) {
            printf("(影响适中)\n");
        } else {
            printf("(影响较大)\n");
        }
    }
    
    // 6. 测试读取性能
    printf("\n6. 测试读取性能...\n");
    
    // 重新打开数据库进行读取测试
    rc = sqlite3_ccvfs_create("zlib_vfs", NULL, "zlib", NULL, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "重新创建压缩VFS失败: %d\n", rc);
        return 1;
    }
    
    rc = sqlite3_open_v2("compressed_zlib.db", &compressed_db, 
                        SQLITE_OPEN_READONLY, "zlib_vfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法重新打开压缩数据库: %s\n", sqlite3_errmsg(compressed_db));
        sqlite3_ccvfs_destroy("zlib_vfs");
        return 1;
    }
    
    // 执行一些读取查询
    sqlite3_stmt *stmt;
    const char *query = "SELECT COUNT(*), AVG(LENGTH(description)) FROM test_data WHERE id > ? AND id < ?;";
    
    start_time = clock();
    rc = sqlite3_prepare_v2(compressed_db, query, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        for (int i = 0; i < 10; i++) {
            int start_id = i * 1000;
            int end_id = start_id + 5000;
            
            sqlite3_bind_int(stmt, 1, start_id);
            sqlite3_bind_int(stmt, 2, end_id);
            
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                int count = sqlite3_column_int(stmt, 0);
                double avg_len = sqlite3_column_double(stmt, 1);
                if (i == 0) {  // 只显示第一次查询结果
                    printf("查询结果示例: 范围内记录数=%d, 平均描述长度=%.1f\n", count, avg_len);
                }
            }
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
    clock_t read_time = clock() - start_time;
    
    printf("压缩数据库读取时间: %.3f 秒 (10次查询)\n", (double)read_time / CLOCKS_PER_SEC);
    
    sqlite3_close(compressed_db);
    sqlite3_ccvfs_destroy("zlib_vfs");
    
    printf("\n✅ 真正的Zlib压缩测试完成!\n");
    return 0;
}