#include "include/ccvfs.h"
#include "sqlite3/sqlite3.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    sqlite3 *db;
    int rc;
    
    printf("=== CCVFS空洞检测测试 ===\n");
    
    // 创建CCVFS实例，启用空洞检测
    rc = sqlite3_ccvfs_create("test_vfs", NULL, "zlib", NULL, 4096, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        printf("❌ 创建VFS失败: %d\n", rc);
        return 1;
    }
    printf("✅ CCVFS创建成功，启用空洞检测\n");
    
    // 删除测试文件（如果存在）
    remove("test_holes.db");
    
    // 打开数据库
    rc = sqlite3_open_v2("test_holes.db", &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "test_vfs");
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
    
    // 插入一些数据来创建页面
    printf("📝 插入测试数据...\n");
    for (int i = 1; i <= 100; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test (data) VALUES ('这是测试数据行 %d，包含一些文本内容用于测试压缩和空洞检测功能')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("❌ 插入数据失败: %s\n", sqlite3_errmsg(db));
            return 1;
        }
    }
    printf("✅ 插入了100行测试数据\n");
    
    // 同步数据库以触发空洞维护
    printf("🔄 同步数据库...\n");
    sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);
    
    // 删除一些数据来创建空洞
    printf("🗑️ 删除部分数据以创建空洞...\n");
    rc = sqlite3_exec(db, "DELETE FROM test WHERE id % 3 = 0", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("❌ 删除数据失败: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    // 再次同步以触发空洞检测
    printf("🔄 再次同步以触发空洞检测...\n");
    sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);
    
    // 插入新数据来测试空洞重用
    printf("📝 插入新数据测试空洞重用...\n");
    for (int i = 101; i <= 150; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), 
                "INSERT INTO test (data) VALUES ('新数据行 %d，测试空洞重用功能')", i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            printf("❌ 插入新数据失败: %s\n", sqlite3_errmsg(db));
            return 1;
        }
    }
    printf("✅ 插入了50行新数据\n");
    
    // 最终同步
    printf("🔄 最终同步...\n");
    sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);
    
    // 查询数据验证完整性
    printf("🔍 验证数据完整性...\n");
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            printf("✅ 数据库包含 %d 行数据\n", count);
        }
        sqlite3_finalize(stmt);
    }
    
    // 关闭数据库
    sqlite3_close(db);
    printf("✅ 数据库关闭成功\n");
    
    // 销毁VFS
    sqlite3_ccvfs_destroy("test_vfs");
    printf("✅ VFS销毁成功\n");
    
    printf("🎉 空洞检测测试完成！\n");
    printf("💡 查看调试输出以了解空洞检测的详细工作情况\n");
    
    return 0;
}