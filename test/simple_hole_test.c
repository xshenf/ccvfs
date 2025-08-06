#include "../include/ccvfs.h"
#include "../sqlite3/sqlite3.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    sqlite3 *db;
    int rc;
    
    printf("=== 简化空洞检测测试 ===\n");
    
    // 创建CCVFS实例，启用空洞检测  
    rc = sqlite3_ccvfs_create("simple_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ 创建VFS失败: %d\n", rc);
        return 1;
    }
    printf("✅ CCVFS创建成功\n");
    
    // 删除测试文件
    remove("simple_holes.db");
    
    // 打开数据库
    rc = sqlite3_open_v2("simple_holes.db", &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "simple_vfs");
    if (rc != SQLITE_OK) {
        printf("❌ 打开数据库失败: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    printf("✅ 数据库打开成功\n");
    
    // 创建测试表
    rc = sqlite3_exec(db, "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)", 
                     NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ 创建表失败: %s\n", sqlite3_errmsg(db)); 
        return 1;
    }
    printf("✅ 测试表创建成功\n");
    
    // 插入少量数据
    printf("📝 插入测试数据...\n");
    for (int i = 1; i <= 20; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test (data) VALUES ('测试数据 %d')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("❌ 插入数据失败: %s\n", sqlite3_errmsg(db));
            return 1;
        }
    }
    printf("✅ 插入了20行测试数据\n");
    
    // 同步数据库
    printf("🔄 同步数据库...\n");  
    sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);
    
    // 删除一些数据来创建空洞
    printf("🗑️ 删除部分数据...\n");
    rc = sqlite3_exec(db, "DELETE FROM test WHERE id % 2 = 0", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ 删除数据失败: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    // 再次同步
    printf("🔄 再次同步...\n");
    sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);
    
    // 插入新数据测试空洞重用
    printf("📝 插入新数据...\n");
    for (int i = 21; i <= 25; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test (data) VALUES ('新数据 %d')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("❌ 插入新数据失败: %s\n", sqlite3_errmsg(db));
            return 1;
        }
    }
    printf("✅ 插入了5行新数据\n");
    
    // 关闭数据库
    sqlite3_close(db);
    printf("✅ 数据库关闭成功\n");
    
    // 销毁VFS
    sqlite3_ccvfs_destroy("simple_vfs");
    printf("✅ VFS销毁成功\n");
    
    printf("🎉 简化测试完成！\n");
    return 0;
}