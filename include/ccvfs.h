#ifndef COMPRESS_VFS_H
#define COMPRESS_VFS_H

#include "sqlite3.h"
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// File format constants
#define CCVFS_MAGIC "CCVFSDB\0"
#define CCVFS_PAGE_MAGIC 0x50434356  // "PCCV" (Page CCVFS)
#define CCVFS_VERSION_MAJOR 1
#define CCVFS_VERSION_MINOR 0
#define CCVFS_HEADER_SIZE 128

// Public constants
#define CCVFS_MAX_ALGORITHM_NAME 12
#define CCVFS_DEFAULT_PAGE_SIZE (64 * 1024)  // 64KB pages
#define CCVFS_MIN_PAGE_SIZE (1 * 1024)       // 1KB minimum
#define CCVFS_MAX_PAGE_SIZE (1 * 1024 * 1024) // 1MB maximum

// Common page sizes (aligned with SQLite page sizes)
#define CCVFS_PAGE_SIZE_1KB   (1 * 1024)    // SQLite minimum
#define CCVFS_PAGE_SIZE_4KB   (4 * 1024)    // SQLite default
#define CCVFS_PAGE_SIZE_8KB   (8 * 1024)
#define CCVFS_PAGE_SIZE_16KB  (16 * 1024)
#define CCVFS_PAGE_SIZE_32KB  (32 * 1024)
#define CCVFS_PAGE_SIZE_64KB  (64 * 1024)   // SQLite maximum
#define CCVFS_PAGE_SIZE_128KB (128 * 1024)
#define CCVFS_PAGE_SIZE_256KB (256 * 1024)
#define CCVFS_PAGE_SIZE_512KB (512 * 1024)
#define CCVFS_PAGE_SIZE_1MB   (1 * 1024 * 1024)

// Creation flags
#define CCVFS_CREATE_REALTIME    (1 << 0)
#define CCVFS_CREATE_OFFLINE     (1 << 1)
#define CCVFS_CREATE_HYBRID      (1 << 2)


/*
 * Compression algorithm interface
 */
typedef struct {
    const char *name;

    int (*compress)(const unsigned char *input, int input_len,
                    unsigned char *output, int output_len, int level);

    int (*decompress)(const unsigned char *input, int input_len,
                      unsigned char *output, int output_len);

    int (*get_max_compressed_size)(int input_len);
} CompressAlgorithm;

/*
 * Encryption algorithm interface
 */
typedef struct {
    const char *name;

    int (*encrypt)(const unsigned char *key, int key_len,
                   const unsigned char *input, int input_len,
                   unsigned char *output, int output_len);

    int (*decrypt)(const unsigned char *key, int key_len,
                   const unsigned char *input, int input_len,
                   unsigned char *output, int output_len);

    int key_size; // Required key size
} EncryptAlgorithm;

/*
 * Predefined algorithm constants for convenience
 * These can be used with sqlite3_ccvfs_create_with_algorithms()
 * 
 * Built-in algorithms (controlled by compile-time macros):
 * - CCVFS_COMPRESS_ZLIB: Available only when HAVE_ZLIB is defined
 * - CCVFS_ENCRYPT_AES128/AES256: Available only when HAVE_OPENSSL is defined
 * 
 * For no compression/encryption, simply pass NULL.
 * For other algorithms, users must define their own CompressAlgorithm 
 * or EncryptAlgorithm structures and pass them directly.
 */
#ifdef HAVE_ZLIB
extern const CompressAlgorithm *CCVFS_COMPRESS_ZLIB;   // ZLib compression
#endif

#ifdef HAVE_OPENSSL
extern const EncryptAlgorithm *CCVFS_ENCRYPT_AES128;   // AES-128 encryption
extern const EncryptAlgorithm *CCVFS_ENCRYPT_AES256;   // AES-256 encryption
#endif

/*
 * Register compression and encryption VFS module
 * Parameters:
 *   zVfsName - Name of the new VFS
 *   pRootVfs - Underlying VFS (usually the default VFS)
 *   pCompressAlg - Compression algorithm (NULL for no compression)
 *   pEncryptAlg - Encryption algorithm (NULL for no encryption)
 *   pageSize - Block size in bytes (1KB - 1MB), 0 for default (64KB)
 *   flags - Creation flags (CCVFS_CREATE_*)
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 * 
 * Example usage:
 *   // Using built-in algorithms (if available)
 *   #ifdef HAVE_ZLIB
 *   sqlite3_ccvfs_create("my_vfs", NULL, 
 *                         CCVFS_COMPRESS_ZLIB, 
 *                         NULL,  // No encryption
 *                         0, CCVFS_CREATE_REALTIME);
 *   #endif
 * 
 *   // No compression, no encryption
 *   sqlite3_ccvfs_create("simple_vfs", NULL, 
 *                         NULL, NULL, 
 *                         0, CCVFS_CREATE_REALTIME);
 * 
 *   // Using custom algorithm
 *   CompressAlgorithm my_compress = {
 *       .name = "my_algorithm",
 *       .compress = my_compress_func,
 *       .decompress = my_decompress_func,
 *       .get_max_compressed_size = my_max_size_func
 *   };
 *   sqlite3_ccvfs_create("my_vfs", NULL, 
 *                         &my_compress, NULL, 
 *                         0, CCVFS_CREATE_REALTIME);
 */
int sqlite3_ccvfs_create(
    const char *zVfsName,
    sqlite3_vfs *pRootVfs,
    const CompressAlgorithm *pCompressAlg,
    const EncryptAlgorithm *pEncryptAlg,
    uint32_t pageSize,
    uint32_t flags
);

/*
 * Create CCVFS with encryption key - 推荐用于加密场景
 * This is a convenience function that creates VFS and sets key in one call
 */
int sqlite3_ccvfs_create_with_key(
    const char *zVfsName,
    sqlite3_vfs *pRootVfs,
    const CompressAlgorithm *pCompressAlg,
    const EncryptAlgorithm *pEncryptAlg,
    uint32_t pageSize,
    uint32_t flags,
    const unsigned char *key,
    int keyLen
);

/*
 * Destroy compression and encryption VFS module
 */
int sqlite3_ccvfs_destroy(const char *zVfsName);

/*
 * Activate compression and encryption VFS, similar to sqlite3_activate_cerod
 */
int sqlite3_activate_ccvfs(const CompressAlgorithm *pCompressAlg, const EncryptAlgorithm *pEncryptAlg);

/*
 * Get compression statistics for a compressed database
 */
typedef struct {
    uint64_t original_size;
    uint64_t compressed_size;
    uint32_t compression_ratio;
    uint32_t total_pages;
    char compress_algorithm[CCVFS_MAX_ALGORITHM_NAME];
    char encrypt_algorithm[CCVFS_MAX_ALGORITHM_NAME];
} CCVFSStats;

int sqlite3_ccvfs_get_stats(const char *compressed_db, CCVFSStats *stats);

/*
 * Configure write buffer settings for a VFS
 * Parameters:
 *   zVfsName - Name of the VFS to configure
 *   enabled - Whether to enable write buffering (0 or 1)
 *   max_entries - Maximum number of pages to buffer (0 for default)
 *   max_buffer_size - Maximum buffer size in bytes (0 for default)
 *   auto_flush_pages - Auto flush threshold in pages (0 for default)
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_configure_write_buffer(
    const char *zVfsName,
    int enabled,
    uint32_t max_entries,
    uint32_t max_buffer_size,
    uint32_t auto_flush_pages
);

/*
 * Get write buffer statistics for an open database
 * Parameters:
 *   db - Open database connection
 *   buffer_hits - Buffer hit count (output)
 *   buffer_flushes - Buffer flush count (output)
 *   buffer_merges - Buffer merge count (output)
 *   total_buffered_writes - Total buffered writes count (output)
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_get_buffer_stats(
    sqlite3 *db,
    uint32_t *buffer_hits,
    uint32_t *buffer_flushes,
    uint32_t *buffer_merges,
    uint32_t *total_buffered_writes
);

/*
 * Force flush write buffer for an open database
 * Parameters:
 *   db - Open database connection
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_flush_write_buffer(sqlite3 *db);

/*
 * VFS级别密钥管理函数
 * VFS-level key management functions
 */
int sqlite3_ccvfs_set_key(const char *zVfsName, const unsigned char *key, int keyLen);
int sqlite3_ccvfs_get_key(const char *zVfsName, unsigned char *key, int maxLen);
int sqlite3_ccvfs_clear_key(const char *zVfsName);

/*
 * 通过VFS执行压缩和加密操作
 * Perform compression and encryption operations through VFS
 * 
 * 这些函数使用指定VFS绑定的压缩和加密算法来处理数据库
 * These functions use the compression and encryption algorithms bound to the specified VFS
 */

/*
 * 使用指定VFS执行数据库压缩加密操作
 * Compress and encrypt database using specified VFS
 * Parameters:
 *   zVfsName - VFS name (must be created first with sqlite3_ccvfs_create*)
 *   source_db - Source database file path
 *   target_db - Target compressed/encrypted database file path
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_compress_encrypt(
    const char *zVfsName,
    const char *source_db,
    const char *target_db
);

/*
 * 使用指定VFS执行数据库解压解密操作
 * Decompress and decrypt database using specified VFS
 * Parameters:
 *   zVfsName - VFS name (must be created first with sqlite3_ccvfs_create*)
 *   source_db - Source compressed/encrypted database file path
 *   target_db - Target decompressed/decrypted database file path
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_decompress_decrypt(
    const char *zVfsName,
    const char *source_db,
    const char *target_db
);

/*
 * 便利函数：创建VFS并执行压缩加密操作
 * Convenience function: Create VFS and perform compression/encryption
 * Parameters:
 *   zVfsName - VFS name to create
 *   pCompressAlg - Compression algorithm (NULL for no compression)
 *   pEncryptAlg - Encryption algorithm (NULL for no encryption)
 *   source_db - Source database file path
 *   target_db - Target compressed/encrypted database file path
 *   key - Encryption key (can be NULL if no encryption)
 *   keyLen - Key length
 *   pageSize - Page size (0 for default)
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_create_and_compress_encrypt(
    const char *zVfsName,
    const CompressAlgorithm *pCompressAlg,
    const EncryptAlgorithm *pEncryptAlg,
    const char *source_db,
    const char *target_db,
    const unsigned char *key,
    int keyLen,
    uint32_t pageSize
);

/*
 * 便利函数：创建VFS并执行解压解密操作
 * Convenience function: Create VFS and perform decompression/decryption
 * Parameters:
 *   zVfsName - VFS name to create
 *   pCompressAlg - Compression algorithm (NULL for no compression)
 *   pEncryptAlg - Encryption algorithm (NULL for no encryption)
 *   source_db - Source compressed/encrypted database file path
 *   target_db - Target decompressed/decrypted database file path
 *   key - Decryption key (can be NULL if no encryption)
 *   keyLen - Key length
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_create_and_decompress_decrypt(
    const char *zVfsName,
    const CompressAlgorithm *pCompressAlg,
    const EncryptAlgorithm *pEncryptAlg,
    const char *source_db,
    const char *target_db,
    const unsigned char *key,
    int keyLen
);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* COMPRESS_VFS_H */
