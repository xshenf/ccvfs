/*
 * CCVFS Improved Batch Writer Implementation
 * 
 * This implementation provides a more robust batch writing system that
 * addresses data consistency and performance issues.
 */

#include "ccvfs_batch_writer.h"
#include "ccvfs_io.h"
#include "ccvfs_utils.h"
#include <string.h>
#include <time.h>

// ============================================================================
// BATCH WRITER CORE FUNCTIONS
// ============================================================================

/*
 * Initialize batch writer with configuration
 */
int ccvfs_init_batch_writer(CCVFSFile *pFile) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    
    CCVFS_DEBUG("Initializing batch writer for file: %s", 
               pFile->filename ? pFile->filename : "unknown");
    
    // Clear the structure
    memset(pWriter, 0, sizeof(CCVFSBatchWriter));
    
    // Set default configuration
    pWriter->enabled = 1;
    pWriter->max_pages = CCVFS_BATCH_WRITER_DEFAULT_MAX_PAGES;
    pWriter->max_memory_mb = CCVFS_BATCH_WRITER_DEFAULT_MAX_MEMORY_MB;
    pWriter->auto_flush_threshold = CCVFS_BATCH_WRITER_DEFAULT_AUTO_FLUSH;
    
    // Apply VFS-level configuration if available
    if (pFile->pOwner) {
        pWriter->enabled = pFile->pOwner->enable_write_buffer;
        if (pFile->pOwner->write_buffer_max_entries > 0) {
            pWriter->max_pages = pFile->pOwner->write_buffer_max_entries;
        }
        if (pFile->pOwner->write_buffer_auto_flush_pages > 0) {
            pWriter->auto_flush_threshold = pFile->pOwner->write_buffer_auto_flush_pages;
        }
    }
    
    pWriter->last_flush_time = time(NULL);
    
    CCVFS_INFO("Batch writer initialized: enabled=%d, max_pages=%u, max_memory=%uMB, auto_flush=%u",
               pWriter->enabled, pWriter->max_pages, pWriter->max_memory_mb, pWriter->auto_flush_threshold);
    
    return SQLITE_OK;
}

/*
 * Cleanup batch writer and free resources
 */
void ccvfs_cleanup_batch_writer(CCVFSFile *pFile) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    
    if (!pWriter->enabled) {
        return;
    }
    
    CCVFS_DEBUG("Cleaning up batch writer: %u pages buffered", pWriter->page_count);
    
    // Flush any remaining pages
    if (pWriter->page_count > 0) {
        CCVFS_DEBUG("Flushing %u remaining pages before cleanup", pWriter->page_count);
        ccvfs_flush_batch_writer(pFile);
    }
    
    // Free any remaining processed pages
    ccvfs_clear_batch_writer(pWriter);
    
    // Print final statistics
    CCVFS_INFO("Batch writer cleanup stats: hits=%u, flushes=%u, merges=%u, total_writes=%u",
               pWriter->hits, pWriter->flushes, pWriter->merges, pWriter->total_writes);
    
    // Clear the structure
    memset(pWriter, 0, sizeof(CCVFSBatchWriter));
}

/*
 * Write page data to batch writer (with immediate processing)
 */
int ccvfs_batch_write_page(CCVFSFile *pFile, uint32_t pageNum, 
                          const unsigned char *data, uint32_t dataSize) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    
    CCVFS_DEBUG("Batch writing page %u, size %u bytes", pageNum, dataSize);
    
    if (!pWriter->enabled) {
        CCVFS_DEBUG("Batch writer disabled, not buffering page %u", pageNum);
        return SQLITE_NOTFOUND;  // Caller should write directly
    }
    
    // Check memory limits before processing
    uint32_t estimated_memory = dataSize * 2;  // Original + processed data
    if (pWriter->total_memory_used + estimated_memory > 
        CCVFS_BATCH_WRITER_MB_TO_BYTES(pWriter->max_memory_mb)) {
        CCVFS_DEBUG("Memory limit reached, flushing before new page");
        int rc = ccvfs_flush_batch_writer(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to flush batch writer for memory limit: %d", rc);
            return rc;
        }
    }
    
    // Process page data immediately (compress + encrypt + checksum)
    CCVFSProcessedPage *pPage = ccvfs_process_page_data(pFile, pageNum, data, dataSize);
    if (!pPage) {
        CCVFS_ERROR("Failed to process page %u data", pageNum);
        return SQLITE_NOMEM;
    }
    
    // Check if page already exists in batch writer
    CCVFSProcessedPage *existing = ccvfs_find_processed_page(pWriter, pageNum);
    if (existing) {
        CCVFS_DEBUG("Merging with existing page %u in batch writer", pageNum);
        int rc = ccvfs_replace_processed_page(pWriter, existing, pPage);
        if (rc != SQLITE_OK) {
            // Free the new page on error
            if (pPage->processed_data) sqlite3_free(pPage->processed_data);
            if (pPage->original_data) sqlite3_free(pPage->original_data);
            sqlite3_free(pPage);
            return rc;
        }
        pWriter->merges++;
    } else {
        CCVFS_DEBUG("Adding new page %u to batch writer", pageNum);
        int rc = ccvfs_add_processed_page(pWriter, pPage);
        if (rc != SQLITE_OK) {
            // Free the page on error
            if (pPage->processed_data) sqlite3_free(pPage->processed_data);
            if (pPage->original_data) sqlite3_free(pPage->original_data);
            sqlite3_free(pPage);
            return rc;
        }
    }
    
    pWriter->total_writes++;
    
    CCVFS_DEBUG("Page %u added to batch writer: pages=%u, memory=%u bytes", 
               pageNum, pWriter->page_count, pWriter->total_memory_used);
    
    // Check if we should auto-flush
    if (ccvfs_should_auto_flush(pWriter)) {
        CCVFS_DEBUG("Auto-flush triggered: %u pages >= %u threshold", 
                   pWriter->page_count, pWriter->auto_flush_threshold);
        return ccvfs_flush_batch_writer(pFile);
    }
    
    return SQLITE_OK;
}

/*
 * Read page data from batch writer if available
 */
int ccvfs_batch_read_page(CCVFSFile *pFile, uint32_t pageNum, 
                         unsigned char *buffer, uint32_t bufferSize) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    
    CCVFS_DEBUG("Checking batch writer for page %u read", pageNum);
    
    if (!pWriter->enabled) {
        return SQLITE_NOTFOUND;
    }
    
    // Find the page in batch writer
    CCVFSProcessedPage *pPage = ccvfs_find_processed_page(pWriter, pageNum);
    if (!pPage) {
        CCVFS_DEBUG("Page %u not found in batch writer", pageNum);
        return SQLITE_NOTFOUND;
    }
    
    // Return original data directly (no decompression needed)
    if (!pPage->original_data) {
        CCVFS_ERROR("Page %u in batch writer has no original data", pageNum);
        return SQLITE_CORRUPT;
    }
    
    if (pPage->original_size > bufferSize) {
        CCVFS_ERROR("Page %u original size %u > buffer size %u", 
                   pageNum, pPage->original_size, bufferSize);
        return SQLITE_TOOBIG;
    }
    
    // Copy original data to buffer
    memcpy(buffer, pPage->original_data, pPage->original_size);
    
    // Zero-fill remaining buffer
    if (pPage->original_size < bufferSize) {
        memset(buffer + pPage->original_size, 0, bufferSize - pPage->original_size);
    }
    
    pWriter->hits++;
    
    CCVFS_DEBUG("Batch writer hit for page %u, returned %u bytes", pageNum, pPage->original_size);
    return SQLITE_OK;
}

/*
 * Flush all pages in batch writer to disk
 */
int ccvfs_flush_batch_writer(CCVFSFile *pFile) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    int rc = SQLITE_OK;
    
    CCVFS_DEBUG("Flushing batch writer: %u pages", pWriter->page_count);
    
    if (!pWriter->enabled || pWriter->page_count == 0) {
        CCVFS_DEBUG("Batch writer flush not needed: enabled=%d, page_count=%u", 
                   pWriter->enabled, pWriter->page_count);
        return SQLITE_OK;
    }
    
    // Allocate contiguous space for all pages
    CCVFSBatchAllocation allocation;
    uint32_t totalSize = ccvfs_calculate_batch_size(pWriter);
    
    rc = ccvfs_allocate_batch_space(pFile, totalSize, &allocation);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to allocate batch space: %d", rc);
        return rc;
    }
    
    CCVFS_DEBUG("Allocated batch space: offset=%llu, size=%u for %u pages",
               (unsigned long long)allocation.base_offset, allocation.total_size, pWriter->page_count);
    
    // Write all pages to allocated space
    rc = ccvfs_write_batch_pages(pFile, pWriter, &allocation);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write batch pages: %d", rc);
        ccvfs_rollback_batch_space(pFile, &allocation);
        return rc;
    }
    
    // Update page index for all pages
    rc = ccvfs_update_batch_index(pFile, pWriter, &allocation);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to update batch index: %d", rc);
        // Note: This is more serious - data is written but index is inconsistent
        // We should try to recover or mark the file as corrupted
        return rc;
    }
    
    // Clear batch writer after successful flush
    ccvfs_clear_batch_writer(pWriter);
    pWriter->flushes++;
    pWriter->last_flush_time = time(NULL);
    
    CCVFS_DEBUG("Batch writer flush completed successfully: %u pages written", allocation.page_count);
    return SQLITE_OK;
}

// ============================================================================
// BATCH WRITER HELPER FUNCTIONS
// ============================================================================

/*
 * Process page data (compress + encrypt + checksum)
 */
CCVFSProcessedPage* ccvfs_process_page_data(CCVFSFile *pFile, uint32_t pageNum,
                                           const unsigned char *data, uint32_t dataSize) {
    CCVFSProcessedPage *pPage;
    unsigned char *compressedData = NULL;
    unsigned char *encryptedData = NULL;
    const unsigned char *dataToProcess = data;
    uint32_t processedSize = dataSize;
    uint32_t flags = 0;
    int rc;
    
    CCVFS_DEBUG("Processing page %u data: %u bytes", pageNum, dataSize);
    
    // Allocate processed page structure
    pPage = (CCVFSProcessedPage*)sqlite3_malloc(sizeof(CCVFSProcessedPage));
    if (!pPage) {
        CCVFS_ERROR("Failed to allocate processed page structure");
        return NULL;
    }
    
    memset(pPage, 0, sizeof(CCVFSProcessedPage));
    pPage->page_number = pageNum;
    pPage->original_size = dataSize;
    pPage->created_time = time(NULL);
    pPage->is_dirty = 1;
    
    // Store original data for read hits
    pPage->original_data = sqlite3_malloc(dataSize);
    if (!pPage->original_data) {
        CCVFS_ERROR("Failed to allocate original data storage");
        sqlite3_free(pPage);
        return NULL;
    }
    memcpy(pPage->original_data, data, dataSize);
    
    // Check if page is all zeros (sparse page optimization)
    int isZeroPage = 1;
    for (uint32_t i = 0; i < dataSize; i++) {
        if (data[i] != 0) {
            isZeroPage = 0;
            break;
        }
    }
    
    if (isZeroPage) {
        CCVFS_DEBUG("Page %u is sparse (all zeros)", pageNum);
        pPage->processed_size = 0;
        pPage->checksum = 0;
        pPage->flags = CCVFS_PAGE_SPARSE;
        pPage->processed_data = NULL;
        return pPage;
    }
    
    // Compress data if compression is enabled
    if (pFile->pOwner && pFile->pOwner->pCompressAlg) {
        int maxCompressedSize = pFile->pOwner->pCompressAlg->get_max_compressed_size(dataSize);
        compressedData = sqlite3_malloc(maxCompressedSize);
        if (!compressedData) {
            CCVFS_ERROR("Failed to allocate compression buffer");
            goto error_cleanup;
        }
        
        rc = pFile->pOwner->pCompressAlg->compress(data, dataSize, compressedData, maxCompressedSize, 1);
        if (rc > 0 && (uint32_t)rc < dataSize) {
            // Compression successful and beneficial
            processedSize = rc;
            dataToProcess = compressedData;
            flags |= CCVFS_PAGE_COMPRESSED;
            CCVFS_DEBUG("Page %u compressed: %u -> %u bytes", pageNum, dataSize, processedSize);
        } else {
            // Compression not beneficial, use original data
            sqlite3_free(compressedData);
            compressedData = NULL;
            CCVFS_DEBUG("Page %u compression not beneficial", pageNum);
        }
    }
    
    // Encrypt data if encryption is enabled
    if (pFile->pOwner && pFile->pOwner->pEncryptAlg) {
        encryptedData = sqlite3_malloc(processedSize + 16); // Add padding for encryption
        if (!encryptedData) {
            CCVFS_ERROR("Failed to allocate encryption buffer");
            goto error_cleanup;
        }
        
        // Use simple key for now (should come from user in real implementation)
        unsigned char key[16] = "default_key_123";
        rc = pFile->pOwner->pEncryptAlg->encrypt(key, 16, dataToProcess, processedSize,
                                               encryptedData, processedSize + 16);
        if (rc > 0) {
            processedSize = rc;
            dataToProcess = encryptedData;
            flags |= CCVFS_PAGE_ENCRYPTED;
            CCVFS_DEBUG("Page %u encrypted: size %u", pageNum, processedSize);
        } else {
            CCVFS_ERROR("Failed to encrypt page %u: %d", pageNum, rc);
            goto error_cleanup;
        }
    }
    
    // Allocate final processed data buffer
    pPage->processed_data = sqlite3_malloc(processedSize);
    if (!pPage->processed_data) {
        CCVFS_ERROR("Failed to allocate processed data buffer");
        goto error_cleanup;
    }
    
    // Copy final processed data
    memcpy(pPage->processed_data, dataToProcess, processedSize);
    pPage->processed_size = processedSize;
    pPage->flags = flags;
    
    // Calculate checksum of processed data
    pPage->checksum = ccvfs_crc32(pPage->processed_data, processedSize);
    
    CCVFS_DEBUG("Page %u processed: original=%u, processed=%u, checksum=0x%08x, flags=0x%x",
               pageNum, pPage->original_size, pPage->processed_size, pPage->checksum, pPage->flags);
    
    // Clean up temporary buffers
    if (compressedData) sqlite3_free(compressedData);
    if (encryptedData) sqlite3_free(encryptedData);
    
    return pPage;
    
error_cleanup:
    if (compressedData) sqlite3_free(compressedData);
    if (encryptedData) sqlite3_free(encryptedData);
    if (pPage->original_data) sqlite3_free(pPage->original_data);
    if (pPage->processed_data) sqlite3_free(pPage->processed_data);
    sqlite3_free(pPage);
    return NULL;
}

/*
 * Find processed page in batch writer
 */
CCVFSProcessedPage* ccvfs_find_processed_page(CCVFSBatchWriter *pWriter, uint32_t pageNum) {
    CCVFSProcessedPage *pPage = pWriter->pages;
    
    while (pPage) {
        if (pPage->page_number == pageNum) {
            return pPage;
        }
        pPage = pPage->next;
    }
    
    return NULL;
}

/*
 * Add processed page to batch writer
 */
int ccvfs_add_processed_page(CCVFSBatchWriter *pWriter, CCVFSProcessedPage *pPage) {
    if (!pPage) {
        return SQLITE_MISUSE;
    }
    
    // Check page count limit
    if (pWriter->page_count >= pWriter->max_pages) {
        CCVFS_ERROR("Batch writer page limit reached: %u >= %u", 
                   pWriter->page_count, pWriter->max_pages);
        return SQLITE_FULL;
    }
    
    // Add to front of list
    pPage->next = pWriter->pages;
    pWriter->pages = pPage;
    pWriter->page_count++;
    
    // Update memory usage
    pWriter->total_memory_used += sizeof(CCVFSProcessedPage);
    pWriter->total_memory_used += pPage->original_size;
    if (pPage->processed_data) {
        pWriter->total_memory_used += pPage->processed_size;
    }
    
    CCVFS_DEBUG("Added page %u to batch writer: count=%u, memory=%u bytes",
               pPage->page_number, pWriter->page_count, pWriter->total_memory_used);
    
    return SQLITE_OK;
}

/*
 * Replace existing processed page
 */
int ccvfs_replace_processed_page(CCVFSBatchWriter *pWriter, 
                                CCVFSProcessedPage *existing, 
                                CCVFSProcessedPage *newPage) {
    if (!existing || !newPage) {
        return SQLITE_MISUSE;
    }
    
    CCVFS_DEBUG("Replacing page %u in batch writer", existing->page_number);
    
    // Update memory usage
    pWriter->total_memory_used -= sizeof(CCVFSProcessedPage);
    pWriter->total_memory_used -= existing->original_size;
    if (existing->processed_data) {
        pWriter->total_memory_used -= existing->processed_size;
    }
    
    // Copy list linkage
    newPage->next = existing->next;
    
    // Replace in list
    if (pWriter->pages == existing) {
        pWriter->pages = newPage;
    } else {
        CCVFSProcessedPage *prev = pWriter->pages;
        while (prev && prev->next != existing) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = newPage;
        }
    }
    
    // Free old page
    if (existing->original_data) sqlite3_free(existing->original_data);
    if (existing->processed_data) sqlite3_free(existing->processed_data);
    sqlite3_free(existing);
    
    // Update memory usage for new page
    pWriter->total_memory_used += sizeof(CCVFSProcessedPage);
    pWriter->total_memory_used += newPage->original_size;
    if (newPage->processed_data) {
        pWriter->total_memory_used += newPage->processed_size;
    }
    
    CCVFS_DEBUG("Replaced page %u in batch writer: memory=%u bytes",
               newPage->page_number, pWriter->total_memory_used);
    
    return SQLITE_OK;
}

/*
 * Check if batch writer should auto-flush
 */
int ccvfs_should_auto_flush(CCVFSBatchWriter *pWriter) {
    if (!pWriter->enabled || pWriter->auto_flush_threshold == 0) {
        return 0;
    }
    
    // Check page count threshold
    if (pWriter->page_count >= pWriter->auto_flush_threshold) {
        return 1;
    }
    
    // Check memory threshold
    if (pWriter->total_memory_used >= CCVFS_BATCH_WRITER_MB_TO_BYTES(pWriter->max_memory_mb)) {
        return 1;
    }
    
    return 0;
}

/*
 * Calculate total size needed for batch flush
 */
uint32_t ccvfs_calculate_batch_size(CCVFSBatchWriter *pWriter) {
    CCVFSProcessedPage *pPage = pWriter->pages;
    uint32_t totalSize = 0;
    
    while (pPage) {
        if (pPage->processed_data) {
            totalSize += pPage->processed_size;
        }
        pPage = pPage->next;
    }
    
    return totalSize;
}

/*
 * Clear all pages from batch writer
 */
void ccvfs_clear_batch_writer(CCVFSBatchWriter *pWriter) {
    CCVFSProcessedPage *pPage = pWriter->pages;
    CCVFSProcessedPage *pNext;
    
    while (pPage) {
        pNext = pPage->next;
        
        if (pPage->original_data) sqlite3_free(pPage->original_data);
        if (pPage->processed_data) sqlite3_free(pPage->processed_data);
        sqlite3_free(pPage);
        
        pPage = pNext;
    }
    
    pWriter->pages = NULL;
    pWriter->page_count = 0;
    pWriter->total_memory_used = 0;
    
    CCVFS_DEBUG("Cleared all pages from batch writer");
}// ==
==========================================================================
// BATCH SPACE ALLOCATION FUNCTIONS
// ============================================================================

/*
 * Allocate contiguous space for batch write
 */
int ccvfs_allocate_batch_space(CCVFSFile *pFile, uint32_t totalSize, 
                              CCVFSBatchAllocation *pAllocation) {
    int rc;
    
    CCVFS_DEBUG("Allocating batch space: %u bytes", totalSize);
    
    if (!pAllocation) {
        return SQLITE_MISUSE;
    }
    
    memset(pAllocation, 0, sizeof(CCVFSBatchAllocation));
    pAllocation->total_size = totalSize;
    
    // Try to find a large hole first
    sqlite3_int64 holeOffset = ccvfs_find_large_hole(pFile, totalSize);
    if (holeOffset > 0) {
        pAllocation->base_offset = holeOffset;
        CCVFS_DEBUG("Allocated batch space from hole: offset=%llu, size=%u",
                   (unsigned long long)holeOffset, totalSize);
        return SQLITE_OK;
    }
    
    // Allocate at end of file
    sqlite3_int64 fileSize;
    rc = pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to get file size for batch allocation: %d", rc);
        return rc;
    }
    
    pAllocation->base_offset = fileSize;
    
    CCVFS_DEBUG("Allocated batch space at EOF: offset=%llu, size=%u",
               (unsigned long long)fileSize, totalSize);
    
    return SQLITE_OK;
}

/*
 * Write all batch pages to allocated space
 */
int ccvfs_write_batch_pages(CCVFSFile *pFile, CCVFSBatchWriter *pWriter, 
                           CCVFSBatchAllocation *pAllocation) {
    unsigned char *batchData;
    uint32_t totalSize;
    int rc;
    
    CCVFS_DEBUG("Writing batch pages: %u pages to offset %llu",
               pWriter->page_count, (unsigned long long)pAllocation->base_offset);
    
    // Build contiguous data block
    batchData = ccvfs_build_batch_data(pWriter, &totalSize);
    if (!batchData) {
        CCVFS_ERROR("Failed to build batch data block");
        return SQLITE_NOMEM;
    }
    
    if (totalSize != pAllocation->total_size) {
        CCVFS_ERROR("Batch data size mismatch: expected %u, got %u",
                   pAllocation->total_size, totalSize);
        sqlite3_free(batchData);
        return SQLITE_INTERNAL;
    }
    
    // Write all data in one operation
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, batchData, totalSize, 
                                       pAllocation->base_offset);
    
    sqlite3_free(batchData);
    
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write batch data: %d", rc);
        return rc;
    }
    
    pAllocation->used_size = totalSize;
    pAllocation->page_count = pWriter->page_count;
    
    CCVFS_DEBUG("Successfully wrote batch data: %u bytes, %u pages",
               totalSize, pWriter->page_count);
    
    return SQLITE_OK;
}

/*
 * Update page index for all batch pages
 */
int ccvfs_update_batch_index(CCVFSFile *pFile, CCVFSBatchWriter *pWriter, 
                            CCVFSBatchAllocation *pAllocation) {
    CCVFSProcessedPage *pPage = pWriter->pages;
    sqlite3_int64 currentOffset = pAllocation->base_offset;
    uint32_t updatedPages = 0;
    
    CCVFS_DEBUG("Updating batch index: %u pages starting at offset %llu",
               pWriter->page_count, (unsigned long long)pAllocation->base_offset);
    
    // Ensure page index is large enough for all pages
    uint32_t maxPageNum = 0;
    pPage = pWriter->pages;
    while (pPage) {
        if (pPage->page_number > maxPageNum) {
            maxPageNum = pPage->page_number;
        }
        pPage = pPage->next;
    }
    
    if (maxPageNum >= pFile->header.total_pages) {
        int rc = ccvfs_expand_page_index(pFile, maxPageNum + 1);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to expand page index for batch update: %d", rc);
            return rc;
        }
    }
    
    // Update index for each page
    pPage = pWriter->pages;
    while (pPage) {
        CCVFSPageIndex *pIndex = &pFile->pPageIndex[pPage->page_number];
        
        // Handle sparse pages
        if (pPage->flags & CCVFS_PAGE_SPARSE) {
            // If page previously had physical storage, add it as a hole
            if (pIndex->physical_offset != 0 && pIndex->compressed_size > 0) {
                ccvfs_add_hole(pFile, pIndex->physical_offset, pIndex->compressed_size);
            }
            
            pIndex->physical_offset = 0;
            pIndex->compressed_size = 0;
            pIndex->original_size = pPage->original_size;
            pIndex->checksum = 0;
            pIndex->flags = CCVFS_PAGE_SPARSE;
        } else {
            // If page previously had physical storage, add it as a hole
            if (pIndex->physical_offset != 0 && pIndex->compressed_size > 0 &&
                pIndex->physical_offset != currentOffset) {
                ccvfs_add_hole(pFile, pIndex->physical_offset, pIndex->compressed_size);
            }
            
            pIndex->physical_offset = currentOffset;
            pIndex->compressed_size = pPage->processed_size;
            pIndex->original_size = pPage->original_size;
            pIndex->checksum = pPage->checksum;
            pIndex->flags = pPage->flags;
            
            currentOffset += pPage->processed_size;
        }
        
        updatedPages++;
        
        CCVFS_DEBUG("Updated index for page %u: offset=%llu, size=%u, checksum=0x%08x",
                   pPage->page_number, (unsigned long long)pIndex->physical_offset,
                   pIndex->compressed_size, pIndex->checksum);
        
        pPage = pPage->next;
    }
    
    // Mark index as dirty
    pFile->index_dirty = 1;
    
    // Update database size
    uint32_t maxPage = 0;
    for (uint32_t i = 0; i < pFile->header.total_pages; i++) {
        if (pFile->pPageIndex[i].physical_offset != 0 || 
            (pFile->pPageIndex[i].flags & CCVFS_PAGE_SPARSE)) {
            maxPage = i + 1;
        }
    }
    pFile->header.database_size_pages = maxPage;
    
    CCVFS_DEBUG("Batch index update completed: %u pages updated, database size: %u pages",
               updatedPages, pFile->header.database_size_pages);
    
    return SQLITE_OK;
}

/*
 * Rollback batch space allocation on error
 */
int ccvfs_rollback_batch_space(CCVFSFile *pFile, CCVFSBatchAllocation *pAllocation) {
    CCVFS_DEBUG("Rolling back batch space allocation: offset=%llu, size=%u",
               (unsigned long long)pAllocation->base_offset, pAllocation->total_size);
    
    // If we allocated from a hole, we need to restore the hole
    // If we allocated at EOF, we might want to truncate (but this is risky)
    // For now, just mark the space as a hole for future reuse
    
    if (pAllocation->used_size > 0) {
        int rc = ccvfs_add_hole(pFile, pAllocation->base_offset, pAllocation->used_size);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to add hole during rollback: %d", rc);
            // Continue anyway - this is just optimization
        }
    }
    
    return SQLITE_OK;
}

/*
 * Find large hole suitable for batch allocation
 */
sqlite3_int64 ccvfs_find_large_hole(CCVFSFile *pFile, uint32_t requiredSize) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    CCVFSSpaceHole *pHole;
    
    if (!pManager->enabled) {
        return 0;
    }
    
    CCVFS_DEBUG("Looking for large hole: required size %u", requiredSize);
    
    pHole = pManager->holes;
    while (pHole) {
        if (pHole->size >= requiredSize) {
            CCVFS_DEBUG("Found suitable hole: offset=%llu, size=%u",
                       (unsigned long long)pHole->offset, pHole->size);
            return pHole->offset;
        }
        pHole = pHole->next;
    }
    
    CCVFS_DEBUG("No suitable hole found for size %u", requiredSize);
    return 0;
}

/*
 * Build contiguous data block for batch write
 */
unsigned char* ccvfs_build_batch_data(CCVFSBatchWriter *pWriter, uint32_t *pTotalSize) {
    CCVFSProcessedPage *pPage;
    unsigned char *batchData;
    unsigned char *writePtr;
    uint32_t totalSize = 0;
    
    // Calculate total size
    pPage = pWriter->pages;
    while (pPage) {
        if (pPage->processed_data) {
            totalSize += pPage->processed_size;
        }
        pPage = pPage->next;
    }
    
    if (totalSize == 0) {
        *pTotalSize = 0;
        return NULL;
    }
    
    // Allocate batch data buffer
    batchData = sqlite3_malloc(totalSize);
    if (!batchData) {
        CCVFS_ERROR("Failed to allocate batch data buffer: %u bytes", totalSize);
        return NULL;
    }
    
    // Copy all processed page data
    writePtr = batchData;
    pPage = pWriter->pages;
    while (pPage) {
        if (pPage->processed_data && pPage->processed_size > 0) {
            memcpy(writePtr, pPage->processed_data, pPage->processed_size);
            writePtr += pPage->processed_size;
        }
        pPage = pPage->next;
    }
    
    *pTotalSize = totalSize;
    
    CCVFS_DEBUG("Built batch data block: %u bytes from %u pages", totalSize, pWriter->page_count);
    
    return batchData;
}

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
                                uint32_t *page_count) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    
    if (hits) *hits = pWriter->hits;
    if (flushes) *flushes = pWriter->flushes;
    if (merges) *merges = pWriter->merges;
    if (total_writes) *total_writes = pWriter->total_writes;
    if (memory_used) *memory_used = pWriter->total_memory_used;
    if (page_count) *page_count = pWriter->page_count;
    
    return SQLITE_OK;
}

/*
 * Configure batch writer parameters
 */
int ccvfs_configure_batch_writer(CCVFSFile *pFile,
                                int enabled,
                                uint32_t max_pages,
                                uint32_t max_memory_mb,
                                uint32_t auto_flush_threshold) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    
    CCVFS_DEBUG("Configuring batch writer: enabled=%d, max_pages=%u, max_memory=%uMB, auto_flush=%u",
               enabled, max_pages, max_memory_mb, auto_flush_threshold);
    
    // Validate parameters
    if (max_pages < CCVFS_BATCH_WRITER_MIN_MAX_PAGES || 
        max_pages > CCVFS_BATCH_WRITER_MAX_MAX_PAGES) {
        CCVFS_ERROR("Invalid max_pages: %u (must be %u-%u)", max_pages,
                   CCVFS_BATCH_WRITER_MIN_MAX_PAGES, CCVFS_BATCH_WRITER_MAX_MAX_PAGES);
        return SQLITE_RANGE;
    }
    
    if (max_memory_mb < CCVFS_BATCH_WRITER_MIN_MEMORY_MB || 
        max_memory_mb > CCVFS_BATCH_WRITER_MAX_MEMORY_MB) {
        CCVFS_ERROR("Invalid max_memory_mb: %u (must be %u-%u)", max_memory_mb,
                   CCVFS_BATCH_WRITER_MIN_MEMORY_MB, CCVFS_BATCH_WRITER_MAX_MEMORY_MB);
        return SQLITE_RANGE;
    }
    
    // If disabling, flush any pending pages first
    if (!enabled && pWriter->enabled && pWriter->page_count > 0) {
        CCVFS_DEBUG("Flushing %u pages before disabling batch writer", pWriter->page_count);
        int rc = ccvfs_flush_batch_writer(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to flush batch writer before disabling: %d", rc);
            return rc;
        }
    }
    
    // Apply new configuration
    pWriter->enabled = enabled;
    pWriter->max_pages = max_pages;
    pWriter->max_memory_mb = max_memory_mb;
    pWriter->auto_flush_threshold = auto_flush_threshold;
    
    CCVFS_INFO("Batch writer configured: enabled=%d, max_pages=%u, max_memory=%uMB, auto_flush=%u",
               pWriter->enabled, pWriter->max_pages, pWriter->max_memory_mb, pWriter->auto_flush_threshold);
    
    return SQLITE_OK;
}