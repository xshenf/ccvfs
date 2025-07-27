#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <locale.h>
#include "../include/compress_vfs.h"

// 获取文件大小的辅助函数
long get_file_size(const char* filename) {
    struct stat stat_buf;
    if (stat(filename, &stat_buf) == 0) {
        return stat_buf.st_size;
    }
    return -1;
}

// 生成随机字符串
void generate_random_string(char* str, int length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < length - 1; i++) {
        int key = rand() % (sizeof(charset) - 1);
        str[i] = charset[key];
    }
    str[length - 1] = '\0';
}

int main(int argc, char **argv) {
    sqlite3 *db_normal, *db_compressed;
    int rc;
    char *errmsg = 0;
    long normal_size, compressed_size;
    
    // 设置本地化以正确显示中文
    setlocale(LC_ALL, "");
    
    printf("SQLite版本: %s\n", sqlite3_libversion());
    
    // 删除可能存在的旧数据库文件
    remove("normal.db");
    remove("compressed.db");
    
    // 创建普通数据库
    printf("正在创建普通数据库...\n");
    rc = sqlite3_open_v2("normal.db", &db_normal, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "打开普通数据库失败: %s\n", sqlite3_errmsg(db_normal));
        sqlite3_close(db_normal);
        return 1;
    }
    
    // 创建压缩加密VFS
    printf("正在创建压缩VFS...\n");
    rc = sqlite3_ccvfs_create("ccvfs", NULL, "rle", "xor");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建压缩加密VFS失败: %d\n", rc);
        return 1;
    }
    
    // 创建压缩数据库
    printf("正在创建压缩数据库...\n");
    rc = sqlite3_open_v2("compressed.db", &db_compressed, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "ccvfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "打开压缩数据库失败: %s\n", sqlite3_errmsg(db_compressed));
        sqlite3_close(db_compressed);
        return 1;
    }
    
    // 在两个数据库中创建相同的表结构
    const char* create_table_sql = "CREATE TABLE test_data (id INTEGER PRIMARY KEY, data TEXT, description TEXT);";
    
    rc = sqlite3_exec(db_normal, create_table_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "在普通数据库中创建表失败: %s\n", errmsg);
        sqlite3_free(errmsg);
        return 1;
    }
    
    rc = sqlite3_exec(db_compressed, create_table_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "在压缩数据库中创建表失败: %s\n", errmsg);
        sqlite3_free(errmsg);
        return 1;
    }
    
    // 开始事务以提高插入性能
    sqlite3_exec(db_normal, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db_compressed, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    // 插入大量数据
    printf("正在插入大量数据...\n");
    const int record_count = 10000;
    char data_str[256];
    char description_str[512];
    char insert_sql[1024];
    
    for (int i = 1; i <= record_count; i++) {
        generate_random_string(data_str, sizeof(data_str));
        generate_random_string(description_str, sizeof(description_str));
        
        // 插入到普通数据库
        snprintf(insert_sql, sizeof(insert_sql), 
                "INSERT INTO test_data (id, data, description) VALUES (%d, '%s', '%s');", 
                i, data_str, description_str);
        sqlite3_exec(db_normal, insert_sql, NULL, NULL, &errmsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "在普通数据库中插入数据失败: %s\n", errmsg);
            sqlite3_free(errmsg);
        }
        
        // 插入到压缩数据库
        sqlite3_exec(db_compressed, insert_sql, NULL, NULL, &errmsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "在压缩数据库中插入数据失败: %s\n", errmsg);
            sqlite3_free(errmsg);
        }
        
        if (i % 1000 == 0) {
            printf("已插入 %d 条记录\n", i);
        }
    }
    
    // 提交事务
    sqlite3_exec(db_normal, "COMMIT;", NULL, NULL, NULL);
    sqlite3_exec(db_compressed, "COMMIT;", NULL, NULL, NULL);
    
    // 关闭数据库
    sqlite3_close(db_normal);
    sqlite3_close(db_compressed);
    
    // 销毁压缩加密VFS
    sqlite3_ccvfs_destroy("ccvfs");
    
    // 获取并比较文件大小
    normal_size = get_file_size("normal.db");
    compressed_size = get_file_size("compressed.db");
    
    if (normal_size == -1 || compressed_size == -1) {
        fprintf(stderr, "无法获取数据库文件大小\n");
        return 1;
    }
    
    printf("\n=== 数据库大小比较 ===\n");
    printf("普通数据库大小: %ld 字节 (%.2f MB)\n", normal_size, normal_size / (1024.0 * 1024.0));
    printf("压缩数据库大小: %ld 字节 (%.2f MB)\n", compressed_size, compressed_size / (1024.0 * 1024.0));
    printf("压缩率: %.2f%%\n", (1.0 - (double)compressed_size / normal_size) * 100);
    printf("空间节省: %ld 字节 (%.2f MB)\n", normal_size - compressed_size, (normal_size - compressed_size) / (1024.0 * 1024.0));
    
    return 0;
}