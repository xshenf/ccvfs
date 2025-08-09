#ifndef COMPRESS_VFS_H
#define COMPRESS_VFS_H

#include "sqlite3.h"
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constants
#define CCVFS_MAGIC "CCVFSDB\0"
#define CCVFS_PAGE_MAGIC 0x50434356  // "PCCV" (Page CCVFS)
#define CCVFS_VERSION_MAJOR 1
#define CCVFS_VERSION_MINOR 0
#define CCVFS_HEADER_SIZE 128
#define CCVFS_DEFAULT_PAGE_SIZE (64 * 1024)  // 64KB pages (可配置为SQLite页面大小)
#define CCVFS_MIN_PAGE_SIZE (1 * 1024)       // 1KB minimum
#define CCVFS_MAX_PAGE_SIZE (1 * 1024 * 1024) // 1MB maximum
#define CCVFS_MAX_ALGORITHM_NAME 12

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

// File layout constants
#define CCVFS_MAX_PAGES 65536  // Maximum pages supported (2^16)
#define CCVFS_INDEX_TABLE_SIZE (CCVFS_MAX_PAGES * 24)  // 24 bytes per page index
#define CCVFS_INDEX_TABLE_OFFSET CCVFS_HEADER_SIZE  // Fixed position after header
#define CCVFS_DATA_PAGES_OFFSET (CCVFS_INDEX_TABLE_OFFSET + CCVFS_INDEX_TABLE_SIZE)  // Start of data pages

// Page flags
#define CCVFS_PAGE_COMPRESSED   (1 << 0)
#define CCVFS_PAGE_ENCRYPTED    (1 << 1) 
#define CCVFS_PAGE_SPARSE       (1 << 2)
#define CCVFS_COMPRESSION_LEVEL_MASK (0xFF << 8)
#define CCVFS_COMPRESSION_LEVEL_SHIFT 8

// Creation flags
#define CCVFS_CREATE_REALTIME    (1 << 0)
#define CCVFS_CREATE_OFFLINE     (1 << 1)
#define CCVFS_CREATE_HYBRID      (1 << 2)

// Hole detection configuration constants
#define CCVFS_DEFAULT_MAX_HOLES     256     // Default maximum holes to track
#define CCVFS_MIN_MAX_HOLES         16      // Minimum allowed max holes
#define CCVFS_MAX_MAX_HOLES         1024    // Maximum allowed max holes
#define CCVFS_DEFAULT_MIN_HOLE_SIZE 64      // Default minimum hole size (bytes)
#define CCVFS_MIN_HOLE_SIZE         16      // Minimum allowed hole size
#define CCVFS_MAX_HOLE_SIZE         4096    // Maximum allowed hole size

// Write buffer configuration constants
#define CCVFS_DEFAULT_BUFFER_ENABLED      1        // Enable write buffering by default
#define CCVFS_DEFAULT_MAX_BUFFER_ENTRIES  32       // Default maximum buffered pages
#define CCVFS_MIN_BUFFER_ENTRIES          4        // Minimum buffer entries
#define CCVFS_MAX_BUFFER_ENTRIES          1024     // Maximum buffer entries
#define CCVFS_DEFAULT_MAX_BUFFER_SIZE     (4*1024*1024) // 4MB default buffer size
#define CCVFS_MIN_BUFFER_SIZE             (256*1024)    // 256KB minimum buffer
#define CCVFS_MAX_BUFFER_SIZE             (64*1024*1024) // 64MB maximum buffer
#define CCVFS_DEFAULT_AUTO_FLUSH_PAGES    16       // Auto flush every 16 pages

/*
 * File header structure (128 bytes)
 */
typedef struct {
    // Basic identification (16 bytes)
    char magic[8];              // "CCVFSDB\0"
    uint16_t major_version;     // Major version (current: 1)
    uint16_t minor_version;     // Minor version (current: 0)
    uint32_t header_size;       // Header size (128)
    
    // SQLite compatibility info (16 bytes) - 与SQLite页面兼容性信息
    uint32_t original_page_size;    // Original SQLite page size (原始SQLite页面大小)
    uint32_t sqlite_version;        // Original SQLite version
    uint32_t database_size_pages;   // Total database pages (数据库总页数)
    uint32_t reserved1;
    
    // Compression configuration (24 bytes)
    char compress_algorithm[CCVFS_MAX_ALGORITHM_NAME];    // Compression algorithm name
    char encrypt_algorithm[CCVFS_MAX_ALGORITHM_NAME];     // Encryption algorithm name
    
    // Page configuration (16 bytes) - 页面配置信息
    uint32_t page_size;           // Logical page size (逻辑页面大小)
    uint32_t total_pages;         // Total number of pages (总页数)
    uint64_t index_table_offset;   // Page index table offset (页面索引表偏移)
    
    // Statistics (24 bytes)
    uint64_t original_file_size;   // Original file size
    uint64_t compressed_file_size; // Compressed file size
    uint32_t compression_ratio;    // Compression ratio (percentage)
    uint32_t creation_flags;       // Creation flags
    
    // Checksum and security (16 bytes)
    uint32_t header_checksum;      // File header checksum
    uint32_t master_key_hash;      // Master key hash (optional)
    uint64_t timestamp;            // Creation timestamp
    
    // Reserved fields (16 bytes)
    uint8_t reserved[16];          // Reserved for extension
} CCVFSFileHeader;

/*
 * Page index entry - 页面索引条目
 */
typedef struct {
    uint64_t physical_offset;      // Physical file offset (物理文件偏移)
    uint32_t compressed_size;      // Compressed size (压缩后大小)
    uint32_t original_size;        // Original size (原始大小)
    uint32_t checksum;             // Page checksum (页面校验和)
    uint32_t flags;                // Page flags (页面标志)
} CCVFSPageIndex;

/*
 * Data page structure - 数据页结构
 */
typedef struct {
    // Page header (32 bytes) - 页面头部
    uint32_t magic;                // Page magic: 0x50434356 ("PCCV")
    uint32_t page_number;         // Logical page number (逻辑页编号)
    uint32_t original_size;        // Original data size (原始数据大小)
    uint32_t compressed_size;      // Compressed data size (压缩数据大小)
    uint32_t checksum;             // Data checksum (CRC32) (数据校验和)
    uint32_t flags;                // Page flags (页面标志)
    uint64_t timestamp;            // Page update timestamp (页面更新时间戳)
    uint64_t sequence_number;      // Sequence number for transaction consistency (事务一致性序列号)
    
    // Variable length data follows - 可变长度数据跟随
    // uint8_t data[];             // Compressed data (压缩数据)
} CCVFSDataPage;

/*
 * Space hole structure for tracking available space - 空间洞结构用于跟踪可用空间
 */
typedef struct CCVFSSpaceHole {
    sqlite3_int64 offset;           // Starting offset of the hole (空洞起始偏移)
    uint32_t size;                  // Size of the hole in bytes (空洞大小，字节)
    struct CCVFSSpaceHole *next;    // Next hole in the list (链表中的下一个空洞)
} CCVFSSpaceHole;

/*
 * Hole manager structure for efficient space allocation - 空洞管理器结构用于高效空间分配
 */
typedef struct CCVFSHoleManager {
    CCVFSSpaceHole *holes;          // Linked list of holes (空洞链表)
    uint32_t hole_count;            // Number of tracked holes (跟踪的空洞数量)
    uint32_t max_holes;             // Maximum holes to track (最大跟踪空洞数)
    uint32_t min_hole_size;         // Minimum hole size to track (最小跟踪空洞大小)
    int enabled;                    // Whether hole detection is enabled (是否启用空洞检测)
} CCVFSHoleManager;

/*
 * Buffered write entry for batch operations - 批量操作的缓冲写入项
 */
typedef struct CCVFSBufferEntry {
    uint32_t page_number;           // Page number to write (要写入的页编号)
    unsigned char *data;            // Page data (页数据)
    uint32_t data_size;            // Size of page data (页数据大小)
    int is_dirty;                  // Whether this entry needs to be written (此条目是否需要写入)
    struct CCVFSBufferEntry *next; // Next entry in buffer list (缓冲区列表中的下一项)
} CCVFSBufferEntry;

/*
 * Write buffer manager for batch write operations - 批量写入操作的写入缓冲区管理器
 */
typedef struct CCVFSWriteBuffer {
    CCVFSBufferEntry *entries;      // Linked list of buffered entries (缓冲条目链表)
    uint32_t entry_count;           // Number of buffered entries (缓冲条目数量)
    uint32_t max_entries;           // Maximum entries to buffer (最大缓冲条目数)
    uint32_t buffer_size;           // Current buffer memory usage (当前缓冲区内存使用量)
    uint32_t max_buffer_size;       // Maximum buffer size in bytes (最大缓冲区大小，字节)
    int enabled;                    // Whether write buffering is enabled (是否启用写入缓冲)
    int auto_flush_pages;           // Auto flush when this many pages buffered (缓冲这么多页时自动刷新)
    sqlite3_int64 last_flush_time;  // Last flush timestamp (上次刷新时间戳)
} CCVFSWriteBuffer;

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
    int key_size;               // Required key size
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
 * Configure batch writer for a VFS
 * Parameters:
 *   zVfsName - Name of the VFS to configure
 *   enabled - Whether to enable batch writing (0 or 1)
 *   max_pages - Maximum number of pages to buffer (0 for default)
 *   max_memory_mb - Maximum memory usage in MB (0 for default)
 *   auto_flush_threshold - Auto flush threshold in pages (0 for default)
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_configure_batch_writer(
    const char *zVfsName,
    int enabled,
    uint32_t max_pages,
    uint32_t max_memory_mb,
    uint32_t auto_flush_threshold
);

/*
 * Get batch writer statistics for an open database
 * Parameters:
 *   db - Open database connection
 *   hits - Batch writer hit count (output)
 *   flushes - Batch writer flush count (output)
 *   merges - Batch writer merge count (output)
 *   total_writes - Total batch writes count (output)
 *   memory_used - Current memory usage in bytes (output)
 *   page_count - Current buffered page count (output)
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_get_batch_writer_stats(
    sqlite3 *db,
    uint32_t *hits,
    uint32_t *flushes,
    uint32_t *merges,
    uint32_t *total_writes,
    uint32_t *memory_used,
    uint32_t *page_count
);

/*
 * Force flush batch writer for an open database
 * Parameters:
 *   db - Open database connection
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_flush_batch_writer(sqlite3 *db);

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