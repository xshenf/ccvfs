#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include "../include/compress_vfs.h"

int main(int argc, char **argv) {
    sqlite3 *db;
    int rc;
    
    // 检查是否启用调试模式
    int debug_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            break;
        }
    }
    
    if (debug_mode) {
        printf("Debug mode enabled\n");
    }
    
    printf("SQLite version: %s\n", sqlite3_libversion());
    
    // 创建压缩加密VFS
    rc = sqlite3_ccvfs_create("ccvfs", NULL, "rle", "xor");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create compression and encryption VFS: %d\n", rc);
        return 1;
    }
    
    printf("Successfully created compression and encryption VFS\n");
    
    // 使用压缩加密VFS打开数据库
    rc = sqlite3_open_v2("test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "ccvfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    printf("Successfully opened database using compression and encryption VFS\n");
    
    // 执行一些SQL操作
    char *errmsg = 0;
    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY, data TEXT);", 
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create table: %s\n", errmsg);
        sqlite3_free(errmsg);
    } else {
        printf("Table created successfully\n");
    }
    
    rc = sqlite3_exec(db, "INSERT INTO test (data) VALUES ('测试数据1'), ('测试数据2');", 
                      NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to insert data: %s\n", errmsg);
        sqlite3_free(errmsg);
    } else {
        printf("Data inserted successfully\n");
    }
    
    // 查询数据
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, data FROM test;", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        printf("Query results:\n");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const char *data = (const char *)sqlite3_column_text(stmt, 1);
            printf("ID: %d, Data: %s\n", id, data);
        }
    }
    sqlite3_finalize(stmt);
    
    // 关闭数据库
    sqlite3_close(db);
    
    // 重新打开数据库测试读取功能
    printf("\n=== Testing compressed database read ===\n");
    rc = sqlite3_open_v2("test.db", &db, SQLITE_OPEN_READONLY, "ccvfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to reopen database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    printf("Successfully reopened compressed database\n");
    
    // 查询数据验证读取功能
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test;", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            printf("Number of records in table: %d\n", count);
        }
    }
    sqlite3_finalize(stmt);
    
    // 关闭数据库
    sqlite3_close(db);
    
    // 销毁压缩加密VFS
    sqlite3_ccvfs_destroy("ccvfs");
    
    printf("Test completed\n");
    return 0;
}