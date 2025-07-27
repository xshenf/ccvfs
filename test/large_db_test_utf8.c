#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include "../include/compress_vfs.h"

// 获取文件大小的辅助函数
long get_file_size(const char* filename) {
    struct stat stat_buf;
    if (stat(filename, &stat_buf) == 0) {
        return stat_buf.st_size;
    }
    return -1;
}

// 生成重复字符串（更适合测试压缩效果）
void generate_repetitive_string(char* str, int length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    // 生成一个较短的模式，然后重复它
    int pattern_length = 10;
    char pattern[11];
    for (int i = 0; i < pattern_length; i++) {
        int key = rand() % (sizeof(charset) - 1);
        pattern[i] = charset[key];
    }
    pattern[pattern_length] = '\0';
    
    // 重复模式以填满整个字符串
    int pos = 0;
    while (pos < length - 1) {
        for (int i = 0; i < pattern_length && pos < length - 1; i++) {
            str[pos++] = pattern[i];
        }
    }
    str[length - 1] = '\0';
}

int main(int argc, char **argv) {
    sqlite3 *db_normal, *db_compressed;
    int rc;
    char *errmsg = 0;
    long normal_size, compressed_size;
    
    printf("SQLite Version: %s\n", sqlite3_libversion());
    
    // 删除可能存在的旧数据库文件
    remove("normal.db");
    remove("compressed.db");
    
    // 创建普通数据库
    printf("Creating normal database...\n");
    rc = sqlite3_open_v2("normal.db", &db_normal, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open normal database: %s\n", sqlite3_errmsg(db_normal));
        sqlite3_close(db_normal);
        return 1;
    }
    
    // 创建压缩加密VFS
    printf("Creating compressed VFS...\n");
    rc = sqlite3_ccvfs_create("ccvfs", NULL, "rle", "xor");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create compressed VFS: %d\n", rc);
        return 1;
    }
    
    // 创建压缩数据库
    printf("Creating compressed database...\n");
    rc = sqlite3_open_v2("compressed.db", &db_compressed, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "ccvfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open compressed database: %s\n", sqlite3_errmsg(db_compressed));
        sqlite3_close(db_compressed);
        return 1;
    }
    
    // 在两个数据库中创建相同的表结构
    const char* create_table_sql = "CREATE TABLE test_data (id INTEGER PRIMARY KEY, data TEXT, description TEXT);";
    
    rc = sqlite3_exec(db_normal, create_table_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create table in normal database: %s\n", errmsg);
        sqlite3_free(errmsg);
        return 1;
    }
    
    rc = sqlite3_exec(db_compressed, create_table_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create table in compressed database: %s\n", errmsg);
        sqlite3_free(errmsg);
        return 1;
    }
    
    // 开始事务以提高插入性能
    sqlite3_exec(db_normal, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db_compressed, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    // 插入大量数据（增加到50000条记录）
    printf("Inserting large amount of data...\n");
    const int record_count = 50000;
    char data_str[512];  // 增加字符串长度
    char description_str[1024];
    char insert_sql[2048];
    
    for (int i = 1; i <= record_count; i++) {
        generate_repetitive_string(data_str, sizeof(data_str));
        generate_repetitive_string(description_str, sizeof(description_str));
        
        // 插入到普通数据库
        snprintf(insert_sql, sizeof(insert_sql), 
                "INSERT INTO test_data (id, data, description) VALUES (%d, '%s', '%s');", 
                i, data_str, description_str);
        sqlite3_exec(db_normal, insert_sql, NULL, NULL, &errmsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to insert data into normal database: %s\n", errmsg);
            sqlite3_free(errmsg);
        }
        
        // 插入到压缩数据库
        sqlite3_exec(db_compressed, insert_sql, NULL, NULL, &errmsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to insert data into compressed database: %s\n", errmsg);
            sqlite3_free(errmsg);
        }
        
        if (i % 5000 == 0) {
            printf("Inserted %d records\n", i);
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
        fprintf(stderr, "Failed to get database file sizes\n");
        return 1;
    }
    
    printf("\n=== Database Size Comparison ===\n");
    printf("Normal database size: %ld bytes (%.2f MB)\n", normal_size, normal_size / (1024.0 * 1024.0));
    printf("Compressed database size: %ld bytes (%.2f MB)\n", compressed_size, compressed_size / (1024.0 * 1024.0));
    printf("Compression ratio: %.2f%%\n", (1.0 - (double)compressed_size / normal_size) * 100);
    printf("Space saved: %ld bytes (%.2f MB)\n", normal_size - compressed_size, (normal_size - compressed_size) / (1024.0 * 1024.0));
    
    return 0;
}