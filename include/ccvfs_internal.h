#ifndef CCVFS_INTERNAL_H
#define CCVFS_INTERNAL_H

#include "ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Internal constants
#define CCVFS_MAX_ALGORITHMS 16
#define CCVFS_CRC32_POLYNOMIAL 0xEDB88320

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

// Debug macro definitions
#ifdef DEBUG
#define CCVFS_DEBUG(fmt, ...) fprintf(stdout, "[CCVFS DEBUG] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define CCVFS_DEBUG(fmt, ...)
#endif

#ifdef VERBOSE
#define CCVFS_VERBOSE(fmt, ...) fprintf(stdout, "[CCVFS VERBOSE] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define CCVFS_VERBOSE(fmt, ...)
#endif

#define CCVFS_INFO(fmt, ...) fprintf(stdout, "[CCVFS INFO] " fmt "\n", ##__VA_ARGS__)
#define CCVFS_ERROR(fmt, ...) fprintf(stderr, "[CCVFS ERROR] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

/*
 * File header structure (128 bytes)
 */
typedef struct {
    // Basic identification (16 bytes)
    char magic[8]; // "CCVFSDB\0"
    uint16_t major_version; // Major version (current: 1)
    uint16_t minor_version; // Minor version (current: 0)
    uint32_t header_size; // Header size (128)

    // SQLite compatibility info (16 bytes) - 与SQLite页面兼容性信息
    uint32_t original_page_size; // Original SQLite page size (原始SQLite页面大小)
    uint32_t sqlite_version; // Original SQLite version
    uint32_t database_size_pages; // Total database pages (数据库总页数)
    uint32_t reserved1;

    // Compression configuration (24 bytes)
    char compress_algorithm[CCVFS_MAX_ALGORITHM_NAME]; // Compression algorithm name
    char encrypt_algorithm[CCVFS_MAX_ALGORITHM_NAME]; // Encryption algorithm name

    // Page configuration (16 bytes) - 页面配置信息
    uint32_t page_size; // Logical page size (逻辑页面大小)
    uint32_t total_pages; // Total number of pages (总页数)
    uint64_t index_table_offset; // Page index table offset (页面索引表偏移)

    // Statistics (24 bytes)
    uint64_t original_file_size; // Original file size
    uint64_t compressed_file_size; // Compressed file size
    uint32_t compression_ratio; // Compression ratio (percentage)
    uint32_t creation_flags; // Creation flags

    // Checksum and security (16 bytes)
    uint32_t header_checksum; // File header checksum
    uint32_t master_key_hash; // Master key hash (optional)
    uint64_t timestamp; // Creation timestamp

    // Reserved fields (16 bytes)
    uint8_t reserved[16]; // Reserved for extension
} CCVFSFileHeader;

/*
 * Page index entry - 页面索引条目
 */
typedef struct {
    uint64_t physical_offset; // Physical file offset (物理文件偏移)
    uint32_t compressed_size; // Compressed size (压缩后大小)
    uint32_t original_size; // Original size (原始大小)
    uint32_t checksum; // Page checksum (页面校验和)
    uint32_t flags; // Page flags (页面标志)
} CCVFSPageIndex;

/*
 * Data page structure - 数据页结构
 */
typedef struct {
    // Page header (32 bytes) - 页面头部
    uint32_t magic; // Page magic: 0x50434356 ("PCCV")
    uint32_t page_number; // Logical page number (逻辑页编号)
    uint32_t original_size; // Original data size (原始数据大小)
    uint32_t compressed_size; // Compressed data size (压缩数据大小)
    uint32_t checksum; // Data checksum (CRC32) (数据校验和)
    uint32_t flags; // Page flags (页面标志)
    uint64_t timestamp; // Page update timestamp (页面更新时间戳)
    uint64_t sequence_number; // Sequence number for transaction consistency (事务一致性序列号)

    // Variable length data follows - 可变长度数据跟随
    // uint8_t data[];             // Compressed data (压缩数据)
} CCVFSDataPage;

/*
 * Space hole structure for tracking available space - 空间洞结构用于跟踪可用空间
 */
typedef struct CCVFSSpaceHole {
    sqlite3_int64 offset; // Starting offset of the hole (空洞起始偏移)
    uint32_t size; // Size of the hole in bytes (空洞大小，字节)
    struct CCVFSSpaceHole *next; // Next hole in the list (链表中的下一个空洞)
} CCVFSSpaceHole;

/*
 * Hole manager structure for efficient space allocation - 空洞管理器结构用于高效空间分配
 */
typedef struct CCVFSHoleManager {
    CCVFSSpaceHole *holes; // Linked list of holes (空洞链表)
    uint32_t hole_count; // Number of tracked holes (跟踪的空洞数量)
    uint32_t max_holes; // Maximum holes to track (最大跟踪空洞数)
    uint32_t min_hole_size; // Minimum hole size to track (最小跟踪空洞大小)
    int enabled; // Whether hole detection is enabled (是否启用空洞检测)
} CCVFSHoleManager;

/*
 * Buffered write entry for batch operations - 批量操作的缓冲写入项
 */
typedef struct CCVFSBufferEntry {
    uint32_t page_number; // Page number to write (要写入的页编号)
    unsigned char *data; // Page data (页数据)
    uint32_t data_size; // Size of page data (页数据大小)
    int is_dirty; // Whether this entry needs to be written (此条目是否需要写入)
    struct CCVFSBufferEntry *next; // Next entry in buffer list (缓冲区列表中的下一项)
} CCVFSBufferEntry;

/*
 * Write buffer manager for batch write operations - 批量写入操作的写入缓冲区管理器
 */
typedef struct CCVFSWriteBuffer {
    CCVFSBufferEntry *entries; // Linked list of buffered entries (缓冲条目链表)
    uint32_t entry_count; // Number of buffered entries (缓冲条目数量)
    uint32_t max_entries; // Maximum entries to buffer (最大缓冲条目数)
    uint32_t buffer_size; // Current buffer memory usage (当前缓冲区内存使用量)
    uint32_t max_buffer_size; // Maximum buffer size in bytes (最大缓冲区大小，字节)
    int enabled; // Whether write buffering is enabled (是否启用写入缓冲)
    int auto_flush_pages; // Auto flush when this many pages buffered (缓冲这么多页时自动刷新)
    sqlite3_int64 last_flush_time; // Last flush timestamp (上次刷新时间戳)
} CCVFSWriteBuffer;

/*
 * CCVFS structure
 */
typedef struct CCVFS {
    sqlite3_vfs base; /* Base VFS structure */
    sqlite3_vfs *pRootVfs; /* Underlying VFS */
    char *zCompressType; /* Compression algorithm type */
    char *zEncryptType; /* Encryption algorithm type */
    CompressAlgorithm *pCompressAlg; /* Compression algorithm implementation */
    EncryptAlgorithm *pEncryptAlg; /* Encryption algorithm implementation */
    uint32_t creation_flags; /* Creation flags */
    uint32_t page_size; /* Page size in bytes (页面大小，对应SQLite页面大小) */

    // 数据完整性和错误处理配置
    // Data integrity and error handling configuration
    int strict_checksum_mode; /* 严格校验和模式：1=严格，0=容错 Strict checksum mode: 1=strict, 0=tolerant */
    int enable_data_recovery; /* 启用数据恢复策略 Enable data recovery strategies */
    int corruption_tolerance; /* 数据损坏容忍级别 (0-100) Data corruption tolerance level (0-100) */

    // 空洞检测配置
    // Hole detection configuration
    int enable_hole_detection; /* 启用空洞检测 Enable hole detection */
    uint32_t max_holes; /* 最大跟踪空洞数 Maximum holes to track */
    uint32_t min_hole_size; /* 最小跟踪空洞大小 Minimum hole size to track */

    // 写入缓冲配置
    // Write buffer configuration
    int enable_write_buffer; /* 启用写入缓冲 Enable write buffering */
    uint32_t max_buffer_entries; /* 最大缓冲条目数 Maximum buffer entries */
    uint32_t max_buffer_size; /* 最大缓冲区大小 Maximum buffer size in bytes */
    uint32_t auto_flush_pages; /* 自动刷新页数阈值 Auto flush page threshold */
} CCVFS;

/*
 * CCVFS file structure - CCVFS文件结构
 */
typedef struct CCVFSFile {
    sqlite3_file base; /* Base file structure */
    sqlite3_file *pReal; /* Actual file pointer */
    CCVFS *pOwner; /* Owner VFS */
    CCVFSFileHeader header; /* Cached file header */
    CCVFSPageIndex *pPageIndex; /* Page index table (in-memory) 页面索引表（内存中） */
    int index_dirty; /* 1 if index needs to be saved */
    uint32_t index_capacity; /* Allocated capacity for page index */
    int header_loaded; /* Header loaded flag */
    int open_flags; /* File open flags */
    int is_ccvfs_file; /* Is this a CCVFS format file */
    char *filename; /* File path for debugging */

    // Space utilization tracking - 空间利用跟踪
    uint64_t total_allocated_space; /* Total space allocated for data pages */
    uint64_t total_used_space; /* Total space actually used by compressed data */
    uint32_t fragmentation_score; /* Fragmentation score (0-100, higher = more fragmented) */
    uint32_t space_reuse_count; /* Number of successful space reuses */
    uint32_t space_expansion_count; /* Number of space expansions */
    uint32_t new_allocation_count; /* Number of new space allocations */

    // Advanced space management - 高级空间管理
    uint32_t hole_reclaim_count; /* Number of space holes reclaimed */
    uint32_t best_fit_count; /* Number of best-fit allocations */
    uint32_t sequential_write_count; /* Number of sequential writes detected */
    uint32_t last_written_page; /* Last page number written (for sequential detection) */

    // 空洞管理器和统计
    // Hole manager and statistics
    CCVFSHoleManager hole_manager; /* Hole tracking system */
    uint32_t hole_allocation_count; /* Number of successful hole allocations */
    uint32_t hole_merge_count; /* Number of hole merge operations */
    uint32_t hole_cleanup_count; /* Number of small holes removed */
    uint32_t hole_operations_count; /* Counter for triggering maintenance */

    // 写入缓冲管理器和统计
    // Write buffer manager and statistics
    CCVFSWriteBuffer write_buffer; /* Write buffering system */
    uint32_t buffer_hit_count; /* Number of buffer hits during reads */
    uint32_t buffer_flush_count; /* Number of buffer flushes performed */
    uint32_t buffer_merge_count; /* Number of write merges in buffer */
    uint32_t total_buffered_writes; /* Total writes that went through buffer */

    // 数据完整性统计和错误跟踪
    // Data integrity statistics and error tracking
    uint32_t checksum_error_count; /* 校验和错误次数 Number of checksum errors encountered */
    uint32_t corrupted_page_count; /* 损坏页数量 Number of corrupted pages detected */
    uint32_t recovery_attempt_count; /* 数据恢复尝试次数 Number of data recovery attempts */
    uint32_t successful_recovery_count; /* 成功恢复次数 Number of successful recoveries */
} CCVFSFile;

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_INTERNAL_H */
