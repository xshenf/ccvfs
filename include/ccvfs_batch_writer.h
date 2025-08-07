/*
 * CCVFS Improved Batch Writer Interface
 * 
 * This header defines the improved batch writing system that addresses
 * the data consistency and performance issues of the original buffer design.
 */

#ifndef CCVFS_BATCH_WRITER_H
#define CCVFS_BATCH_WRITER_H

#include "ccvfs_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct CCVFSFile CCVFSFile;

/*
 * Processed page entry - stores pre-compressed and validated data
 */
typedef struct CCVFSProcessedPage {
    uint32_t page_number;
    uint32_t original_size;
    uint32_t processed_size;
    uint32_t checksum;
    uint32_t flags;
    
    unsigned char *processed_data;  // Compressed + encrypted data
    unsigned char *original_data;   // Original data for read hits
    
    int is_dirty;
    time_t created_time;
    struct CCVFSProcessedPage *next;
} CCVFSProcessedPage;

/*
 * Batch writer configuration and state
 */
typedef struct CCVFSBatchWriter {
    // Configuration
    int enabled;
    uint32_t max_pages;              // Maximum pages to buffer
    uint32_t max_memory_mb;          // Memory limit in MB
    uint32_t auto_flush_threshold;   // Auto-flush when this many pages buffered
    
    // State
    CCVFSProcessedPage *pages;       // Linked list of processed pages
    uint32_t page_count;             // Current number of pages
    uint32_t total_memory_used;      // Current memory usage in bytes
    time_t last_flush_time;
    
    // Statistics
    uint32_t hits;                   // Read hits from batch writer
    uint32_t flushes;                // Number of batch flushes
    uint32_t merges;                 // Page merge operations
    uint32_t total_writes;           // Total pages written to batch writer
    uint32_t batch_allocations;      // Number of batch space allocations
    uint32_t rollbacks;              // Number of rollback operations
} CCVFSBatchWriter;

/*
 * Batch space allocation info
 */
typedef struct CCVFSBatchAllocation {
    sqlite3_int64 base_offset;       // Starting offset of batch
    uint32_t total_size;             // Total size allocated
    uint32_t used_size;              // Actually used size
    uint32_t page_count;             // Number of pages in batch
} CCVFSBatchAllocation;

// ============================================================================
// BATCH WRITER CORE FUNCTIONS
// ============================================================================

/*
 * Initialize batch writer with configuration
 */
int ccvfs_init_batch_writer(CCVFSFile *pFile);

/*
 * Cleanup batch writer and free resources
 */
void ccvfs_cleanup_batch_writer(CCVFSFile *pFile);

/*
 * Write page data to batch writer (with immediate processing)
 */
int ccvfs_batch_write_page(CCVFSFile *pFile, uint32_t pageNum, 
                          const unsigned char *data, uint32_t dataSize);

/*
 * Read page data from batch writer if available
 */
int ccvfs_batch_read_page(CCVFSFile *pFile, uint32_t pageNum, 
                         unsigned char *buffer, uint32_t bufferSize);

/*
 * Flush all pages in batch writer to disk
 */
int ccvfs_flush_batch_writer(CCVFSFile *pFile);

/*
 * Force flush a specific page from batch writer
 */
int ccvfs_flush_batch_page(CCVFSFile *pFile, uint32_t pageNum);

// ============================================================================
// BATCH WRITER HELPER FUNCTIONS
// ============================================================================

/*
 * Process page data (compress + encrypt + checksum)
 */
CCVFSProcessedPage* ccvfs_process_page_data(CCVFSFile *pFile, uint32_t pageNum,
                                           const unsigned char *data, uint32_t dataSize);

/*
 * Find processed page in batch writer
 */
CCVFSProcessedPage* ccvfs_find_processed_page(CCVFSBatchWriter *pWriter, uint32_t pageNum);

/*
 * Add processed page to batch writer
 */
int ccvfs_add_processed_page(CCVFSBatchWriter *pWriter, CCVFSProcessedPage *pPage);

/*
 * Replace existing processed page
 */
int ccvfs_replace_processed_page(CCVFSBatchWriter *pWriter, 
                                CCVFSProcessedPage *existing, 
                                CCVFSProcessedPage *newPage);

/*
 * Remove processed page from batch writer
 */
int ccvfs_remove_processed_page(CCVFSBatchWriter *pWriter, uint32_t pageNum);

/*
 * Check if batch writer should auto-flush
 */
int ccvfs_should_auto_flush(CCVFSBatchWriter *pWriter);

/*
 * Calculate total size needed for batch flush
 */
uint32_t ccvfs_calculate_batch_size(CCVFSBatchWriter *pWriter);

/*
 * Clear all pages from batch writer
 */
void ccvfs_clear_batch_writer(CCVFSBatchWriter *pWriter);

// ============================================================================
// BATCH SPACE ALLOCATION FUNCTIONS
// ============================================================================

/*
 * Allocate contiguous space for batch write
 */
int ccvfs_allocate_batch_space(CCVFSFile *pFile, uint32_t totalSize, 
                              CCVFSBatchAllocation *pAllocation);

/*
 * Write all batch pages to allocated space
 */
int ccvfs_write_batch_pages(CCVFSFile *pFile, CCVFSBatchWriter *pWriter, 
                           CCVFSBatchAllocation *pAllocation);

/*
 * Update page index for all batch pages
 */
int ccvfs_update_batch_index(CCVFSFile *pFile, CCVFSBatchWriter *pWriter, 
                            CCVFSBatchAllocation *pAllocation);

/*
 * Rollback batch space allocation on error
 */
int ccvfs_rollback_batch_space(CCVFSFile *pFile, CCVFSBatchAllocation *pAllocation);

/*
 * Find large hole suitable for batch allocation
 */
sqlite3_int64 ccvfs_find_large_hole(CCVFSFile *pFile, uint32_t requiredSize);

/*
 * Build contiguous data block for batch write
 */
unsigned char* ccvfs_build_batch_data(CCVFSBatchWriter *pWriter, uint32_t *pTotalSize);

// ============================================================================
// BATCH WRITER STATISTICS AND MONITORING
// ============================================================================

/*
 * Get batch writer statistics
 */
int ccvfs_get_batch_writer_stats(CCVFSFile *pFile,
                                uint32_t *hits,
                                uint32_t *flushes, 
                                uint32_t *merges,
                                uint32_t *total_writes,
                                uint32_t *memory_used,
                                uint32_t *page_count);

/*
 * Reset batch writer statistics
 */
void ccvfs_reset_batch_writer_stats(CCVFSFile *pFile);

/*
 * Get batch writer memory usage
 */
uint32_t ccvfs_get_batch_writer_memory_usage(CCVFSFile *pFile);

/*
 * Check batch writer health
 */
int ccvfs_check_batch_writer_health(CCVFSFile *pFile);

// ============================================================================
// CONFIGURATION AND TUNING
// ============================================================================

/*
 * Configure batch writer parameters
 */
int ccvfs_configure_batch_writer(CCVFSFile *pFile,
                                int enabled,
                                uint32_t max_pages,
                                uint32_t max_memory_mb,
                                uint32_t auto_flush_threshold);

/*
 * Get current batch writer configuration
 */
int ccvfs_get_batch_writer_config(CCVFSFile *pFile,
                                 int *enabled,
                                 uint32_t *max_pages,
                                 uint32_t *max_memory_mb,
                                 uint32_t *auto_flush_threshold);

// ============================================================================
// CONSTANTS AND DEFAULTS
// ============================================================================

// Default configuration values
#define CCVFS_BATCH_WRITER_DEFAULT_MAX_PAGES        64
#define CCVFS_BATCH_WRITER_DEFAULT_MAX_MEMORY_MB    8
#define CCVFS_BATCH_WRITER_DEFAULT_AUTO_FLUSH       32

// Limits
#define CCVFS_BATCH_WRITER_MIN_MAX_PAGES            4
#define CCVFS_BATCH_WRITER_MAX_MAX_PAGES            1024
#define CCVFS_BATCH_WRITER_MIN_MEMORY_MB            1
#define CCVFS_BATCH_WRITER_MAX_MEMORY_MB            256

// Memory calculation helpers
#define CCVFS_BATCH_WRITER_MB_TO_BYTES(mb)          ((mb) * 1024 * 1024)
#define CCVFS_BATCH_WRITER_BYTES_TO_MB(bytes)       ((bytes) / (1024 * 1024))

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_BATCH_WRITER_H */