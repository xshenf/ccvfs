#include "compress_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

// Function declarations from db_compress_tool.c
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
    printf("  -h, --help                       显示帮助信息\n");
    printf("  -v, --verbose                    详细输出\n\n");
    
    printf("示例:\n");
    printf("  %s compress test.db test.ccvfs\n", program_name);
    printf("  %s compress -c zlib -e aes128 -l 9 test.db test.ccvfs\n", program_name);
    printf("  %s decompress test.ccvfs restored.db\n", program_name);
    printf("  %s info test.ccvfs\n", program_name);
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
    printf("总块数: %u\n", stats->total_blocks);
}

int main(int argc, char *argv[]) {
    const char *compress_algo = "zlib";
    const char *encrypt_algo = NULL;  // Default to no encryption
    int compression_level = 6;
    int verbose = 0;
    int rc;
    
    static struct option long_options[] = {
        {"compress-algo", required_argument, 0, 'c'},
        {"encrypt-algo",  required_argument, 0, 'e'},
        {"level",         required_argument, 0, 'l'},
        {"verbose",       no_argument,       0, 'v'},
        {"help",          no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "c:e:l:vh", long_options, NULL)) != -1) {
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
            printf("  压缩等级: %d\n", compression_level);
            printf("\n");
        }
        
        rc = sqlite3_ccvfs_compress_database(source_db, target_db, 
                                             compress_algo, encrypt_algo, 
                                             compression_level);
        
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