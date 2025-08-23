#include "ccvfs.h"
#include "ccvfs_internal.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/*
 * Database compression and decompression tool
 * Provides utility functions for offline database compression/decompression
 */

// Helper function to get file size
static long get_file_size(const char* filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// Helper function to check if file exists
static int file_exists(const char* filename) {
    struct stat st;
    return (stat(filename, &st) == 0);
}

// Helper function to calculate CRC32 checksum
static uint32_t calculate_crc32(const void* data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    const unsigned char* buf = (const unsigned char*)data;
    
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc ^ 0xFFFFFFFF;
}

/*
 * Compress an existing SQLite database file
 */
int sqlite3_ccvfs_compress_database(
    const char *source_db,
    const char *compressed_db,
    const char *compress_algorithm,
    const char *encrypt_algorithm,
    int compression_level
) {
    sqlite3 *source = NULL;
    sqlite3 *target = NULL;
    FILE *source_file = NULL;
    FILE *target_file = NULL;
    int rc = SQLITE_OK;
    long source_size, target_size;
    CCVFSFileHeader header;
    time_t start_time, end_time;
    
    printf("开始压缩数据库...\n");
    printf("源文件: %s\n", source_db);
    printf("目标文件: %s\n", compressed_db);
    printf("压缩算法: %s\n", compress_algorithm ? compress_algorithm : "none");
    printf("加密算法: %s\n", encrypt_algorithm ? encrypt_algorithm : "none");
    printf("压缩等级: %d\n", compression_level);
    
    start_time = time(NULL);
    
    // Check if source database exists
    if (!file_exists(source_db)) {
        printf("错误: 源数据库文件不存在: %s\n", source_db);
        return SQLITE_ERROR;
    }
    
    source_size = get_file_size(source_db);
    if (source_size <= 0) {
        printf("错误: 无法获取源文件大小\n");
        return SQLITE_ERROR;
    }
    
    printf("源文件大小: %ld 字节\n", source_size);
    
    // Map string algorithms to algorithm pointers
    const CompressAlgorithm *pCompressAlg = NULL;
    const EncryptAlgorithm *pEncryptAlg = NULL;
    
    if (compress_algorithm && strcmp(compress_algorithm, "zlib") == 0) {
#ifdef HAVE_ZLIB
        pCompressAlg = CCVFS_COMPRESS_ZLIB;
#else
        printf("错误: ZLIB压缩算法未编译\n");
        return SQLITE_ERROR;
#endif
    }
    
    if (encrypt_algorithm) {
        if (strcmp(encrypt_algorithm, "aes128") == 0) {
#ifdef HAVE_OPENSSL
            pEncryptAlg = CCVFS_ENCRYPT_AES128;
#else
            printf("错误: AES128加密算法未编译\n");
            return SQLITE_ERROR;
#endif
        } else if (strcmp(encrypt_algorithm, "aes256") == 0) {
#ifdef HAVE_OPENSSL
            pEncryptAlg = CCVFS_ENCRYPT_AES256;
#else
            printf("错误: AES256加密算法未编译\n");
            return SQLITE_ERROR;
#endif
        }
    }
    
    // Create CCVFS for compression
    rc = sqlite3_ccvfs_create("compress_vfs", NULL, pCompressAlg, pEncryptAlg, 
                              0, CCVFS_CREATE_OFFLINE);
    if (rc != SQLITE_OK) {
        printf("错误: 创建压缩VFS失败: %d\n", rc);
        return rc;
    }
    
    // Open source database in read-only mode
    rc = sqlite3_open_v2(source_db, &source, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        printf("错误: 打开源数据库失败: %s\n", sqlite3_errmsg(source));
        goto cleanup;
    }
    
    // Create and open target database with CCVFS
    rc = sqlite3_open_v2(compressed_db, &target, 
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                         "compress_vfs");
    if (rc != SQLITE_OK) {
        printf("错误: 创建压缩数据库失败: %s\n", sqlite3_errmsg(target));
        goto cleanup;
    }
    
    // Copy database schema and data
    printf("正在复制数据库结构和数据...\n");
    
    // Use SQLite backup API for efficient copying
    sqlite3_backup *backup = sqlite3_backup_init(target, "main", source, "main");
    if (!backup) {
        printf("错误: 初始化备份失败\n");
        rc = SQLITE_ERROR;
        goto cleanup;
    }
    
    int pages_remaining, total_pages = 0;
    do {
        rc = sqlite3_backup_step(backup, 100);  // Copy 100 pages at a time
        pages_remaining = sqlite3_backup_remaining(backup);
        if (total_pages == 0) {
            total_pages = sqlite3_backup_pagecount(backup);
        }
        
        if (total_pages > 0) {
            printf("\r进度: %.1f%% (%d/%d 页)", 
                   (double)(total_pages - pages_remaining) * 100.0 / total_pages,
                   total_pages - pages_remaining, total_pages);
            fflush(stdout);
        }
        
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
    
    printf("\n");
    
    if (rc != SQLITE_DONE) {
        printf("错误: 数据库备份失败: %d\n", rc);
        sqlite3_backup_finish(backup);
        goto cleanup;
    }
    
    sqlite3_backup_finish(backup);
    printf("数据库复制完成\n");
    
    // Close databases first to ensure all data is written
    if (source) {
        sqlite3_close(source);
        source = NULL;
    }
    if (target) {
        sqlite3_close(target);
        target = NULL;
    }
    
    // Update file header with correct statistics
    printf("更新文件头统计信息...\n");
    FILE *compressed_file = fopen(compressed_db, "r+b");
    if (compressed_file) {
        CCVFSFileHeader header;
        
        // Read current header
        if (fread(&header, sizeof(CCVFSFileHeader), 1, compressed_file) == 1) {
            // Update statistics
            header.original_file_size = (uint64_t)source_size;
            target_size = get_file_size(compressed_db);
            header.compressed_file_size = (uint64_t)target_size;
            
            if (source_size > 0 && target_size <= source_size) {
                header.compression_ratio = (uint32_t)((source_size - target_size) * 100 / source_size);
            } else if (source_size > 0) {
                // Compression made file larger, set ratio to 0 (no compression benefit)
                header.compression_ratio = 0;
            } else {
                header.compression_ratio = 0;
            }
            
            // Write updated header back
            fseek(compressed_file, 0, SEEK_SET);
            if (fwrite(&header, sizeof(CCVFSFileHeader), 1, compressed_file) == 1) {
                printf("✓ 文件头统计信息更新成功\n");
            } else {
                printf("⚠ 警告: 无法更新文件头统计信息\n");
            }
        } else {
            printf("⚠ 警告: 无法读取文件头\n");
        }
        
        fclose(compressed_file);
    } else {
        printf("⚠ 警告: 无法打开压缩文件更新统计信息\n");
    }
    
    // Get final compressed file size
    target_size = get_file_size(compressed_db);
    end_time = time(NULL);
    
    // Display compression results
    if (target_size > 0) {
        double compression_ratio;
        long space_saved = source_size - target_size;
        
        if (source_size > 0 && target_size <= source_size) {
            compression_ratio = (double)(source_size - target_size) * 100.0 / source_size;
        } else {
            compression_ratio = 0.0; // No compression benefit or file grew larger
        }
        
        printf("\n压缩完成!\n");
        printf("原始大小: %ld 字节\n", source_size);
        printf("压缩后大小: %ld 字节\n", target_size);
        printf("压缩比: %.2f%%\n", compression_ratio);
        printf("节省空间: %ld 字节\n", space_saved);
        printf("用时: %ld 秒\n", end_time - start_time);
    } else {
        printf("警告: 无法获取压缩文件大小\n");
    }
    
    rc = SQLITE_OK;

cleanup:
    if (source) sqlite3_close(source);
    if (target) sqlite3_close(target);
    if (source_file) fclose(source_file);
    if (target_file) fclose(target_file);
    
    sqlite3_ccvfs_destroy("compress_vfs");
    
    return rc;
}

/*
 * Decompress a compressed database to standard SQLite format
 */
int sqlite3_ccvfs_decompress_database(
    const char *compressed_db,
    const char *output_db
) {
    sqlite3 *source = NULL;
    sqlite3 *target = NULL;
    int rc = SQLITE_OK;
    long source_size, target_size;
    time_t start_time, end_time;
    CCVFSStats stats;
    
    printf("开始解压数据库...\n");
    printf("压缩文件: %s\n", compressed_db);
    printf("输出文件: %s\n", output_db);
    
    start_time = time(NULL);
    
    // Check if compressed database exists
    if (!file_exists(compressed_db)) {
        printf("错误: 压缩数据库文件不存在: %s\n", compressed_db);
        return SQLITE_ERROR;
    }
    
    source_size = get_file_size(compressed_db);
    if (source_size <= 0) {
        printf("错误: 无法获取压缩文件大小\n");
        return SQLITE_ERROR;
    }
    
    printf("压缩文件大小: %ld 字节\n", source_size);
    
    // Get compression statistics first to determine algorithms
    if (sqlite3_ccvfs_get_stats(compressed_db, &stats) == SQLITE_OK) {
        printf("压缩算法: %s\n", stats.compress_algorithm);
        printf("加密算法: %s\n", stats.encrypt_algorithm);
        printf("原始大小: %llu 字节\n", (unsigned long long)stats.original_size);
        printf("压缩比: %u%%\n", stats.compression_ratio);
        printf("总页数: %u\n", stats.total_pages);
    } else {
        printf("警告: 无法读取压缩文件统计信息\n");
        // Set default algorithms if stats reading fails
        strcpy(stats.compress_algorithm, "zlib");
        strcpy(stats.encrypt_algorithm, "");
    }
    
    // Create CCVFS for decompression using the correct algorithms
    const char *compress_alg = strlen(stats.compress_algorithm) > 0 ? stats.compress_algorithm : NULL;
    const char *encrypt_alg = strlen(stats.encrypt_algorithm) > 0 ? stats.encrypt_algorithm : NULL;
    
    printf("使用算法进行解压: 压缩=%s, 加密=%s\n", 
           compress_alg ? compress_alg : "无", 
           encrypt_alg ? encrypt_alg : "无");
    
    // Map string algorithms to algorithm pointers
    const CompressAlgorithm *pCompressAlg = NULL;
    const EncryptAlgorithm *pEncryptAlg = NULL;
    
    if (compress_alg && strcmp(compress_alg, "zlib") == 0) {
#ifdef HAVE_ZLIB
        pCompressAlg = CCVFS_COMPRESS_ZLIB;
#else
        printf("错误: ZLIB压缩算法未编译\n");
        return SQLITE_ERROR;
#endif
    }
    
    if (encrypt_alg) {
        if (strcmp(encrypt_alg, "aes128") == 0) {
#ifdef HAVE_OPENSSL
            pEncryptAlg = CCVFS_ENCRYPT_AES128;
#else
            printf("错误: AES128加密算法未编译\n");
            return SQLITE_ERROR;
#endif
        } else if (strcmp(encrypt_alg, "aes256") == 0) {
#ifdef HAVE_OPENSSL
            pEncryptAlg = CCVFS_ENCRYPT_AES256;
#else
            printf("错误: AES256加密算法未编译\n");
            return SQLITE_ERROR;
#endif
        }
    }
    
    rc = sqlite3_ccvfs_create("decompress_vfs", NULL, pCompressAlg, pEncryptAlg, 0, 0);
    if (rc != SQLITE_OK) {
        printf("错误: 创建解压VFS失败: %d\n", rc);
        return rc;
    }
    
    // Open compressed database with CCVFS
    rc = sqlite3_open_v2(compressed_db, &source, SQLITE_OPEN_READONLY, "decompress_vfs");
    if (rc != SQLITE_OK) {
        printf("错误: 打开压缩数据库失败: %s\n", sqlite3_errmsg(source));
        goto cleanup;
    }
    
    // Create standard SQLite database
    rc = sqlite3_open_v2(output_db, &target, 
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                         NULL);
    if (rc != SQLITE_OK) {
        printf("错误: 创建输出数据库失败: %s\n", sqlite3_errmsg(target));
        goto cleanup;
    }
    
    // Copy database from compressed to standard format
    printf("正在解压数据库...\n");
    
    sqlite3_backup *backup = sqlite3_backup_init(target, "main", source, "main");
    if (!backup) {
        printf("错误: 初始化备份失败\n");
        rc = SQLITE_ERROR;
        goto cleanup;
    }
    
    int pages_remaining, total_pages = 0;
    do {
        rc = sqlite3_backup_step(backup, 100);
        pages_remaining = sqlite3_backup_remaining(backup);
        if (total_pages == 0) {
            total_pages = sqlite3_backup_pagecount(backup);
        }
        
        if (total_pages > 0) {
            printf("\r进度: %.1f%% (%d/%d 页)", 
                   (double)(total_pages - pages_remaining) * 100.0 / total_pages,
                   total_pages - pages_remaining, total_pages);
            fflush(stdout);
        }
        
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
    
    printf("\n");
    
    if (rc != SQLITE_DONE) {
        printf("错误: 数据库解压失败: %d\n", rc);
        sqlite3_backup_finish(backup);
        goto cleanup;
    }
    
    sqlite3_backup_finish(backup);
    printf("数据库解压完成\n");
    
    // Close databases
    if (source) {
        sqlite3_close(source);
        source = NULL;
    }
    if (target) {
        sqlite3_close(target);
        target = NULL;
    }
    
    // Get output file size and display results
    target_size = get_file_size(output_db);
    end_time = time(NULL);
    
    if (target_size > 0) {
        printf("\n解压完成!\n");
        printf("压缩文件大小: %ld 字节\n", source_size);
        printf("解压后大小: %ld 字节\n", target_size);
        printf("用时: %ld 秒\n", end_time - start_time);
        
        if (stats.original_size > 0) {
            printf("还原率: %.2f%%\n", 
                   (double)target_size * 100.0 / stats.original_size);
        }
    } else {
        printf("警告: 无法获取输出文件大小\n");
    }
    
    rc = SQLITE_OK;

cleanup:
    if (source) sqlite3_close(source);
    if (target) sqlite3_close(target);
    
    sqlite3_ccvfs_destroy("decompress_vfs");
    
    return rc;
}

/*
 * Get compression statistics for a compressed database
 */
int sqlite3_ccvfs_get_stats(const char *compressed_db, CCVFSStats *stats) {
    FILE *file = NULL;
    CCVFSFileHeader header;
    int rc = SQLITE_ERROR;
    
    if (!compressed_db || !stats) {
        return SQLITE_MISUSE;
    }
    
    memset(stats, 0, sizeof(CCVFSStats));
    
    file = fopen(compressed_db, "rb");
    if (!file) {
        return SQLITE_CANTOPEN;
    }
    
    // Read file header
    if (fread(&header, sizeof(CCVFSFileHeader), 1, file) != 1) {
        goto cleanup;
    }
    
    // Verify magic number
    if (memcmp(header.magic, CCVFS_MAGIC, 8) != 0) {
        goto cleanup;
    }
    
    // Fill statistics structure
    stats->original_size = header.original_file_size;
    stats->compressed_size = header.compressed_file_size;
    stats->compression_ratio = header.compression_ratio;
    stats->total_pages = header.total_pages;
    
    strncpy(stats->compress_algorithm, header.compress_algorithm, 
            CCVFS_MAX_ALGORITHM_NAME - 1);
    stats->compress_algorithm[CCVFS_MAX_ALGORITHM_NAME - 1] = '\0';
    
    strncpy(stats->encrypt_algorithm, header.encrypt_algorithm, 
            CCVFS_MAX_ALGORITHM_NAME - 1);
    stats->encrypt_algorithm[CCVFS_MAX_ALGORITHM_NAME - 1] = '\0';
    
    rc = SQLITE_OK;

cleanup:
    if (file) fclose(file);
    return rc;
}

/*
 * SQLite CEROD activation function implementation
 * This function is called by SQLite shell to activate compression/encryption
 */
void sqlite3_activate_cerod(const char *zParms) {
    char *zCopy = NULL;
    int rc = SQLITE_OK;
    static int activation_count = 0;
    const CompressAlgorithm *pCompressAlg = NULL;
    const EncryptAlgorithm *pEncryptAlg = NULL;
    
    activation_count++;
    
    if (zParms && *zParms) {
        zCopy = sqlite3_mprintf("%s", zParms);
        if (!zCopy) {
            printf("CCVFS: Out of memory during activation\n");
            return;
        }
        
        // Parse parameters: "compression:encryption" or "compression"
        char *zCompressType = NULL;
        char *zEncryptType = NULL;
        char *zSep = strchr(zCopy, ':');
        if (zSep) {
            *zSep = '\0';
            zCompressType = zCopy;
            zEncryptType = zSep + 1;
            
            // Handle "none" values
            if (strcmp(zCompressType, "none") == 0) zCompressType = NULL;
            if (strcmp(zEncryptType, "none") == 0) zEncryptType = NULL;
        } else {
            zCompressType = zCopy;
            zEncryptType = NULL;
            
            if (strcmp(zCompressType, "none") == 0) zCompressType = NULL;
        }
        
        // Map compression algorithm names to pointers
        if (zCompressType) {
#ifdef HAVE_ZLIB
            if (strcmp(zCompressType, "zlib") == 0) {
                pCompressAlg = CCVFS_COMPRESS_ZLIB;
            }
#endif
            if (!pCompressAlg) {
                printf("CCVFS: Unknown compression algorithm: %s\n", zCompressType);
            }
        }
        
        // Map encryption algorithm names to pointers
        if (zEncryptType) {
#ifdef HAVE_OPENSSL
            if (strcmp(zEncryptType, "aes128") == 0) {
                pEncryptAlg = CCVFS_ENCRYPT_AES128;
            } else if (strcmp(zEncryptType, "aes256") == 0) {
                pEncryptAlg = CCVFS_ENCRYPT_AES256;
            }
#endif
            if (!pEncryptAlg) {
                printf("CCVFS: Unknown encryption algorithm: %s\n", zEncryptType);
            }
        }
        
        printf("CCVFS: Activating with compression=%s, encryption=%s\n", 
               pCompressAlg ? pCompressAlg->name : "none",
               pEncryptAlg ? pEncryptAlg->name : "none");
    } else {
        // Default: use zlib compression if available
#ifdef HAVE_ZLIB
        pCompressAlg = CCVFS_COMPRESS_ZLIB;
#endif
        printf("CCVFS: Activating with default settings\n");
    }
    
    // Activate CCVFS
    rc = sqlite3_activate_ccvfs(pCompressAlg, pEncryptAlg);
    if (rc == SQLITE_OK) {
        printf("CCVFS: Successfully activated (attempt #%d)\n", activation_count);
    } else {
        printf("CCVFS: Activation failed with error code %d\n", rc);
        switch (rc) {
            case SQLITE_NOMEM:
                printf("CCVFS: Out of memory\n");
                break;
            case SQLITE_ERROR:
                printf("CCVFS: Invalid algorithm or configuration\n");
                break;
            default:
                printf("CCVFS: Unknown error\n");
                break;
        }
    }
    
    if (zCopy) {
        sqlite3_free(zCopy);
    }
}

/*
 * Compress an existing SQLite database with custom page size
 */
int sqlite3_ccvfs_compress_database_with_page_size(
    const char *source_db,
    const char *compressed_db,
    const char *compress_algorithm,
    const char *encrypt_algorithm,
    uint32_t page_size,
    int compression_level
) {
    sqlite3 *source = NULL;
    sqlite3 *target = NULL;
    FILE *source_file = NULL;
    FILE *target_file = NULL;
    int rc = SQLITE_OK;
    long source_size, target_size;
    CCVFSFileHeader header;
    time_t start_time, end_time;
    
    printf("开始压缩数据库 (页大小: %u KB)...\n", page_size / 1024);
    printf("源文件: %s\n", source_db);
    printf("目标文件: %s\n", compressed_db);
    printf("压缩算法: %s\n", compress_algorithm ? compress_algorithm : "none");
    printf("加密算法: %s\n", encrypt_algorithm ? encrypt_algorithm : "none");
    printf("页大小: %u 字节 (%u KB)\n", page_size, page_size / 1024);
    printf("压缩等级: %d\n", compression_level);
    
    start_time = time(NULL);
    
    // Validate page size
    if (page_size < CCVFS_MIN_PAGE_SIZE || page_size > CCVFS_MAX_PAGE_SIZE) {
        printf("错误: 无效的页大小 %u (必须在 %u - %u 之间)\n",
               page_size, CCVFS_MIN_PAGE_SIZE, CCVFS_MAX_PAGE_SIZE);
        return SQLITE_ERROR;
    }
    
    if ((page_size & (page_size - 1)) != 0) {
        printf("错误: 页大小必须是2的幂: %u\n", page_size);
        return SQLITE_ERROR;
    }
    
    // Check if source database exists
    if (!file_exists(source_db)) {
        printf("错误: 源数据库文件不存在: %s\n", source_db);
        return SQLITE_ERROR;
    }
    
    source_size = get_file_size(source_db);
    if (source_size <= 0) {
        printf("错误: 无法获取源文件大小\n");
        return SQLITE_ERROR;
    }
    
    printf("源文件大小: %ld 字节\n", source_size);
    
    // Map string algorithms to algorithm pointers
    const CompressAlgorithm *pCompressAlg = NULL;
    const EncryptAlgorithm *pEncryptAlg = NULL;
    
    if (compress_algorithm && strcmp(compress_algorithm, "zlib") == 0) {
#ifdef HAVE_ZLIB
        pCompressAlg = CCVFS_COMPRESS_ZLIB;
#else
        printf("错误: ZLIB压缩算法未编译\n");
        return SQLITE_ERROR;
#endif
    }
    
    if (encrypt_algorithm) {
        if (strcmp(encrypt_algorithm, "aes128") == 0) {
#ifdef HAVE_OPENSSL
            pEncryptAlg = CCVFS_ENCRYPT_AES128;
#else
            printf("错误: AES128加密算法未编译\n");
            return SQLITE_ERROR;
#endif
        } else if (strcmp(encrypt_algorithm, "aes256") == 0) {
#ifdef HAVE_OPENSSL
            pEncryptAlg = CCVFS_ENCRYPT_AES256;
#else
            printf("错误: AES256加密算法未编译\n");
            return SQLITE_ERROR;
#endif
        }
    }
    
    // Create CCVFS for compression with custom page size
    rc = sqlite3_ccvfs_create("compress_vfs_custom", NULL, 
                              pCompressAlg, pEncryptAlg, 
                              page_size, CCVFS_CREATE_OFFLINE);
    if (rc != SQLITE_OK) {
        printf("错误: 创建压缩VFS失败: %d\n", rc);
        return rc;
    }
    
    // Open source database in read-only mode
    rc = sqlite3_open_v2(source_db, &source, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        printf("错误: 打开源数据库失败: %s\n", sqlite3_errmsg(source));
        goto cleanup;
    }
    
    // Create and open target database with custom CCVFS
    rc = sqlite3_open_v2(compressed_db, &target, 
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                         "compress_vfs_custom");
    if (rc != SQLITE_OK) {
        printf("错误: 创建压缩数据库失败: %s\n", sqlite3_errmsg(target));
        goto cleanup;
    }
    
    // Copy database schema and data
    printf("正在复制数据库结构和数据...\n");
    
    // Use SQLite backup API for efficient copying
    sqlite3_backup *backup = sqlite3_backup_init(target, "main", source, "main");
    if (!backup) {
        printf("错误: 初始化备份失败\n");
        rc = SQLITE_ERROR;
        goto cleanup;
    }
    
    int pages_remaining, total_pages = 0;
    do {
        rc = sqlite3_backup_step(backup, 100);  // Copy 100 pages at a time
        pages_remaining = sqlite3_backup_remaining(backup);
        if (total_pages == 0) {
            total_pages = sqlite3_backup_pagecount(backup);
        }
        
        if (total_pages > 0) {
            printf("\r进度: %.1f%% (%d/%d 页)", 
                   (double)(total_pages - pages_remaining) * 100.0 / total_pages,
                   total_pages - pages_remaining, total_pages);
            fflush(stdout);
        }
        
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
    
    printf("\n");
    
    if (rc != SQLITE_DONE) {
        printf("错误: 数据库备份失败: %d\n", rc);
        sqlite3_backup_finish(backup);
        goto cleanup;
    }
    
    sqlite3_backup_finish(backup);
    printf("数据库复制完成\n");
    
    // Close databases first to ensure all data is written
    if (source) {
        sqlite3_close(source);
        source = NULL;
    }
    if (target) {
        sqlite3_close(target);
        target = NULL;
    }
    
    // Update file header with correct statistics
    printf("更新文件头统计信息...\n");
    FILE *compressed_file = fopen(compressed_db, "r+b");
    if (compressed_file) {
        CCVFSFileHeader header;
        
        // Read current header
        if (fread(&header, sizeof(CCVFSFileHeader), 1, compressed_file) == 1) {
            // Update statistics
            header.original_file_size = (uint64_t)source_size;
            target_size = get_file_size(compressed_db);
            header.compressed_file_size = (uint64_t)target_size;
            
            if (source_size > 0 && target_size <= source_size) {
                header.compression_ratio = (uint32_t)((source_size - target_size) * 100 / source_size);
            } else if (source_size > 0) {
                // Compression made file larger, set ratio to 0 (no compression benefit)
                header.compression_ratio = 0;
            } else {
                header.compression_ratio = 0;
            }
            
            // Write updated header back
            fseek(compressed_file, 0, SEEK_SET);
            if (fwrite(&header, sizeof(CCVFSFileHeader), 1, compressed_file) == 1) {
                printf("✓ 文件头统计信息更新成功\n");
            } else {
                printf("⚠ 警告: 无法更新文件头统计信息\n");
            }
        } else {
            printf("⚠ 警告: 无法读取文件头\n");
        }
        
        fclose(compressed_file);
    } else {
        printf("⚠ 警告: 无法打开压缩文件更新统计信息\n");
    }
    
    // Get final compressed file size
    target_size = get_file_size(compressed_db);
    end_time = time(NULL);
    
    // Display compression results
    if (target_size > 0) {
        double compression_ratio;
        long space_saved = source_size - target_size;
        
        if (source_size > 0 && target_size <= source_size) {
            compression_ratio = (double)(source_size - target_size) * 100.0 / source_size;
        } else {
            compression_ratio = 0.0; // No compression benefit or file grew larger
        }
        
        printf("\n压缩完成!\n");
        printf("原始大小: %ld 字节\n", source_size);
        printf("压缩后大小: %ld 字节\n", target_size);
        printf("压缩比: %.2f%%\n", compression_ratio);
        printf("节省空间: %ld 字节\n", space_saved);
        printf("页大小: %u KB\n", page_size / 1024);
        printf("用时: %ld 秒\n", end_time - start_time);
    } else {
        printf("警告: 无法获取压缩文件大小\n");
    }
    
    rc = SQLITE_OK;

cleanup:
    if (source) sqlite3_close(source);
    if (target) sqlite3_close(target);
    if (source_file) fclose(source_file);
    if (target_file) fclose(target_file);
    
    sqlite3_ccvfs_destroy("compress_vfs_custom");
    
    return rc;
}