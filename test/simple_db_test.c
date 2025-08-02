#include "ccvfs.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>

void cleanup_test_files() {
    printf("🧹 Cleaning up old test files...\n");
    
    // Remove main database file
    if (remove("simple_test.db") == 0) {
        printf("   Removed simple_test.db\n");
    }
    if (remove("simple_test.ccvfs") == 0) {
        printf("   Removed simple_test.ccvfs\n");
    }
    
    // Remove journal file
    if (remove("simple_test.db-journal") == 0) {
        printf("   Removed simple_test.db-journal\n");
    }
    
    // Remove WAL file (if exists)
    if (remove("simple_test.db-wal") == 0) {
        printf("   Removed simple_test.db-wal\n");
    }
    
    // Remove SHM file (if exists)
    if (remove("simple_test.db-shm") == 0) {
        printf("   Removed simple_test.db-shm\n");
    }
    
    printf("✅ Cleanup completed\n\n");
}


int main() {
    printf("简单的数据库压缩解压测试\n");
    printf("==============================\n");

    cleanup_test_files();
    
    sqlite3 *db;
    int rc;
    char *err_msg = NULL;
    
    // 创建一个简单的测试数据库
    printf("1. 创建测试数据库...\n");
    rc = sqlite3_open("simple_test.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法创建数据库: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    const char *sql = 
        "CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO test VALUES (1, 'Hello');"
        "INSERT INTO test VALUES (2, 'World');"
        "INSERT INTO test VALUES (3, 'SQLite');";
    
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL错误: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }
    
    sqlite3_close(db);
    printf("✓ 测试数据库创建成功\n");
    
    // 尝试压缩数据库
    printf("2. 压缩数据库...\n");
    rc = sqlite3_ccvfs_compress_database("simple_test.db", "simple_test.ccvfs", 
                                         "zlib", NULL, 6);
    if (rc == SQLITE_OK) {
        printf("✓ 数据库压缩成功\n");
    } else {
        printf("✗ 数据库压缩失败: %d\n", rc);
        return 1;
    }
    
    // 尝试解压数据库
    printf("3. 解压数据库...\n");
    rc = sqlite3_ccvfs_decompress_database("simple_test.ccvfs", "simple_restored.db");
    if (rc == SQLITE_OK) {
        printf("✓ 数据库解压成功\n");
    } else {
        printf("✗ 数据库解压失败: %d\n", rc);
        return 1;
    }
    
    // 验证解压后的数据
    printf("4. 验证解压后的数据...\n");
    rc = sqlite3_open("simple_restored.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开解压后的数据库: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT * FROM test ORDER BY id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL查询错误: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        count++;
        int id = sqlite3_column_int(stmt, 0);
        const char *name = (const char*)sqlite3_column_text(stmt, 1);
        printf("  记录 %d: ID=%d, Name=%s\n", count, id, name);
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    if (count == 3) {
        printf("✓ 数据验证成功，所有记录完整\n");
        printf("\n测试完成！数据库压缩解压功能正常工作。\n");
        return 0;
    } else {
        printf("✗ 数据验证失败，预期3条记录，实际%d条\n", count);
        return 1;
    }
}