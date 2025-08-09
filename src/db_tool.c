#include "ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

// Function declarations from db_compress_tool.c
extern int sqlite3_ccvfs_compress_database_with_page_size(
    const char *source_db,
    const char *compressed_db,
    const char *compress_algorithm,
    const char *encrypt_algorithm,
    uint32_t page_size,
    int compression_level
);

extern int sqlite3_ccvfs_decompress_database(
    const char *compressed_db,
    const char *output_db
);

extern int sqlite3_ccvfs_get_stats(const char *compressed_db, CCVFSStats *stats);

// Batch writer test functions
static int perform_batch_test(const char *db_path, int enable_batch, 
                             int max_pages, int max_memory_mb, 
                             int test_records, int verbose);
static int show_batch_stats(const char *db_path, int verbose);
static int flush_batch_writer(const char *db_path, int verbose);

static void print_usage(const char *program_name) {
    printf("SQLite数据库压缩解压工具\n");
    printf("用法: %s [选项] <操作> <文件>\n\n", program_name);
    
    printf("操作:\n");
    printf("  compress <源数据库> <目标文件>    压缩SQLite数据库\n");
    printf("  decompress <压缩文件> <输出文件>  解压数据库到标准SQLite格式\n");
    printf("  info <压缩文件>                   显示压缩文件信息\n");
    printf("  batch-test <数据库文件>           测试批量写入功能\n");
    printf("  batch-stats <数据库文件>          显示批量写入统计信息\n");
    printf("  batch-flush <数据库文件>          强制刷新批量写入缓冲区\n\n");
    
    printf("通用选项:\n");
    printf("  -h, --help                       显示帮助信息\n");
    printf("  -v, --verbose                    详细输出\n\n");
    
    printf("压缩/解压选项:\n");
    printf("  -c, --compress-algo <算法>       压缩算法 (rle, lz4, zlib)\n");
    printf("  -e, --encrypt-algo <算法>        加密算法 (xor, aes128, aes256, chacha20)\n");
    printf("  -l, --level <等级>               压缩等级 (1-9, 默认: 6)\n");
    printf("  -b, --page-size <大小>          页大小 (1K, 4K, 8K, 16K, 32K, 64K, 128K, 256K, 512K, 1M, 默认: 64K)\n\n");
    
    printf("批量写入测试选项 (仅用于 batch-test):\n");
    printf("  --batch-enable                   启用批量写入 (默认: 禁用)\n");
    printf("  --batch-pages <数量>             批量写入最大页数 (默认: 100)\n");
    printf("  --batch-memory <MB>              批量写入最大内存 (默认: 16MB)\n");
    printf("  --batch-records <数量>           批量测试记录数 (默认: 1000)\n\n");
    
    printf("页大小选项:\n");
    printf("  1K, 1024         1KB 页 (适合极小文件)\n");
    printf("  4K, 4096         4KB 页 (适合小文件)\n");
    printf("  8K, 8192         8KB 页 (适合小到中等文件)\n");
    printf("  16K, 16384       16KB 页 (平衡点)\n");
    printf("  32K, 32768       32KB 页 (适合中等文件)\n");
    printf("  64K, 65536       64KB 页 (默认, 适合大文件)\n");
    printf("  128K, 131072     128KB 页 (适合很大文件)\n");
    printf("  256K, 262144     256KB 页 (适合巨大文件)\n");
    printf("  512K, 524288     512KB 页 (适合超大文件)\n");
    printf("  1M, 1048576      1MB 页 (最大页大小)\n\n");
    
    printf("示例:\n");
    printf("  %s compress test.db test.ccvfs\n", program_name);
    printf("  %s compress -c zlib -e aes128 -l 9 test.db test.ccvfs\n", program_name);
    printf("  %s compress -b 4K test.db test.ccvfs          # 使用4KB页大小\n", program_name);
    printf("  %s compress -b 1M -c zlib test.db test.ccvfs  # 使用1MB页大小\n", program_name);
    printf("  %s decompress test.ccvfs restored.db\n", program_name);
    printf("  %s info test.ccvfs\n", program_name);
    printf("  %s batch-test --batch-enable --batch-records 5000 test.db\n", program_name);
    printf("  %s batch-stats test.db\n", program_name);
    printf("  %s batch-flush test.db\n", program_name);
}

// Parse page size string to bytes
static uint32_t parse_page_size(const char *size_str) {
    if (!size_str) return CCVFS_DEFAULT_PAGE_SIZE;
    
    char *endptr;
    long value = strtol(size_str, &endptr, 10);
    
    if (value <= 0) return 0;
    
    // Handle size suffixes
    if (*endptr) {
        if (strcmp(endptr, "K") == 0 || strcmp(endptr, "k") == 0) {
            value *= 1024;
        } else if (strcmp(endptr, "M") == 0 || strcmp(endptr, "m") == 0) {
            value *= 1024 * 1024;
        } else {
            return 0;  // Invalid suffix
        }
    }
    
    // Validate range
    if (value < CCVFS_MIN_PAGE_SIZE || value > CCVFS_MAX_PAGE_SIZE) {
        return 0;
    }
    
    // Check if power of 2
    if ((value & (value - 1)) != 0) {
        return 0;
    }
    
    return (uint32_t)value;
}

static void print_stats(const CCVFSStats *stats) {
    printf("\n=== 压缩文件信息 ===\n");
    printf("压缩算法: %s\n", stats->compress_algorithm);
    printf("加密算法: %s\n", stats->encrypt_algorithm);
    printf("原始大小: %llu 字节 (%.2f MB)\n", 
           (unsigned long long)stats->original_size,
           (double)stats->original_size / (1024.0 * 1024.0));
    printf("压缩大小: %llu 字节 (%.2f MB)\n", 
           (unsigned long long)stats->compressed_size,
           (double)stats->compressed_size / (1024.0 * 1024.0));
    printf("压缩比: %u%%\n", stats->compression_ratio);
    printf("节省空间: %llu 字节 (%.2f MB)\n", 
           (unsigned long long)(stats->original_size - stats->compressed_size),
           (double)(stats->original_size - stats->compressed_size) / (1024.0 * 1024.0));
    printf("总页数: %u\n", stats->total_pages);
}

int main(int argc, char *argv[]) {
    const char *compress_algo = "zlib";
    const char *encrypt_algo = NULL;  // Default to no encryption
    int compression_level = 6;
    uint32_t page_size = 0;  // Will be auto-detected from source database
    int verbose = 0;
    int rc;
    
    // Batch writer options
    int batch_enable = 0;
    int batch_pages = 100;
    int batch_memory_mb = 16;
    int batch_test_records = 1000;
    
    static struct option long_options[] = {
        {"compress-algo", required_argument, 0, 'c'},
        {"encrypt-algo",  required_argument, 0, 'e'},
        {"level",         required_argument, 0, 'l'},
        {"page-size",    required_argument, 0, 'b'},
        {"batch-enable",  no_argument,       0, 1000},
        {"batch-pages",   required_argument, 0, 1001},
        {"batch-memory",  required_argument, 0, 1002},
        {"batch-records", required_argument, 0, 1003},
        {"verbose",       no_argument,       0, 'v'},
        {"help",          no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "c:e:l:b:vh", long_options, NULL)) != -1) {
        switch (c) {
            case 'c':
                compress_algo = optarg;
                break;
            case 'e':
                encrypt_algo = optarg;
                if (strcmp(encrypt_algo, "none") == 0) {
                    encrypt_algo = NULL;
                }
                break;
            case 'l':
                compression_level = atoi(optarg);
                if (compression_level < 1 || compression_level > 9) {
                    fprintf(stderr, "错误: 压缩等级必须在1-9之间\n");
                    return 1;
                }
                break;
            case 'b':
                page_size = parse_page_size(optarg);
                if (page_size == 0) {
                    fprintf(stderr, "错误: 无效的页大小 '%s'\n", optarg);
                    fprintf(stderr, "支持的格式: 1K, 4K, 8K, 16K, 32K, 64K, 128K, 256K, 512K, 1M\n");
                    fprintf(stderr, "或直接使用字节数: 1024, 4096, 8192, 16384, 32768, 65536, ...\n");
                    return 1;
                }
                break;
            case 1000:  // --batch-enable
                batch_enable = 1;
                break;
            case 1001:  // --batch-pages
                batch_pages = atoi(optarg);
                if (batch_pages <= 0) {
                    fprintf(stderr, "错误: 批量页数必须大于0\n");
                    return 1;
                }
                break;
            case 1002:  // --batch-memory
                batch_memory_mb = atoi(optarg);
                if (batch_memory_mb <= 0) {
                    fprintf(stderr, "错误: 批量内存大小必须大于0MB\n");
                    return 1;
                }
                break;
            case 1003:  // --batch-records
                batch_test_records = atoi(optarg);
                if (batch_test_records <= 0) {
                    fprintf(stderr, "错误: 测试记录数必须大于0\n");
                    return 1;
                }
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case '?':
                fprintf(stderr, "未知选项。使用 -h 查看帮助。\n");
                return 1;
            default:
                break;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "错误: 缺少操作参数\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *operation = argv[optind];
    
    // Validate batch options are only used with batch operations
    if (batch_enable || batch_pages != 100 || batch_memory_mb != 16 || batch_test_records != 1000) {
        if (strcmp(operation, "batch-test") != 0) {
            fprintf(stderr, "错误: 批量写入选项只能用于 batch-test 操作\n");
            fprintf(stderr, "批量写入选项: --batch-enable, --batch-pages, --batch-memory, --batch-records\n");
            return 1;
        }
    }
    
    if (strcmp(operation, "compress") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "错误: compress 操作需要源文件和目标文件参数\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *source_db = argv[optind + 1];
        const char *target_db = argv[optind + 2];
        
        // Auto-detect page size from source database if not specified
        if (page_size == 0) {
            sqlite3 *db = NULL;
            int rc_open = sqlite3_open_v2(source_db, &db, SQLITE_OPEN_READONLY, NULL);
            if (rc_open == SQLITE_OK) {
                sqlite3_stmt *stmt = NULL;
                rc_open = sqlite3_prepare_v2(db, "PRAGMA page_size", -1, &stmt, NULL);
                if (rc_open == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                    page_size = (uint32_t)sqlite3_column_int(stmt, 0);
                    printf("检测到源数据库页大小: %u 字节 (%u KB)\n", page_size, page_size / 1024);
                }
                if (stmt) sqlite3_finalize(stmt);
                sqlite3_close(db);
            }
            
            // Fallback to default if detection failed
            if (page_size == 0) {
                page_size = CCVFS_DEFAULT_PAGE_SIZE;
                printf("无法检测页大小，使用默认值: %u 字节 (%u KB)\n", page_size, page_size / 1024);
            }
        }
        
        if (verbose) {
            printf("压缩参数:\n");
            printf("  源文件: %s\n", source_db);
            printf("  目标文件: %s\n", target_db);
            printf("  压缩算法: %s\n", compress_algo);
            printf("  加密算法: %s\n", encrypt_algo ? encrypt_algo : "无");
            printf("  页大小: %u 字节 (%u KB)\n", page_size, page_size / 1024);
            printf("  压缩等级: %d\n", compression_level);
            printf("\n");
        }
        
        rc = sqlite3_ccvfs_compress_database_with_page_size(source_db, target_db,
                                                             compress_algo, encrypt_algo, 
                                                             page_size, compression_level);
        
        if (rc == SQLITE_OK) {
            printf("\n数据库压缩成功!\n");
            
            // Show statistics
            CCVFSStats stats;
            if (sqlite3_ccvfs_get_stats(target_db, &stats) == SQLITE_OK) {
                print_stats(&stats);
            }
            return 0;
        } else {
            fprintf(stderr, "数据库压缩失败，错误代码: %d\n", rc);
            return 1;
        }
        
    } else if (strcmp(operation, "decompress") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "错误: decompress 操作需要压缩文件和输出文件参数\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *compressed_db = argv[optind + 1];
        const char *output_db = argv[optind + 2];
        
        if (verbose) {
            printf("解压参数:\n");
            printf("  压缩文件: %s\n", compressed_db);
            printf("  输出文件: %s\n", output_db);
            printf("\n");
        }
        
        rc = sqlite3_ccvfs_decompress_database(compressed_db, output_db);
        
        if (rc == SQLITE_OK) {
            printf("\n数据库解压成功!\n");
            return 0;
        } else {
            fprintf(stderr, "数据库解压失败，错误代码: %d\n", rc);
            return 1;
        }
        
    } else if (strcmp(operation, "info") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "错误: info 操作需要压缩文件参数\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *compressed_db = argv[optind + 1];
        CCVFSStats stats;
        
        rc = sqlite3_ccvfs_get_stats(compressed_db, &stats);
        
        if (rc == SQLITE_OK) {
            print_stats(&stats);
            return 0;
        } else {
            fprintf(stderr, "无法读取压缩文件信息，错误代码: %d\n", rc);
            return 1;
        }
        
    } else if (strcmp(operation, "batch-test") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "错误: batch-test 操作需要数据库文件参数\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *db_path = argv[optind + 1];
        return perform_batch_test(db_path, batch_enable, batch_pages, 
                                 batch_memory_mb, batch_test_records, verbose);
        
    } else if (strcmp(operation, "batch-stats") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "错误: batch-stats 操作需要数据库文件参数\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *db_path = argv[optind + 1];
        return show_batch_stats(db_path, verbose);
        
    } else if (strcmp(operation, "batch-flush") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "错误: batch-flush 操作需要数据库文件参数\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *db_path = argv[optind + 1];
        return flush_batch_writer(db_path, verbose);
        
    } else {
        fprintf(stderr, "错误: 未知操作 '%s'\n", operation);
        print_usage(argv[0]);
        return 1;
    }
}

// ============================================================================
// BATCH WRITER FUNCTIONS
// ============================================================================

static int perform_batch_test(const char *db_path, int enable_batch, 
                             int max_pages, int max_memory_mb, 
                             int test_records, int verbose) {
    sqlite3 *db = NULL;
    int rc;
    
    if (verbose) {
        printf("批量写入测试参数:\n");
        printf("  数据库文件: %s\n", db_path);
        printf("  启用批量写入: %s\n", enable_batch ? "是" : "否");
        printf("  最大页数: %d\n", max_pages);
        printf("  最大内存: %d MB\n", max_memory_mb);
        printf("  测试记录数: %d\n", test_records);
        printf("\n");
    }
    
    // Create CCVFS if it doesn't exist
    sqlite3_vfs *pDefaultVfs = sqlite3_vfs_find(NULL);
    rc = sqlite3_ccvfs_create("ccvfs", pDefaultVfs, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK && rc != SQLITE_MISUSE) {  // SQLITE_MISUSE means already exists
        fprintf(stderr, "创建 CCVFS 失败，错误代码: %d\n", rc);
        return 1;
    }
    
    if (verbose && rc == SQLITE_OK) {
        printf("CCVFS 创建成功\n");
    }
    
    // Configure batch writer if enabled
    if (enable_batch) {
        uint32_t auto_flush_threshold = max_pages / 2;  // Auto flush at 50% capacity
        rc = sqlite3_ccvfs_configure_batch_writer("ccvfs", 1, max_pages, max_memory_mb, auto_flush_threshold);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "配置批量写入器失败，错误代码: %d\n", rc);
            return 1;
        }
        if (verbose) {
            printf("批量写入器配置成功 (自动刷新阈值: %u 页)\n", auto_flush_threshold);
        }
    }
    
    // Open database with CCVFS
    rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, enable_batch ? "ccvfs" : NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "打开数据库失败: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return 1;
    }
    
    if (verbose) {
        printf("数据库打开成功，开始批量写入测试...\n");
    }
    
    // Create test table (outside transaction)
    const char *create_sql = 
        "CREATE TABLE IF NOT EXISTS batch_test ("
        "id INTEGER PRIMARY KEY, "
        "data TEXT, "
        "timestamp INTEGER"
        ")";
    
    rc = sqlite3_exec(db, create_sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建测试表失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    if (verbose) {
        printf("测试表创建成功\n");
    }
    
    // Prepare insert statement
    const char *insert_sql = "INSERT INTO batch_test (data, timestamp) VALUES (?, ?)";
    sqlite3_stmt *stmt = NULL;
    
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备插入语句失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    // Start timing
    clock_t start_time = clock();
    
    // Begin transaction
    rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "开始事务失败: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 1;
    }
    
    // Insert test records
    int successful_inserts = 0;
    for (int i = 0; i < test_records; i++) {
        char data_buffer[256];
        snprintf(data_buffer, sizeof(data_buffer), "Test data record %d with some content", i);
        
        sqlite3_bind_text(stmt, 1, data_buffer, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)time(NULL));
        
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            if (verbose) {
                fprintf(stderr, "插入记录 %d 失败: %s\n", i, sqlite3_errmsg(db));
            }
            // Don't break, continue with remaining records
        } else {
            successful_inserts++;
        }
        
        sqlite3_reset(stmt);
        
        if (verbose && (i + 1) % 1000 == 0) {
            printf("已处理 %d 条记录 (成功: %d)...\n", i + 1, successful_inserts);
        }
    }
    
    // Commit transaction
    rc = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "提交事务失败: %s\n", sqlite3_errmsg(db));
    }
    
    // End timing
    clock_t end_time = clock();
    double elapsed_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    printf("\n=== 批量写入测试结果 ===\n");
    printf("尝试插入记录数: %d\n", test_records);
    printf("成功插入记录数: %d\n", successful_inserts);
    printf("耗时: %.2f 秒\n", elapsed_time);
    if (successful_inserts > 0) {
        printf("平均速度: %.0f 记录/秒\n", successful_inserts / elapsed_time);
    }
    
    // Show batch writer statistics if enabled
    if (enable_batch) {
        uint32_t hits, flushes, merges, total_writes, memory_used, page_count;
        rc = sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes, &merges, 
                                                 &total_writes, &memory_used, &page_count);
        if (rc == SQLITE_OK) {
            printf("\n=== 批量写入器统计 ===\n");
            printf("缓存命中: %u\n", hits);
            printf("刷新次数: %u\n", flushes);
            printf("合并次数: %u\n", merges);
            printf("总写入页数: %u\n", total_writes);
            printf("内存使用: %u 字节 (%.2f MB)\n", memory_used, memory_used / (1024.0 * 1024.0));
            printf("缓冲页数: %u\n", page_count);
        }
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    printf("\n批量写入测试完成!\n");
    return 0;
}

static int show_batch_stats(const char *db_path, int verbose) {
    sqlite3 *db = NULL;
    int rc;
    
    // Create CCVFS if it doesn't exist
    sqlite3_vfs *pDefaultVfs = sqlite3_vfs_find(NULL);
    rc = sqlite3_ccvfs_create("ccvfs", pDefaultVfs, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK && rc != SQLITE_MISUSE) {  // SQLITE_MISUSE means already exists
        fprintf(stderr, "创建 CCVFS 失败，错误代码: %d\n", rc);
        return 1;
    }
    
    // Open database with CCVFS
    rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, "ccvfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "打开数据库失败: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return 1;
    }
    
    uint32_t hits, flushes, merges, total_writes, memory_used, page_count;
    rc = sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &flushes, &merges, 
                                             &total_writes, &memory_used, &page_count);
    
    if (rc == SQLITE_OK) {
        printf("\n=== 批量写入器统计信息 ===\n");
        printf("数据库文件: %s\n", db_path);
        printf("缓存命中: %u\n", hits);
        printf("刷新次数: %u\n", flushes);
        printf("合并次数: %u\n", merges);
        printf("总写入页数: %u\n", total_writes);
        printf("内存使用: %u 字节 (%.2f MB)\n", memory_used, memory_used / (1024.0 * 1024.0));
        printf("缓冲页数: %u\n", page_count);
        
        if (verbose) {
            printf("\n=== 详细信息 ===\n");
            printf("平均每次刷新页数: %.1f\n", flushes > 0 ? (double)total_writes / flushes : 0.0);
            printf("缓存命中率: %.1f%%\n", (hits + total_writes) > 0 ? 
                   (double)hits / (hits + total_writes) * 100.0 : 0.0);
        }
    } else {
        fprintf(stderr, "获取批量写入器统计信息失败，错误代码: %d\n", rc);
        sqlite3_close(db);
        return 1;
    }
    
    sqlite3_close(db);
    return 0;
}

static int flush_batch_writer(const char *db_path, int verbose) {
    sqlite3 *db = NULL;
    int rc;
    
    if (verbose) {
        printf("强制刷新批量写入缓冲区: %s\n", db_path);
    }
    
    // Create CCVFS if it doesn't exist
    sqlite3_vfs *pDefaultVfs = sqlite3_vfs_find(NULL);
    rc = sqlite3_ccvfs_create("ccvfs", pDefaultVfs, "zlib", NULL, 0, 0);
    if (rc != SQLITE_OK && rc != SQLITE_MISUSE) {  // SQLITE_MISUSE means already exists
        fprintf(stderr, "创建 CCVFS 失败，错误代码: %d\n", rc);
        return 1;
    }
    
    // Open database with CCVFS
    rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, "ccvfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "打开数据库失败: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return 1;
    }
    
    // Get stats before flush
    uint32_t before_flushes = 0, before_pages = 0;
    if (verbose) {
        uint32_t hits, merges, total_writes, memory_used;
        sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &before_flushes, &merges, 
                                           &total_writes, &memory_used, &before_pages);
        printf("刷新前缓冲页数: %u\n", before_pages);
    }
    
    // Flush batch writer
    rc = sqlite3_ccvfs_flush_batch_writer(db);
    
    if (rc == SQLITE_OK) {
        printf("批量写入缓冲区刷新成功!\n");
        
        if (verbose) {
            uint32_t hits, after_flushes, merges, total_writes, memory_used, after_pages;
            sqlite3_ccvfs_get_batch_writer_stats(db, &hits, &after_flushes, &merges, 
                                               &total_writes, &memory_used, &after_pages);
            printf("刷新后缓冲页数: %u\n", after_pages);
            printf("本次刷新页数: %u\n", before_pages - after_pages);
        }
    } else {
        fprintf(stderr, "刷新批量写入缓冲区失败，错误代码: %d\n", rc);
        sqlite3_close(db);
        return 1;
    }
    
    sqlite3_close(db);
    return 0;
}