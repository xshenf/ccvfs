#include "ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

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

static void print_usage(const char *program_name) {
    printf("SQLite数据库压缩解压工具\n");
    printf("用法: %s [选项] <操作> <文件>\n\n", program_name);
    
    printf("操作:\n");
    printf("  compress <源数据库> <目标文件>    压缩SQLite数据库\n");
    printf("  decompress <压缩文件> <输出文件>  解压数据库到标准SQLite格式\n");
    printf("  info <压缩文件>                   显示压缩文件信息\n\n");
    
    printf("选项:\n");
    printf("  -c, --compress-algo <算法>       压缩算法 (rle, lz4, zlib)\n");
    printf("  -e, --encrypt-algo <算法>        加密算法 (xor, aes128, aes256, chacha20)\n");
    printf("  -l, --level <等级>               压缩等级 (1-9, 默认: 6)\n");
    printf("  -b, --page-size <大小>          页大小 (1K, 4K, 8K, 16K, 32K, 64K, 128K, 256K, 512K, 1M, 默认: 64K)\n");
    printf("  -h, --help                       显示帮助信息\n");
    printf("  -v, --verbose                    详细输出\n\n");
    
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
    uint32_t page_size = CCVFS_DEFAULT_PAGE_SIZE;  // Default to 64KB
    int verbose = 0;
    int rc;
    
    static struct option long_options[] = {
        {"compress-algo", required_argument, 0, 'c'},
        {"encrypt-algo",  required_argument, 0, 'e'},
        {"level",         required_argument, 0, 'l'},
        {"page-size",    required_argument, 0, 'b'},
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
    
    if (strcmp(operation, "compress") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "错误: compress 操作需要源文件和目标文件参数\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *source_db = argv[optind + 1];
        const char *target_db = argv[optind + 2];
        
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
        
    } else {
        fprintf(stderr, "错误: 未知操作 '%s'\n", operation);
        print_usage(argv[0]);
        return 1;
    }
}