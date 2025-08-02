#include "ccvfs.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

// Function declarations
extern int sqlite3_ccvfs_compress_database(
    const char *source_db,
    const char *compressed_db,
    const char *compress_algorithm,
    const char *encrypt_algorithm,
    int compression_level
);

extern int sqlite3_ccvfs_decompress_database(
    const char *compressed_db,
    const char *output_db
);

extern int sqlite3_ccvfs_get_stats(const char *compressed_db, CCVFSStats *stats);

static long get_file_size(const char* filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

static int create_test_database(const char *db_name) {
    sqlite3 *db = NULL;
    char *err_msg = NULL;
    int rc;
    
    printf("创建测试数据库: %s\n", db_name);
    
    rc = sqlite3_open(db_name, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法创建数据库: %s\n", sqlite3_errmsg(db));
        return rc;
    }
    
    // Create tables with various data types
    const char *sql_create = 
        "CREATE TABLE users ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  email TEXT UNIQUE,"
        "  age INTEGER,"
        "  salary REAL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  profile BLOB"
        ");"
        
        "CREATE TABLE products ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  description TEXT,"
        "  price REAL NOT NULL,"
        "  category TEXT,"
        "  in_stock BOOLEAN DEFAULT TRUE"
        ");"
        
        "CREATE TABLE orders ("
        "  id INTEGER PRIMARY KEY,"
        "  user_id INTEGER REFERENCES users(id),"
        "  product_id INTEGER REFERENCES products(id),"
        "  quantity INTEGER DEFAULT 1,"
        "  order_date DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    
    rc = sqlite3_exec(db, sql_create, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return rc;
    }
    
    // Insert test data
    printf("插入测试数据...\n");
    
    // Insert users
    for (int i = 1; i <= 1000; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql),
                "INSERT INTO users (name, email, age, salary) VALUES "
                "('用户%d', 'user%d@test.com', %d, %.2f);",
                i, i, 20 + (i % 50), 30000.0 + (i % 100) * 100.0);
        
        rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "插入用户数据失败: %s\n", err_msg);
            sqlite3_free(err_msg);
            break;
        }
    }
    
    // Insert products
    const char *categories[] = {"电子产品", "书籍", "服装", "食品", "家具"};
    for (int i = 1; i <= 500; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql),
                "INSERT INTO products (name, description, price, category) VALUES "
                "('产品%d', '这是产品%d的详细描述，包含了很多重复的文本内容用于测试压缩效果。', %.2f, '%s');",
                i, i, 10.0 + (i % 1000), categories[i % 5]);
        
        rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "插入产品数据失败: %s\n", err_msg);
            sqlite3_free(err_msg);
            break;
        }
    }
    
    // Insert orders
    for (int i = 1; i <= 2000; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql),
                "INSERT INTO orders (user_id, product_id, quantity) VALUES "
                "(%d, %d, %d);",
                1 + (i % 1000), 1 + (i % 500), 1 + (i % 5));
        
        rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "插入订单数据失败: %s\n", err_msg);
            sqlite3_free(err_msg);
            break;
        }
    }
    
    sqlite3_close(db);
    printf("测试数据库创建完成\n");
    return SQLITE_OK;
}

static int verify_database_content(const char *db_name) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int user_count = 0, product_count = 0, order_count = 0;
    
    printf("验证数据库内容: %s\n", db_name);
    
    rc = sqlite3_open_v2(db_name, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开数据库: %s\n", sqlite3_errmsg(db));
        return rc;
    }
    
    // Count users
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user_count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    // Count products
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM products", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            product_count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    // Count orders
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM orders", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            order_count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_close(db);
    
    printf("数据库记录数:\n");
    printf("  用户: %d\n", user_count);
    printf("  产品: %d\n", product_count);
    printf("  订单: %d\n", order_count);
    
    // Verify expected counts
    if (user_count == 1000 && product_count == 500 && order_count == 2000) {
        printf("✓ 数据库内容验证通过\n");
        return SQLITE_OK;
    } else {
        printf("✗ 数据库内容验证失败\n");
        return SQLITE_ERROR;
    }
}

static void test_compression_algorithm(const char *compress_algo, const char *encrypt_algo) {
    printf("\n=== 测试压缩算法: %s (加密: %s) ===\n", 
           compress_algo, encrypt_algo ? encrypt_algo : "无");
    
    const char *source_db = "test_source.db";
    const char *compressed_db = "test_compressed.ccvfs";
    const char *decompressed_db = "test_decompressed.db";
    
    int rc;
    long original_size, compressed_size, decompressed_size;
    CCVFSStats stats;
    
    // Create test database
    rc = create_test_database(source_db);
    if (rc != SQLITE_OK) {
        printf("✗ 创建测试数据库失败\n");
        return;
    }
    
    original_size = get_file_size(source_db);
    printf("原始数据库大小: %ld 字节 (%.2f MB)\n", 
           original_size, (double)original_size / (1024.0 * 1024.0));
    
    // Test compression
    printf("\n--- 压缩测试 ---\n");
    clock_t start_time = clock();
    
    rc = sqlite3_ccvfs_compress_database(source_db, compressed_db, 
                                         compress_algo, encrypt_algo, 6);
    
    clock_t compress_time = clock() - start_time;
    
    if (rc != SQLITE_OK) {
        printf("✗ 压缩失败，错误代码: %d\n", rc);
        goto cleanup;
    }
    
    compressed_size = get_file_size(compressed_db);
    printf("✓ 压缩成功\n");
    printf("压缩时间: %.2f 秒\n", (double)compress_time / CLOCKS_PER_SEC);
    
    // Get compression statistics
    if (sqlite3_ccvfs_get_stats(compressed_db, &stats) == SQLITE_OK) {
        printf("压缩统计:\n");
        printf("  压缩大小: %ld 字节 (%.2f MB)\n", 
               compressed_size, (double)compressed_size / (1024.0 * 1024.0));
        printf("  压缩比: %u%%\n", stats.compression_ratio);
        printf("  节省空间: %ld 字节\n", original_size - compressed_size);
    }
    
    // Test decompression
    printf("\n--- 解压测试 ---\n");
    start_time = clock();
    
    rc = sqlite3_ccvfs_decompress_database(compressed_db, decompressed_db);
    
    clock_t decompress_time = clock() - start_time;
    
    if (rc != SQLITE_OK) {
        printf("✗ 解压失败，错误代码: %d\n", rc);
        goto cleanup;
    }
    
    decompressed_size = get_file_size(decompressed_db);
    printf("✓ 解压成功\n");
    printf("解压时间: %.2f 秒\n", (double)decompress_time / CLOCKS_PER_SEC);
    printf("解压大小: %ld 字节 (%.2f MB)\n", 
           decompressed_size, (double)decompressed_size / (1024.0 * 1024.0));
    
    // Verify decompressed content
    printf("\n--- 数据完整性验证 ---\n");
    rc = verify_database_content(decompressed_db);
    
    if (rc == SQLITE_OK && decompressed_size == original_size) {
        printf("✓ 数据完整性验证通过\n");
    } else {
        printf("✗ 数据完整性验证失败\n");
    }
    
    // Summary
    printf("\n--- 测试总结 ---\n");
    printf("算法: %s + %s\n", compress_algo, encrypt_algo ? encrypt_algo : "无加密");
    printf("原始大小: %ld 字节\n", original_size);
    printf("压缩大小: %ld 字节\n", compressed_size);
    printf("解压大小: %ld 字节\n", decompressed_size);
    if (original_size > 0) {
        printf("压缩率: %.2f%%\n", (double)(original_size - compressed_size) * 100.0 / original_size);
    }
    printf("压缩时间: %.2f 秒\n", (double)compress_time / CLOCKS_PER_SEC);
    printf("解压时间: %.2f 秒\n", (double)decompress_time / CLOCKS_PER_SEC);

cleanup:
    // Clean up test files
    remove(source_db);
    remove(compressed_db);
    remove(decompressed_db);
}

int main(int argc, char *argv[]) {
    printf("SQLite数据库压缩解压功能测试\n");
    printf("==============================\n");
    
    // Test different compression algorithms
    test_compression_algorithm("rle", NULL);
    test_compression_algorithm("rle", "xor");
    test_compression_algorithm("zlib", NULL);
    test_compression_algorithm("zlib", "xor");
    
    // Test with LZ4 if available
    #ifdef HAVE_LZ4
    test_compression_algorithm("lz4", NULL);
    test_compression_algorithm("lz4", "aes128");
    #endif
    
    printf("\n所有测试完成！\n");
    return 0;
}