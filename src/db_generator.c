
#include "compress_vfs.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <getopt.h>

/*
 * Database generator tool - Create databases of arbitrary size
 * Supports both compressed and uncompressed database generation
 */

// Data generation modes
typedef enum {
    DATA_MODE_RANDOM,      // Random text data
    DATA_MODE_SEQUENTIAL,  // Sequential numbers and text
    DATA_MODE_LOREM,       // Lorem ipsum text
    DATA_MODE_BINARY,      // Binary blob data
    DATA_MODE_MIXED        // Mixed data types
} DataMode;

// Configuration structure
typedef struct {
    char *output_file;
    long target_size;           // Target file size in bytes
    int use_compression;        // Use CCVFS compression
    char *compress_algorithm;   // Compression algorithm
    char *encrypt_algorithm;    // Encryption algorithm
    uint32_t block_size;        // Block size for compression
    int compression_level;      // Compression level (1-9)
    DataMode data_mode;         // Data generation mode
    int record_size;            // Average record size
    int table_count;            // Number of tables to create
    int verbose;                // Verbose output
    int batch_size;             // Records per transaction
} GeneratorConfig;

// Get file size
static long get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// Generate random string
static void generate_random_string(char *buffer, int length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ";
    int charset_size = sizeof(charset) - 1;
    
    for (int i = 0; i < length - 1; i++) {
        buffer[i] = charset[rand() % charset_size];
    }
    buffer[length - 1] = '\0';
}

// Generate Lorem ipsum text
static void generate_lorem_text(char *buffer, int length) {
    const char *lorem_words[] = {
        "Lorem", "ipsum", "dolor", "sit", "amet", "consectetur", "adipiscing", "elit",
        "sed", "do", "eiusmod", "tempor", "incididunt", "ut", "labore", "et", "dolore",
        "magna", "aliqua", "Ut", "enim", "ad", "minim", "veniam", "quis", "nostrud",
        "exercitation", "ullamco", "laboris", "nisi", "ut", "aliquip", "ex", "ea",
        "commodo", "consequat", "Duis", "aute", "irure", "dolor", "in", "reprehenderit",
        "voluptate", "velit", "esse", "cillum", "fugiat", "nulla", "pariatur"
    };
    int word_count = sizeof(lorem_words) / sizeof(lorem_words[0]);
    
    int pos = 0;
    while (pos < length - 20) {  // Leave space for word and null terminator
        const char *word = lorem_words[rand() % word_count];
        int word_len = strlen(word);
        
        if (pos + word_len + 1 < length) {
            strcpy(buffer + pos, word);
            pos += word_len;
            if (pos < length - 1) {
                buffer[pos++] = ' ';
            }
        } else {
            break;
        }
    }
    buffer[pos] = '\0';
}

// Generate data based on mode
static void generate_data(char *buffer, int length, DataMode mode, int record_id) {
    switch (mode) {
        case DATA_MODE_RANDOM:
            generate_random_string(buffer, length);
            break;
            
        case DATA_MODE_SEQUENTIAL:
            snprintf(buffer, length, "Record_%d_Data_%08d", record_id, record_id * 123456);
            break;
            
        case DATA_MODE_LOREM:
            generate_lorem_text(buffer, length);
            break;
            
        case DATA_MODE_BINARY:
            for (int i = 0; i < length - 1; i++) {
                buffer[i] = (char)(rand() % 256);
            }
            buffer[length - 1] = '\0';
            break;
            
        case DATA_MODE_MIXED:
            if (record_id % 4 == 0) {
                generate_random_string(buffer, length);
            } else if (record_id % 4 == 1) {
                generate_lorem_text(buffer, length);
            } else if (record_id % 4 == 2) {
                snprintf(buffer, length, "Mixed_Record_%d_Time_%ld", record_id, time(NULL));
            } else {
                for (int i = 0; i < length - 1; i++) {
                    buffer[i] = (char)(32 + (rand() % 95));  // Printable ASCII only
                }
                buffer[length - 1] = '\0';
            }
            break;
    }
}

// Create database tables
static int create_tables(sqlite3 *db, GeneratorConfig *config) {
    char sql[1024];
    
    for (int table_id = 0; table_id < config->table_count; table_id++) {
        // Create table with various column types
        snprintf(sql, sizeof(sql),
            "CREATE TABLE IF NOT EXISTS data_table_%d ("
            "id INTEGER PRIMARY KEY, "
            "text_data TEXT, "
            "blob_data BLOB, "
            "numeric_data REAL, "
            "timestamp_data DATETIME DEFAULT CURRENT_TIMESTAMP, "
            "json_data TEXT"
            ")", table_id);
            
        int rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "错误: 创建表 %d 失败: %s\n", table_id, sqlite3_errmsg(db));
            return rc;
        }
        
        if (config->verbose) {
            printf("✓ 创建表 data_table_%d\n", table_id);
        }
    }
    
    return SQLITE_OK;
}

// Generate database content
static int generate_database_content(sqlite3 *db, GeneratorConfig *config) {
    char *text_buffer = malloc(config->record_size + 1);
    char *blob_buffer = malloc(config->record_size + 1);
    char sql[2048];
    sqlite3_stmt *stmt = NULL;
    int record_id = 0;
    long current_size = 0;
    time_t start_time = time(NULL);
    int records_inserted = 0;
    
    if (!text_buffer || !blob_buffer) {
        fprintf(stderr, "错误: 内存分配失败\n");
        free(text_buffer);
        free(blob_buffer);
        return SQLITE_NOMEM;
    }
    
    // Start transaction
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    printf("开始生成数据库内容...\n");
    printf("目标大小: %ld 字节 (%.2f MB)\n", config->target_size, config->target_size / (1024.0 * 1024.0));
    printf("数据模式: ");
    switch (config->data_mode) {
        case DATA_MODE_RANDOM: printf("随机数据\n"); break;
        case DATA_MODE_SEQUENTIAL: printf("顺序数据\n"); break;
        case DATA_MODE_LOREM: printf("Lorem ipsum\n"); break;
        case DATA_MODE_BINARY: printf("二进制数据\n"); break;
        case DATA_MODE_MIXED: printf("混合数据\n"); break;
    }
    printf("记录大小: %d 字节\n", config->record_size);
    printf("表数量: %d\n", config->table_count);
    printf("\n");
    
    while (current_size < config->target_size) {
        int table_id = record_id % config->table_count;
        
        // Prepare insert statement for current table
        snprintf(sql, sizeof(sql),
            "INSERT INTO data_table_%d (text_data, blob_data, numeric_data, json_data) "
            "VALUES (?, ?, ?, ?)", table_id);
            
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "错误: 准备SQL语句失败: %s\n", sqlite3_errmsg(db));
            break;
        }
        
        // Generate data for this record
        generate_data(text_buffer, config->record_size, config->data_mode, record_id);
        generate_data(blob_buffer, config->record_size / 2, DATA_MODE_BINARY, record_id);
        
        // Create JSON data
        char json_data[512];
        snprintf(json_data, sizeof(json_data),
            "{\"record_id\":%d,\"table_id\":%d,\"timestamp\":%ld,\"random\":%d}",
            record_id, table_id, time(NULL), rand() % 10000);
        
        // Bind parameters
        sqlite3_bind_text(stmt, 1, text_buffer, -1, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 2, blob_buffer, config->record_size / 2, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 3, (double)record_id + (rand() % 1000) / 1000.0);
        sqlite3_bind_text(stmt, 4, json_data, -1, SQLITE_STATIC);
        
        // Execute insert
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "错误: 插入数据失败: %s\n", sqlite3_errmsg(db));
            break;
        }
        
        record_id++;
        records_inserted++;
        
        // Commit transaction periodically
        if (record_id % config->batch_size == 0) {
            sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
            sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
            
            // Check current file size
            current_size = get_file_size(config->output_file);
            
            if (config->verbose || record_id % (config->batch_size * 10) == 0) {
                time_t current_time = time(NULL);
                double elapsed = difftime(current_time, start_time);
                double progress = (double)current_size / config->target_size * 100.0;
                
                printf("\r进度: %.1f%% (%ld/%ld 字节) - %d 记录 - %.1f 记录/秒",
                       progress, current_size, config->target_size, records_inserted,
                       records_inserted / (elapsed > 0 ? elapsed : 1));
                fflush(stdout);
            }
            
            if (current_size >= config->target_size) {
                break;
            }
        }
    }
    
    // Final commit
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    printf("\n");
    
    free(text_buffer);
    free(blob_buffer);
    
    printf("✓ 数据生成完成: %d 记录插入到 %d 个表中\n", records_inserted, config->table_count);
    return SQLITE_OK;
}

// Parse size string (e.g., "10MB", "500KB", "2GB")
static long parse_size_string(const char *size_str) {
    if (!size_str) return 0;
    
    char *endptr;
    double value = strtod(size_str, &endptr);
    
    if (value <= 0) return 0;
    
    // Handle size suffixes
    if (*endptr) {
        if (strcasecmp(endptr, "B") == 0) {
            // bytes - no change
        } else if (strcasecmp(endptr, "KB") == 0 || strcasecmp(endptr, "K") == 0) {
            value *= 1024;
        } else if (strcasecmp(endptr, "MB") == 0 || strcasecmp(endptr, "M") == 0) {
            value *= 1024 * 1024;
        } else if (strcasecmp(endptr, "GB") == 0 || strcasecmp(endptr, "G") == 0) {
            value *= 1024 * 1024 * 1024;
        } else {
            return 0;  // Invalid suffix
        }
    }
    
    return (long)value;
}

// Parse block size string
static uint32_t parse_block_size(const char *size_str) {
    if (!size_str) return 0;
    
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
    if (value < CCVFS_MIN_BLOCK_SIZE || value > CCVFS_MAX_BLOCK_SIZE) {
        return 0;
    }
    
    // Check if power of 2
    if ((value & (value - 1)) != 0) {
        return 0;
    }
    
    return (uint32_t)value;
}

// Print usage information
static void print_usage(const char *program_name) {
    printf("数据库生成工具 - 创建任意大小的压缩或非压缩数据库\n");
    printf("用法: %s [选项] <输出文件> <目标大小>\n\n", program_name);
    
    printf("参数:\n");
    printf("  <输出文件>        输出数据库文件路径\n");
    printf("  <目标大小>        目标文件大小 (例如: 10MB, 500KB, 2GB)\n\n");
    
    printf("选项:\n");
    printf("  -c, --compress              启用压缩 (使用CCVFS)\n");
    printf("  -a, --compress-algo <算法>  压缩算法 (rle, lz4, zlib, 默认: zlib)\n");
    printf("  -e, --encrypt-algo <算法>   加密算法 (xor, aes128, aes256, chacha20)\n");
    printf("  -b, --block-size <大小>     压缩块大小 (1K-1M, 默认: 64K)\n");
    printf("  -l, --level <等级>          压缩等级 (1-9, 默认: 6)\n");
    printf("  -m, --mode <模式>           数据生成模式:\n");
    printf("                                random    - 随机文本数据 (默认)\n");
    printf("                                sequential - 顺序数字和文本\n");
    printf("                                lorem     - Lorem ipsum 文本\n");
    printf("                                binary    - 二进制数据\n");
    printf("                                mixed     - 混合数据类型\n");
    printf("  -r, --record-size <大小>    平均记录大小 (字节, 默认: 1024)\n");
    printf("  -t, --tables <数量>         创建表的数量 (默认: 1)\n");
    printf("  -batch, --batch-size <大小> 每个事务的记录数 (默认: 1000)\n");
    printf("  -v, --verbose               详细输出\n");
    printf("  -h, --help                  显示帮助信息\n\n");
    
    printf("示例:\n");
    printf("  %s test.db 10MB                          # 创建10MB非压缩数据库\n", program_name);
    printf("  %s -c test.ccvfs 50MB                    # 创建50MB压缩数据库\n", program_name);
    printf("  %s -c -a zlib -b 4K test.ccvfs 100MB    # 使用zlib和4KB块压缩\n", program_name);
    printf("  %s -m lorem -r 2048 -t 5 test.db 25MB   # Lorem文本,2KB记录,5个表\n", program_name);
    printf("  %s -c -e aes128 secure.ccvfs 1GB        # 压缩+AES128加密的1GB数据库\n", program_name);
}

int main(int argc, char *argv[]) {
    GeneratorConfig config = {
        .output_file = NULL,
        .target_size = 0,
        .use_compression = 0,
        .compress_algorithm = "zlib",
        .encrypt_algorithm = NULL,
        .block_size = 0,  // Use default
        .compression_level = 6,
        .data_mode = DATA_MODE_RANDOM,
        .record_size = 1024,
        .table_count = 1,
        .verbose = 0,
        .batch_size = 1000
    };
    
    static struct option long_options[] = {
        {"compress",        no_argument,       0, 'c'},
        {"compress-algo",   required_argument, 0, 'a'},
        {"encrypt-algo",    required_argument, 0, 'e'},
        {"block-size",      required_argument, 0, 'b'},
        {"level",           required_argument, 0, 'l'},
        {"mode",            required_argument, 0, 'm'},
        {"record-size",     required_argument, 0, 'r'},
        {"tables",          required_argument, 0, 't'},
        {"batch-size",      required_argument, 0, 1000},
        {"verbose",         no_argument,       0, 'v'},
        {"help",            no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "ca:e:b:l:m:r:t:vh", long_options, NULL)) != -1) {
        switch (c) {
            case 'c':
                config.use_compression = 1;
                break;
            case 'a':
                config.compress_algorithm = optarg;
                break;
            case 'e':
                config.encrypt_algorithm = optarg;
                if (strcmp(config.encrypt_algorithm, "none") == 0) {
                    config.encrypt_algorithm = NULL;
                }
                break;
            case 'b':
                config.block_size = parse_block_size(optarg);
                if (config.block_size == 0) {
                    fprintf(stderr, "错误: 无效的块大小 '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'l':
                config.compression_level = atoi(optarg);
                if (config.compression_level < 1 || config.compression_level > 9) {
                    fprintf(stderr, "错误: 压缩等级必须在1-9之间\n");
                    return 1;
                }
                break;
            case 'm':
                if (strcmp(optarg, "random") == 0) {
                    config.data_mode = DATA_MODE_RANDOM;
                } else if (strcmp(optarg, "sequential") == 0) {
                    config.data_mode = DATA_MODE_SEQUENTIAL;
                } else if (strcmp(optarg, "lorem") == 0) {
                    config.data_mode = DATA_MODE_LOREM;
                } else if (strcmp(optarg, "binary") == 0) {
                    config.data_mode = DATA_MODE_BINARY;
                } else if (strcmp(optarg, "mixed") == 0) {
                    config.data_mode = DATA_MODE_MIXED;
                } else {
                    fprintf(stderr, "错误: 无效的数据模式 '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'r':
                config.record_size = atoi(optarg);
                if (config.record_size < 100 || config.record_size > 1000000) {
                    fprintf(stderr, "错误: 记录大小必须在100-1000000字节之间\n");
                    return 1;
                }
                break;
            case 't':
                config.table_count = atoi(optarg);
                if (config.table_count < 1 || config.table_count > 100) {
                    fprintf(stderr, "错误: 表数量必须在1-100之间\n");
                    return 1;
                }
                break;
            case 1000:  // --batch-size
                config.batch_size = atoi(optarg);
                if (config.batch_size < 10 || config.batch_size > 100000) {
                    fprintf(stderr, "错误: 批量大小必须在10-100000之间\n");
                    return 1;
                }
                break;
            case 'v':
                config.verbose = 1;
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
    
    // Validate arguments
    if (optind + 2 > argc) {
        fprintf(stderr, "错误: 缺少必需参数\n");
        print_usage(argv[0]);
        return 1;
    }
    
    config.output_file = argv[optind];
    config.target_size = parse_size_string(argv[optind + 1]);
    
    if (config.target_size <= 0) {
        fprintf(stderr, "错误: 无效的目标大小 '%s'\n", argv[optind + 1]);
        return 1;
    }
    
    // Initialize random seed
    srand((unsigned int)time(NULL));
    
    printf("=== 数据库生成工具 ===\n");
    printf("输出文件: %s\n", config.output_file);
    printf("目标大小: %ld 字节 (%.2f MB)\n", config.target_size, config.target_size / (1024.0 * 1024.0));
    printf("压缩: %s\n", config.use_compression ? "是" : "否");
    if (config.use_compression) {
        printf("压缩算法: %s\n", config.compress_algorithm);
        printf("加密算法: %s\n", config.encrypt_algorithm ? config.encrypt_algorithm : "无");
        printf("块大小: %s\n", config.block_size > 0 ? "自定义" : "64KB (默认)");
        printf("压缩等级: %d\n", config.compression_level);
    }
    printf("\n");
    
    // Remove existing file
    remove(config.output_file);
    
    sqlite3 *db = NULL;
    int rc;
    
    if (config.use_compression) {
        // Create CCVFS for compression
        rc = sqlite3_ccvfs_create("generator_vfs", NULL, 
                                  config.compress_algorithm, 
                                  config.encrypt_algorithm,
                                  config.block_size, 
                                  CCVFS_CREATE_OFFLINE);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "错误: 创建压缩VFS失败: %d\n", rc);
            return 1;
        }
        
        // Open database with compression
        rc = sqlite3_open_v2(config.output_file, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             "generator_vfs");
    } else {
        // Open regular database
        rc = sqlite3_open_v2(config.output_file, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             NULL);
    }
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "错误: 打开数据库失败: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        if (config.use_compression) {
            sqlite3_ccvfs_destroy("generator_vfs");
        }
        return 1;
    }
    
    // Configure database
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA cache_size=-2000", NULL, NULL, NULL);  // 2MB cache
    
    time_t start_time = time(NULL);
    
    // Create tables
    rc = create_tables(db, &config);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        if (config.use_compression) {
            sqlite3_ccvfs_destroy("generator_vfs");
        }
        return 1;
    }
    
    // Generate content
    rc = generate_database_content(db, &config);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        if (config.use_compression) {
            sqlite3_ccvfs_destroy("generator_vfs");
        }
        return 1;
    }
    
    // Close database
    sqlite3_close(db);
    
    if (config.use_compression) {
        sqlite3_ccvfs_destroy("generator_vfs");
    }
    
    time_t end_time = time(NULL);
    double elapsed = difftime(end_time, start_time);
    
    // Get final file size
    long final_size = get_file_size(config.output_file);
    
    printf("\n=== 生成完成 ===\n");
    printf("最终文件大小: %ld 字节 (%.2f MB)\n", final_size, final_size / (1024.0 * 1024.0));
    printf("目标达成率: %.2f%%\n", (double)final_size / config.target_size * 100.0);
    printf("用时: %.0f 秒\n", elapsed);
    printf("生成速度: %.2f MB/秒\n", (final_size / (1024.0 * 1024.0)) / (elapsed > 0 ? elapsed : 1));
    
    if (config.use_compression) {
        printf("\n数据库已使用CCVFS压缩格式保存\n");
        printf("要解压缩，请使用: ./db_tool decompress %s output.db\n", config.output_file);
    }
    
    return 0;
}