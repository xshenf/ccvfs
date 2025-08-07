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
 * CCVFS structure
 */
typedef struct CCVFS {
    sqlite3_vfs base;           /* Base VFS structure */
    sqlite3_vfs *pRootVfs;      /* Underlying VFS */
    char *zCompressType;        /* Compression algorithm type */
    char *zEncryptType;         /* Encryption algorithm type */
    CompressAlgorithm *pCompressAlg; /* Compression algorithm implementation */
    EncryptAlgorithm *pEncryptAlg;   /* Encryption algorithm implementation */
    uint32_t creation_flags;    /* Creation flags */
    uint32_t page_size;        /* Page size in bytes (页面大小，对应SQLite页面大小) */
    
    // 数据完整性和错误处理配置
    // Data integrity and error handling configuration
    int strict_checksum_mode;   /* 严格校验和模式：1=严格，0=容错 Strict checksum mode: 1=strict, 0=tolerant */
    int enable_data_recovery;   /* 启用数据恢复策略 Enable data recovery strategies */
    int corruption_tolerance;   /* 数据损坏容忍级别 (0-100) Data corruption tolerance level (0-100) */
    
    // 空洞检测配置
    // Hole detection configuration
    int enable_hole_detection;  /* 启用空洞检测 Enable hole detection */
    uint32_t max_holes;         /* 最大跟踪空洞数 Maximum holes to track */
    uint32_t min_hole_size;     /* 最小跟踪空洞大小 Minimum hole size to track */
    
    // 写入缓冲配置
    // Write buffer configuration
    int enable_write_buffer;    /* 启用写入缓冲 Enable write buffering */
    uint32_t max_buffer_entries; /* 最大缓冲条目数 Maximum buffer entries */
    uint32_t max_buffer_size;   /* 最大缓冲区大小 Maximum buffer size in bytes */
    uint32_t auto_flush_pages;  /* 自动刷新页数阈值 Auto flush page threshold */
} CCVFS;

/*
 * CCVFS file structure - CCVFS文件结构
 */
typedef struct CCVFSFile {
    sqlite3_file base;          /* Base file structure */
    sqlite3_file *pReal;        /* Actual file pointer */
    CCVFS *pOwner;              /* Owner VFS */
    CCVFSFileHeader header;     /* Cached file header */
    CCVFSPageIndex *pPageIndex; /* Page index table (in-memory) 页面索引表（内存中） */
    int index_dirty;              /* 1 if index needs to be saved */
    uint32_t index_capacity;      /* Allocated capacity for page index */
    int header_loaded;          /* Header loaded flag */
    int open_flags;             /* File open flags */
    int is_ccvfs_file;          /* Is this a CCVFS format file */
    char *filename;             /* File path for debugging */
    
    // Space utilization tracking - 空间利用跟踪
    uint64_t total_allocated_space;   /* Total space allocated for data pages */
    uint64_t total_used_space;        /* Total space actually used by compressed data */
    uint32_t fragmentation_score;     /* Fragmentation score (0-100, higher = more fragmented) */
    uint32_t space_reuse_count;       /* Number of successful space reuses */
    uint32_t space_expansion_count;   /* Number of space expansions */
    uint32_t new_allocation_count;    /* Number of new space allocations */
    
    // Advanced space management - 高级空间管理
    uint32_t hole_reclaim_count;      /* Number of space holes reclaimed */
    uint32_t best_fit_count;          /* Number of best-fit allocations */
    uint32_t sequential_write_count;  /* Number of sequential writes detected */
    uint32_t last_written_page;      /* Last page number written (for sequential detection) */
    
    // 空洞管理器和统计
    // Hole manager and statistics
    CCVFSHoleManager hole_manager;    /* Hole tracking system */
    uint32_t hole_allocation_count;   /* Number of successful hole allocations */
    uint32_t hole_merge_count;        /* Number of hole merge operations */
    uint32_t hole_cleanup_count;      /* Number of small holes removed */
    uint32_t hole_operations_count;   /* Counter for triggering maintenance */
    
    // 写入缓冲管理器和统计
    // Write buffer manager and statistics
    CCVFSWriteBuffer write_buffer;    /* Write buffering system */
    uint32_t buffer_hit_count;        /* Number of buffer hits during reads */
    uint32_t buffer_flush_count;      /* Number of buffer flushes performed */
    uint32_t buffer_merge_count;      /* Number of write merges in buffer */
    uint32_t total_buffered_writes;   /* Total writes that went through buffer */
    
    // 数据完整性统计和错误跟踪
    // Data integrity statistics and error tracking
    uint32_t checksum_error_count;    /* 校验和错误次数 Number of checksum errors encountered */
    uint32_t corrupted_page_count;   /* 损坏页数量 Number of corrupted pages detected */
    uint32_t recovery_attempt_count;  /* 数据恢复尝试次数 Number of data recovery attempts */
    uint32_t successful_recovery_count; /* 成功恢复次数 Number of successful recoveries */
} CCVFSFile;

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_INTERNAL_H */