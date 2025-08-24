#include "ccvfs.h"
#include "sqlite3.h"
#include "db_generator.h"
#include "db_compare.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <sys/stat.h>

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

// Encryption/Decryption functions
static int perform_encrypt_database(const char *source_db, const char *encrypted_db, 
                                  const char *encrypt_algo, const char *key_hex, int verbose);
static int perform_decrypt_database(const char *encrypted_db, const char *output_db, 
                                  const char *key_hex, int verbose);
static int perform_compress_encrypt_database(const char *source_db, const char *target_db, 
                                           const char *compress_algo, const char *encrypt_algo,
                                           const char *key_hex, uint32_t page_size, 
                                           int compression_level, int verbose);
static int perform_decrypt_decompress_database(const char *encrypted_file, const char *output_db,
                                              const char *key_hex, int verbose);

// Helper functions
static int parse_hex_key(const char *hex_str, unsigned char *key, int max_len);
static void print_hex_key(const unsigned char *key, int len);

// Batch writer test functions
static int perform_batch_test(const char *db_path, int enable_batch, 
                             int max_pages, int max_memory_mb, 
                             int test_records, int verbose);

// Database generator functions
static int generate_database(int argc, char *argv[], int verbose,
                           int gen_compress, const char *gen_encrypt_algo,
                           const char *gen_mode, int gen_table_count, int gen_wal_mode);

static int perform_database_compare(const char *db1_path, const char *db2_path, int verbose,
                                   int schema_only, int ignore_case, int ignore_whitespace,
                                   const char *ignore_tables);

static void print_usage(const char *program_name) {
    printf("SQLite数据库压缩解压工具\n");
    printf("用法: %s [选项] <操作> <文件>\n\n", program_name);

    printf("操作:\n");
    printf("  compress <源数据库> <目标文件>    压缩SQLite数据库\n");
    printf("  decompress <压缩文件> <输出文件>  解压数据库到标准SQLite格式\n");
    printf("  encrypt <源数据库> <加密文件>    加密SQLite数据库\n");
    printf("  decrypt <加密文件> <输出文件>    解密数据库到标准SQLite格式\n");
    printf("  compress-encrypt <源数据库> <目标文件>  压缩并加密SQLite数据库\n");
    printf("  decrypt-decompress <加密文件> <输出文件>  解密并解压SQLite数据库\n");
    printf("  info <压缩文件>                   显示压缩文件信息\n");
    printf("  generate <输出文件> <大小>        生成指定大小的测试数据库\n");
    printf("  compare <数据库1> <数据库2>       比较两个数据库\n");
    printf("  batch-test <数据库文件>           测试批量写入功能\n");
    printf("  batch-stats <数据库文件>          显示批量写入统计信息\n");
    printf("  batch-flush <数据库文件>          强制刷新批量写入缓冲区\n\n");

    printf("通用选项:\n");
    printf("  -h, --help                       显示帮助信息\n");
    printf("  -v, --verbose                    详细输出\n");
    printf("  -k, --key <密钥>                  加密密钥（十六进制格式）\n\n");

    printf("压缩/解压选项:\n");
    printf("  -c, --compress-algo <算法>       压缩算法 (rle, lz4, zlib)\n");
    printf("  -e, --encrypt-algo <算法>        加密算法 (xor, aes128, aes256, chacha20)\n");
    printf("  -l, --level <等级>               压缩等级 (1-9, 默认: 6)\n");
    printf("  -b, --page-size <大小>          页大小 (1K, 4K, 8K, 16K, 32K, 64K, 128K, 256K, 512K, 1M, 默认: 64K)\n\n");

    printf("数据库生成选项 (仅用于 generate):\n");
    printf("  -C, --compress                   启用压缩\n");
    printf("  -E, --encrypt <算法>             加密算法 (xor, aes128, aes256, chacha20)\n");
    printf("  --mode <模式>                    数据生成模式 (random, sequential, lorem, binary, mixed)\n");
    printf("  --tables <数量>                  创建表的数量 (默认: 1)\n");
    printf("  --no-wal                         禁用WAL模式\n\n");

    printf("数据库比较选项 (仅用于 compare):\n");
    printf("  -s, --schema-only                只比较表结构，不比较数据\n");
    printf("  -i, --ignore-case                忽略字符串比较中的大小写差异\n");
    printf("  -w, --ignore-whitespace          忽略空白字符差异\n");
    printf("  -t, --ignore-tables <表名>       忽略指定的表（逗号分隔）\n\n");

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
    printf("  %s encrypt -e aes256 -k 0123456789ABCDEF test.db encrypted.db\n", program_name);
    printf("  %s decrypt -k 0123456789ABCDEF encrypted.db decrypted.db\n", program_name);
    printf("  %s compress-encrypt -c zlib -e aes256 -k 0123456789ABCDEF test.db secure.ccvfs\n", program_name);
    printf("  %s decrypt-decompress -k 0123456789ABCDEF secure.ccvfs restored.db\n", program_name);
    printf("  %s info test.ccvfs\n", program_name);
    printf("  %s generate test.db 100MB                     # 生成100MB测试数据库\n", program_name);
    printf("  %s generate -C -E aes128 test.ccvfs 500MB    # 生成500MB压缩加密数据库\n", program_name);
    printf("  %s compare db1.db db2.db                      # 比较两个数据库\n", program_name);
    printf("  %s compare -s db1.db db2.db                   # 只比较表结构\n", program_name);
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
            return 0; // Invalid suffix
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

    return (uint32_t) value;
}

static void print_stats(const CCVFSStats *stats) {
    printf("\n=== 压缩文件信息 ===\n");
    printf("压缩算法: %s\n", stats->compress_algorithm);
    printf("加密算法: %s\n", stats->encrypt_algorithm);
    printf("原始大小: %llu 字节 (%.2f MB)\n",
           (unsigned long long) stats->original_size,
           (double) stats->original_size / (1024.0 * 1024.0));
    printf("压缩大小: %llu 字节 (%.2f MB)\n",
           (unsigned long long) stats->compressed_size,
           (double) stats->compressed_size / (1024.0 * 1024.0));
    printf("压缩比: %u%%\n", stats->compression_ratio);
    printf("节省空间: %llu 字节 (%.2f MB)\n",
           (unsigned long long) (stats->original_size - stats->compressed_size),
           (double) (stats->original_size - stats->compressed_size) / (1024.0 * 1024.0));
    printf("总页数: %u\n", stats->total_pages);
}

int main(int argc, char *argv[]) {
    const char *compress_algo = "zlib";
    const char *encrypt_algo = NULL; // Default to no encryption
    const char *key_hex = NULL; // Encryption key in hex format
    int compression_level = 6;
    uint32_t page_size = 0; // Will be auto-detected from source database
    int verbose = 0;
    int rc;

    // Batch writer options
    int batch_enable = 0;
    int batch_pages = 100;
    int batch_memory_mb = 16;
    int batch_test_records = 1000;

    // Generator options
    int gen_compress = 0;
    const char *gen_encrypt_algo = NULL;
    const char *gen_mode = "random";
    int gen_table_count = 1;
    int gen_wal_mode = 1;

    static struct option long_options[] = {
        {"compress-algo", required_argument, 0, 'c'},
        {"encrypt-algo", required_argument, 0, 'e'},
        {"key", required_argument, 0, 'k'},
        {"level", required_argument, 0, 'l'},
        {"page-size", required_argument, 0, 'b'},
        {"batch-enable", no_argument, 0, 1000},
        {"batch-pages", required_argument, 0, 1001},
        {"batch-memory", required_argument, 0, 1002},
        {"batch-records", required_argument, 0, 1003},
        {"compress", no_argument, 0, 'C'},
        {"encrypt", required_argument, 0, 'E'},
        {"mode", required_argument, 0, 1004},
        {"tables", required_argument, 0, 1005},
        {"no-wal", no_argument, 0, 1006},
        {"schema-only", no_argument, 0, 's'},
        {"ignore-case", no_argument, 0, 'i'},
        {"ignore-whitespace", no_argument, 0, 'w'},
        {"ignore-tables", required_argument, 0, 't'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "c:e:k:l:b:CE:siwt:vh", long_options, NULL)) != -1) {
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
            case 'k':
                key_hex = optarg;
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
            case 1000: // --batch-enable
                batch_enable = 1;
                break;
            case 1001: // --batch-pages
                batch_pages = atoi(optarg);
                if (batch_pages <= 0) {
                    fprintf(stderr, "错误: 批量页数必须大于0\n");
                    return 1;
                }
                break;
            case 1002: // --batch-memory
                batch_memory_mb = atoi(optarg);
                if (batch_memory_mb <= 0) {
                    fprintf(stderr, "错误: 批量内存大小必须大于0MB\n");
                    return 1;
                }
                break;
            case 1003: // --batch-records
                batch_test_records = atoi(optarg);
                if (batch_test_records <= 0) {
                    fprintf(stderr, "错误: 测试记录数必须大于0\n");
                    return 1;
                }
                break;
            case 'C': // --compress for generate
                gen_compress = 1;
                break;
            case 'E': // --encrypt for generate
                gen_encrypt_algo = optarg;
                if (strcmp(gen_encrypt_algo, "none") == 0) {
                    gen_encrypt_algo = NULL;
                }
                break;
            case 1004: // --mode
                gen_mode = optarg;
                break;
            case 1005: // --tables
                gen_table_count = atoi(optarg);
                if (gen_table_count < 1 || gen_table_count > 100) {
                    fprintf(stderr, "错误: 表数量必须在1-100之间\n");
                    return 1;
                }
                break;
            case 1006: // --no-wal
                gen_wal_mode = 0;
                break;
            case 's': // --schema-only for compare
                // Will be handled in compare operation
                break;
            case 'i': // --ignore-case for compare
                // Will be handled in compare operation
                break;
            case 'w': // --ignore-whitespace for compare
                // Will be handled in compare operation
                break;
            case 't': // --ignore-tables for compare
                // Will be handled in compare operation
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

    // Validate generator options are only used with generate operation
    if (gen_compress || gen_encrypt_algo || strcmp(gen_mode, "random") != 0 || gen_table_count != 1 || gen_wal_mode !=
        1) {
        if (strcmp(operation, "generate") != 0) {
            fprintf(stderr, "错误: 数据库生成选项只能用于 generate 操作\n");
            fprintf(stderr, "数据库生成选项: -C, -E, --mode, --tables, --no-wal\n");
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

        // Set encryption key if provided
        if (key_hex && encrypt_algo) {
            unsigned char key[64];
            int key_len = parse_hex_key(key_hex, key, sizeof(key));
            if (key_len <= 0) {
                fprintf(stderr, "错误: 无效的密钥格式\n");
                return 1;
            }
            ccvfs_set_encryption_key(key, key_len);
            if (verbose) {
                printf("已设置加密密钥: ");
                print_hex_key(key, key_len);
                printf("\n");
            }
        }

        // Auto-detect page size from source database if not specified
        if (page_size == 0) {
            sqlite3 *db = NULL;
            int rc_open = sqlite3_open_v2(source_db, &db, SQLITE_OPEN_READONLY, NULL);
            if (rc_open == SQLITE_OK) {
                sqlite3_stmt *stmt = NULL;
                rc_open = sqlite3_prepare_v2(db, "PRAGMA page_size", -1, &stmt, NULL);
                if (rc_open == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                    page_size = (uint32_t) sqlite3_column_int(stmt, 0);
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

        // Set decryption key if provided
        if (key_hex) {
            unsigned char key[64];
            int key_len = parse_hex_key(key_hex, key, sizeof(key));
            if (key_len <= 0) {
                fprintf(stderr, "错误: 无效的密钥格式\n");
                return 1;
            }
            ccvfs_set_encryption_key(key, key_len);
            if (verbose) {
                printf("已设置解密密钥: ");
                print_hex_key(key, key_len);
                printf("\n");
            }
        }

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
    } else if (strcmp(operation, "encrypt") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "错误: encrypt 操作需要源文件和加密文件参数\n");
            print_usage(argv[0]);
            return 1;
        }

        const char *source_db = argv[optind + 1];
        const char *encrypted_db = argv[optind + 2];

        if (!encrypt_algo) {
            fprintf(stderr, "错误: encrypt 操作需要指定加密算法 (-e 参数)\n");
            return 1;
        }

        if (!key_hex) {
            fprintf(stderr, "错误: encrypt 操作需要指定密钥 (-k 参数)\n");
            return 1;
        }

        return perform_encrypt_database(source_db, encrypted_db, encrypt_algo, key_hex, verbose);
    } else if (strcmp(operation, "decrypt") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "错误: decrypt 操作需要加密文件和输出文件参数\n");
            print_usage(argv[0]);
            return 1;
        }

        const char *encrypted_db = argv[optind + 1];
        const char *output_db = argv[optind + 2];

        if (!key_hex) {
            fprintf(stderr, "错误: decrypt 操作需要指定密钥 (-k 参数)\n");
            return 1;
        }

        return perform_decrypt_database(encrypted_db, output_db, key_hex, verbose);
    } else if (strcmp(operation, "compress-encrypt") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "错误: compress-encrypt 操作需要源文件和目标文件参数\n");
            print_usage(argv[0]);
            return 1;
        }

        const char *source_db = argv[optind + 1];
        const char *target_db = argv[optind + 2];

        if (!encrypt_algo) {
            fprintf(stderr, "错误: compress-encrypt 操作需要指定加密算法 (-e 参数)\n");
            return 1;
        }

        if (!key_hex) {
            fprintf(stderr, "错误: compress-encrypt 操作需要指定密钥 (-k 参数)\n");
            return 1;
        }

        // Auto-detect page size if not specified
        if (page_size == 0) {
            sqlite3 *db = NULL;
            int rc_open = sqlite3_open_v2(source_db, &db, SQLITE_OPEN_READONLY, NULL);
            if (rc_open == SQLITE_OK) {
                sqlite3_stmt *stmt = NULL;
                rc_open = sqlite3_prepare_v2(db, "PRAGMA page_size", -1, &stmt, NULL);
                if (rc_open == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                    page_size = (uint32_t) sqlite3_column_int(stmt, 0);
                }
                if (stmt) sqlite3_finalize(stmt);
                sqlite3_close(db);
            }
            if (page_size == 0) {
                page_size = CCVFS_DEFAULT_PAGE_SIZE;
            }
        }

        return perform_compress_encrypt_database(source_db, target_db, compress_algo, 
                                                encrypt_algo, key_hex, page_size, 
                                                compression_level, verbose);
    } else if (strcmp(operation, "decrypt-decompress") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "错误: decrypt-decompress 操作需要加密文件和输出文件参数\n");
            print_usage(argv[0]);
            return 1;
        }

        const char *encrypted_file = argv[optind + 1];
        const char *output_db = argv[optind + 2];

        if (!key_hex) {
            fprintf(stderr, "错误: decrypt-decompress 操作需要指定密钥 (-k 参数)\n");
            return 1;
        }

        return perform_decrypt_decompress_database(encrypted_file, output_db, key_hex, verbose);
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
    } else if (strcmp(operation, "generate") == 0) {
        return generate_database(argc - optind, &argv[optind], verbose,
                               gen_compress, gen_encrypt_algo, gen_mode,
                               gen_table_count, gen_wal_mode);
    } else if (strcmp(operation, "compare") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "错误: compare 操作需要两个数据库文件参数\n");
            print_usage(argv[0]);
            return 1;
        }

        const char *db1_path = argv[optind + 1];
        const char *db2_path = argv[optind + 2];

        // Parse compare-specific options
        int schema_only = 0;
        int ignore_case = 0;
        int ignore_whitespace = 0;
        const char *ignore_tables = NULL;

        // Reset getopt parsing to get compare-specific options
        optind = 1;
        
        // Create local variables for parsing
        int local_argc = argc;
        char **local_argv = argv;
        
        // Temporarily modify argc/argv for re-parsing
        int c;
        while ((c = getopt_long(local_argc, local_argv, "c:e:l:b:CE:siwt:vh", long_options, NULL)) != -1) {
            switch (c) {
                case 's':
                    schema_only = 1;
                    break;
                case 'i':
                    ignore_case = 1;
                    break;
                case 'w':
                    ignore_whitespace = 1;
                    break;
                case 't':
                    ignore_tables = optarg;
                    break;
            }
        }

        return perform_database_compare(db1_path, db2_path, verbose, 
                                       schema_only, ignore_case, ignore_whitespace, ignore_tables);
    } else {
        fprintf(stderr, "错误: 未知操作 '%s'\n", operation);
        print_usage(argv[0]);
        return 1;
    }
}

// ============================================================================
// DATABASE GENERATOR FUNCTIONS
// ============================================================================

static int generate_database(int argc, char *argv[], int verbose, int gen_compress, const char *gen_encrypt_algo,
                           const char *gen_mode, int gen_table_count, int gen_wal_mode) {
    if (argc < 3) {
        fprintf(stderr, "错误: generate 操作需要输出文件和大小参数\n");
        return 1;
    }

    GeneratorConfig config;
    sqlite3_ccvfs_init_generator_config(&config);

    config.output_file = argv[1];
    config.target_size = sqlite3_ccvfs_parse_size_string(argv[2]);

    if (config.target_size <= 0) {
        fprintf(stderr, "错误: 无效的目标大小 '%s'\n", argv[2]);
        return 1;
    }

    config.use_compression = gen_compress;
    config.encrypt_algorithm = gen_encrypt_algo;
    config.table_count = gen_table_count;
    config.use_wal_mode = gen_wal_mode;
    config.verbose = verbose;

    // Set data mode
    if (strcmp(gen_mode, "random") == 0) {
        config.data_mode = DATA_MODE_RANDOM;
    } else if (strcmp(gen_mode, "sequential") == 0) {
        config.data_mode = DATA_MODE_SEQUENTIAL;
    } else if (strcmp(gen_mode, "lorem") == 0) {
        config.data_mode = DATA_MODE_LOREM;
    } else if (strcmp(gen_mode, "binary") == 0) {
        config.data_mode = DATA_MODE_BINARY;
    } else if (strcmp(gen_mode, "mixed") == 0) {
        config.data_mode = DATA_MODE_MIXED;
    } else {
        fprintf(stderr, "错误: 无效的数据模式 '%s'\n", gen_mode);
        return 1;
    }

    if (verbose) {
        printf("数据库生成参数:\n");
        printf("  输出文件: %s\n", config.output_file);
        printf("  目标大小: %.2f MB\n", config.target_size / (1024.0 * 1024.0));
        printf("  压缩: %s\n", config.use_compression ? "是" : "否");
        printf("  加密算法: %s\n", config.encrypt_algorithm ? config.encrypt_algorithm : "无");
        printf("  数据模式: %s\n", gen_mode);
        printf("  表数量: %d\n", config.table_count);
        printf("  WAL模式: %s\n", config.use_wal_mode ? "启用" : "禁用");
        printf("\n");
    }

    int rc = sqlite3_ccvfs_generate_database(&config);
    if (rc == SQLITE_OK) {
        printf("\n数据库生成成功: %s\n", config.output_file);
        return 0;
    } else {
        fprintf(stderr, "数据库生成失败，错误代码: %d\n", rc);
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
#ifdef HAVE_ZLIB
    rc = sqlite3_ccvfs_create("ccvfs", pDefaultVfs, CCVFS_COMPRESS_ZLIB, NULL, 0, 0);
#else
    rc = sqlite3_ccvfs_create("ccvfs", pDefaultVfs, NULL, NULL, 0, 0);
#endif
    if (rc != SQLITE_OK && rc != SQLITE_MISUSE) {
        // SQLITE_MISUSE means already exists
        fprintf(stderr, "创建 CCVFS 失败，错误代码: %d\n", rc);
        return 1;
    }

    if (verbose && rc == SQLITE_OK) {
        printf("CCVFS 创建成功\n");
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
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64) time(NULL));

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
    double elapsed_time = ((double) (end_time - start_time)) / CLOCKS_PER_SEC;

    printf("\n=== 批量写入测试结果 ===\n");
    printf("尝试插入记录数: %d\n", test_records);
    printf("成功插入记录数: %d\n", successful_inserts);
    printf("耗时: %.2f 秒\n", elapsed_time);
    if (successful_inserts > 0) {
        printf("平均速度: %.0f 记录/秒\n", successful_inserts / elapsed_time);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    printf("\n批量写入测试完成!\n");
    return 0;
}

static int perform_database_compare(const char *db1_path, const char *db2_path, int verbose,
                                   int schema_only, int ignore_case, int ignore_whitespace, 
                                   const char *ignore_tables) {
    // Setup compare options
    CompareOptions options = {0};
    options.compare_schema_only = schema_only;
    options.ignore_case = ignore_case;
    options.ignore_whitespace = ignore_whitespace;
    options.verbose = verbose;
    options.ignore_tables = ignore_tables;

    // Initialize SQLite and CCVFS
    sqlite3_initialize();
    
    // Register CCVFS for handling compressed databases
#ifdef HAVE_ZLIB
    int rc = sqlite3_ccvfs_create("ccvfs", NULL, CCVFS_COMPRESS_ZLIB, NULL, 0, 0);
#else
    int rc = sqlite3_ccvfs_create("ccvfs", NULL, NULL, NULL, 0, 0);
#endif
    if (rc != SQLITE_OK && verbose) {
        printf("Note: CCVFS registration failed (code %d) - compressed databases may not work\n", rc);
    }

    if (verbose) {
        printf("数据库比较参数:\n");
        printf("  数据库1: %s\n", db1_path);
        printf("  数据库2: %s\n", db2_path);
        printf("  仅比较结构: %s\n", options.compare_schema_only ? "是" : "否");
        printf("  忽略大小写: %s\n", options.ignore_case ? "是" : "否");
        if (options.ignore_tables) {
            printf("  忽略表: %s\n", options.ignore_tables);
        }
        printf("\n");
    }

    CompareResult result;
    rc = compare_databases(db1_path, db2_path, &options, &result);

    if (rc == SQLITE_OK) {
        print_compare_results(&result, &options);
        // Return non-zero if databases are different
        return (result.tables_different + result.records_different + result.schema_differences) > 0 ? 2 : 0;
    } else {
        fprintf(stderr, "数据库比较失败，错误代码: %d\n", rc);
        return 1;
    }
}

// ============================================================================
// ENCRYPTION/DECRYPTION FUNCTIONS
// ============================================================================

// Parse hexadecimal key string to binary
static int parse_hex_key(const char *hex_str, unsigned char *key, int max_len) {
    if (!hex_str || !key) return -1;
    
    int hex_len = strlen(hex_str);
    if (hex_len % 2 != 0) {
        fprintf(stderr, "错误: 密钥必须是偶数个十六进制字符\n");
        return -1;
    }
    
    int key_len = hex_len / 2;
    if (key_len > max_len) {
        fprintf(stderr, "错误: 密钥太长，最大允许 %d 字节\n", max_len);
        return -1;
    }
    
    for (int i = 0; i < key_len; i++) {
        unsigned int byte;
        if (sscanf(hex_str + i * 2, "%2x", &byte) != 1) {
            fprintf(stderr, "错误: 无效的十六进制字符 '%c%c'\n", 
                    hex_str[i * 2], hex_str[i * 2 + 1]);
            return -1;
        }
        key[i] = (unsigned char) byte;
    }
    
    return key_len;
}

// Print key in hexadecimal format
static void print_hex_key(const unsigned char *key, int len) {
    for (int i = 0; i < len; i++) {
        printf("%02X", key[i]);
    }
}

static int perform_encrypt_database(const char *source_db, const char *encrypted_db, 
                                  const char *encrypt_algo, const char *key_hex, int verbose) {
    unsigned char key[64];
    int key_len = parse_hex_key(key_hex, key, sizeof(key));
    if (key_len <= 0) {
        fprintf(stderr, "错误: 无效的密钥格式\n");
        return 1;
    }
    
    // Set encryption key globally
    ccvfs_set_encryption_key(key, key_len);
    
    // Auto-detect page size from source database
    uint32_t page_size = 0;
    sqlite3 *db = NULL;
    int rc_open = sqlite3_open_v2(source_db, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc_open == SQLITE_OK) {
        sqlite3_stmt *stmt = NULL;
        rc_open = sqlite3_prepare_v2(db, "PRAGMA page_size", -1, &stmt, NULL);
        if (rc_open == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            page_size = (uint32_t) sqlite3_column_int(stmt, 0);
        }
        if (stmt) sqlite3_finalize(stmt);
        sqlite3_close(db);
    }
    
    // Fallback to default if detection failed
    if (page_size == 0) {
        page_size = CCVFS_DEFAULT_PAGE_SIZE;
    }
    
    if (verbose) {
        printf("加密参数:\n");
        printf("  源文件: %s\n", source_db);
        printf("  加密文件: %s\n", encrypted_db);
        printf("  加密算法: %s\n", encrypt_algo);
        printf("  页大小: %u 字节 (%u KB)\n", page_size, page_size / 1024);
        printf("  密钥长度: %d 字节\n", key_len);
        printf("  密钥: ");
        print_hex_key(key, key_len);
        printf("\n\n");
    }
    
    // Use compress function with no compression but with encryption
    int rc = sqlite3_ccvfs_compress_database_with_page_size(
        source_db, encrypted_db, NULL, encrypt_algo, page_size, 0);
    
    if (rc == SQLITE_OK) {
        printf("\n数据库加密成功!\n");
        
        // Show statistics
        CCVFSStats stats;
        if (sqlite3_ccvfs_get_stats(encrypted_db, &stats) == SQLITE_OK) {
            print_stats(&stats);
        }
        return 0;
    } else {
        fprintf(stderr, "数据库加密失败，错误代码: %d\n", rc);
        return 1;
    }
}

static int perform_decrypt_database(const char *encrypted_db, const char *output_db, 
                                  const char *key_hex, int verbose) {
    unsigned char key[64];
    int key_len = parse_hex_key(key_hex, key, sizeof(key));
    if (key_len <= 0) {
        fprintf(stderr, "错误: 无效的密钥格式\n");
        return 1;
    }
    
    // Set decryption key globally
    ccvfs_set_encryption_key(key, key_len);
    
    if (verbose) {
        printf("解密参数:\n");
        printf("  加密文件: %s\n", encrypted_db);
        printf("  输出文件: %s\n", output_db);
        printf("  密钥长度: %d 字节\n", key_len);
        printf("  密钥: ");
        print_hex_key(key, key_len);
        printf("\n\n");
    }
    
    // Use decompress function which will handle decryption
    int rc = sqlite3_ccvfs_decompress_database(encrypted_db, output_db);
    
    if (rc == SQLITE_OK) {
        printf("\n数据库解密成功!\n");
        return 0;
    } else {
        fprintf(stderr, "数据库解密失败，错误代码: %d\n", rc);
        return 1;
    }
}

static int perform_compress_encrypt_database(const char *source_db, const char *target_db, 
                                           const char *compress_algo, const char *encrypt_algo,
                                           const char *key_hex, uint32_t page_size, 
                                           int compression_level, int verbose) {
    unsigned char key[64];
    int key_len = parse_hex_key(key_hex, key, sizeof(key));
    if (key_len <= 0) {
        fprintf(stderr, "错误: 无效的密钥格式\n");
        return 1;
    }
    
    // Set encryption key globally
    ccvfs_set_encryption_key(key, key_len);
    
    if (verbose) {
        printf("压缩加密参数:\n");
        printf("  源文件: %s\n", source_db);
        printf("  目标文件: %s\n", target_db);
        printf("  压缩算法: %s\n", compress_algo);
        printf("  加密算法: %s\n", encrypt_algo);
        printf("  页大小: %u 字节 (%u KB)\n", page_size, page_size / 1024);
        printf("  压缩等级: %d\n", compression_level);
        printf("  密钥长度: %d 字节\n", key_len);
        printf("  密钥: ");
        print_hex_key(key, key_len);
        printf("\n\n");
    }
    
    // Perform compression and encryption together
    int rc = sqlite3_ccvfs_compress_database_with_page_size(
        source_db, target_db, compress_algo, encrypt_algo, 
        page_size, compression_level);
    
    if (rc == SQLITE_OK) {
        printf("\n数据库压缩加密成功!\n");
        
        // Show statistics
        CCVFSStats stats;
        if (sqlite3_ccvfs_get_stats(target_db, &stats) == SQLITE_OK) {
            print_stats(&stats);
        }
        return 0;
    } else {
        fprintf(stderr, "数据库压缩加密失败，错误代码: %d\n", rc);
        return 1;
    }
}

static int perform_decrypt_decompress_database(const char *encrypted_file, const char *output_db,
                                              const char *key_hex, int verbose) {
    unsigned char key[64];
    int key_len = parse_hex_key(key_hex, key, sizeof(key));
    if (key_len <= 0) {
        fprintf(stderr, "错误: 无效的密钥格式\n");
        return 1;
    }
    
    // Set decryption key globally
    ccvfs_set_encryption_key(key, key_len);
    
    if (verbose) {
        printf("解密解压参数:\n");
        printf("  加密文件: %s\n", encrypted_file);
        printf("  输出文件: %s\n", output_db);
        printf("  密钥长度: %d 字节\n", key_len);
        printf("  密钥: ");
        print_hex_key(key, key_len);
        printf("\n\n");
    }
    
    // Use decompress function which will handle both decryption and decompression
    int rc = sqlite3_ccvfs_decompress_database(encrypted_file, output_db);
    
    if (rc == SQLITE_OK) {
        printf("\n数据库解密解压成功!\n");
        return 0;
    } else {
        fprintf(stderr, "数据库解密解压失败，错误代码: %d\n", rc);
        return 1;
    }
}
