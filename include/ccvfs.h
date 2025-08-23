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
 * Register compression and encryption VFS module
 * Parameters:
 *   zVfsName - Name of the new VFS
 *   pRootVfs - Underlying VFS (usually the default VFS)
 *   zCompressType - Compression algorithm type
 *   zEncryptType - Encryption algorithm type
 *   pageSize - Block size in bytes (1KB - 1MB), 0 for default (64KB)
 *   flags - Creation flags (CCVFS_CREATE_*)
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_create(
    const char *zVfsName,
    sqlite3_vfs *pRootVfs,
    const char *zCompressType,
    const char *zEncryptType,
    uint32_t pageSize,
    uint32_t flags
);

/*
 * Destroy compression and encryption VFS module
 */
int sqlite3_ccvfs_destroy(const char *zVfsName);

/*
 * Register custom compression algorithm
 */
int sqlite3_ccvfs_register_compress_algorithm(CompressAlgorithm *algorithm);

/*
 * Register custom encryption algorithm
 */
int sqlite3_ccvfs_register_encrypt_algorithm(EncryptAlgorithm *algorithm);

/*
 * Activate compression and encryption VFS, similar to sqlite3_activate_cerod
 */
int sqlite3_activate_ccvfs(const char *zCompressType, const char *zEncryptType);

/*
 * Compress an existing SQLite database (offline compression)
 */
int sqlite3_ccvfs_compress_database(
    const char *source_db,
    const char *compressed_db,
    const char *compress_algorithm,
    const char *encrypt_algorithm,
    int compression_level
);

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
);

/*
 * Decompress a compressed database to standard SQLite format
 */
int sqlite3_ccvfs_decompress_database(
    const char *compressed_db,
    const char *output_db
);

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
 * 全局密钥管理函数
 * Set and get encryption key for CCVFS
 */
void ccvfs_set_encryption_key(const unsigned char *key, int keyLen);

int ccvfs_get_encryption_key(unsigned char *key, int maxLen);

/*
 * Convenience macro for backward compatibility
 * Creates VFS with default 64KB page size
 */
#define sqlite3_ccvfs_create_default(zVfsName, pRootVfs, zCompressType, zEncryptType, flags) \
    sqlite3_ccvfs_create(zVfsName, pRootVfs, zCompressType, zEncryptType, 0, flags)

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* COMPRESS_VFS_H */
