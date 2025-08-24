#include "ccvfs_io.h"
#include "ccvfs_page.h"
#include "ccvfs_core.h"
#include "ccvfs_utils.h"
#include <string.h>

// Forward declarations
static void ccvfs_update_space_tracking(CCVFSFile *pFile);
static sqlite3_int64 ccvfs_find_best_fit_space(CCVFSFile *pFile, uint32_t requiredSize, uint32_t *pWastedSpace);
static void ccvfs_report_file_health(CCVFSFile *pFile);

// Hole management function declarations
static int ccvfs_remove_hole(CCVFSFile *pFile, sqlite3_int64 offset);
static void ccvfs_merge_adjacent_holes(CCVFSFile *pFile);
static void ccvfs_cleanup_small_holes(CCVFSFile *pFile);
static void ccvfs_check_hole_maintenance_threshold(CCVFSFile *pFile);

/*
 * 寻找最佳匹配的可用空间洞或间隙来满足所需大小
 * 使用最佳适配算法：选择能容纳所需大小的最小空洞
 * Find the best fitting available space hole or gap for the required size
 * Uses best-fit algorithm: select the smallest hole that can accommodate the required size
 */
static sqlite3_int64 ccvfs_find_best_fit_space(CCVFSFile *pFile, uint32_t requiredSize, uint32_t *pWastedSpace) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    CCVFSSpaceHole *pCurrent, *pBestFit;
    sqlite3_int64 bestOffset = 0;
    uint32_t bestWastedSpace = UINT32_MAX;
    
    CCVFS_DEBUG("Searching for best-fit hole: required_size=%u", requiredSize);
    
    // Initialize output parameter
    *pWastedSpace = 0;
    
    // Check if hole detection is enabled
    if (!pManager->enabled) {
        CCVFS_DEBUG("Hole detection disabled, returning 0");
        return 0;
    }
    
    // Check if we have any holes to search
    if (pManager->hole_count == 0) {
        CCVFS_DEBUG("No holes available for allocation");
        return 0;
    }
    
    // Search through all holes for the best fit
    pCurrent = pManager->holes;
    pBestFit = NULL;
    
    while (pCurrent) {
        // Check if this hole can accommodate the required size
        if (pCurrent->size >= requiredSize) {
            uint32_t wastedSpace = pCurrent->size - requiredSize;
            
            CCVFS_DEBUG("Evaluating hole[%llu,%u]: waste=%u", 
                       (unsigned long long)pCurrent->offset, pCurrent->size, wastedSpace);
            
            // Check if this is a better fit than our current best
            if (wastedSpace < bestWastedSpace) {
                pBestFit = pCurrent;
                bestOffset = pCurrent->offset;
                bestWastedSpace = wastedSpace;
                
                CCVFS_DEBUG("New best fit: hole[%llu,%u], waste=%u", 
                           (unsigned long long)bestOffset, pCurrent->size, bestWastedSpace);
                
                // Perfect fit found, no need to search further
                if (wastedSpace == 0) {
                    CCVFS_DEBUG("Perfect fit found, stopping search");
                    break;
                }
            }
        } else {
            CCVFS_DEBUG("Hole[%llu,%u] too small for required size %u", 
                       (unsigned long long)pCurrent->offset, pCurrent->size, requiredSize);
        }
        
        pCurrent = pCurrent->next;
    }
    
    // Return results
    if (pBestFit) {
        *pWastedSpace = bestWastedSpace;
        CCVFS_DEBUG("Best-fit search result: offset=%llu, hole_size=%u, required=%u, waste=%u", 
                   (unsigned long long)bestOffset, pBestFit->size, requiredSize, bestWastedSpace);
        return bestOffset;
    } else {
        CCVFS_DEBUG("No suitable hole found for size %u", requiredSize);
        return 0;
    }
}

/*
 * IO方法表 - 定义了VFS层的所有文件操作接口
 * IO Methods table - Defines all file operation interfaces for VFS layer
 */
sqlite3_io_methods ccvfsIoMethods = {
    3,                              /* iVersion */
    ccvfsIoClose,                   /* xClose */
    ccvfsIoRead,                    /* xRead */
    ccvfsIoWrite,                   /* xWrite */
    ccvfsIoTruncate,               /* xTruncate */
    ccvfsIoSync,                   /* xSync */
    ccvfsIoFileSize,               /* xFileSize */
    ccvfsIoLock,                   /* xLock */
    ccvfsIoUnlock,                 /* xUnlock */
    ccvfsIoCheckReservedLock,      /* xCheckReservedLock */
    ccvfsIoFileControl,            /* xFileControl */
    ccvfsIoSectorSize,             /* xSectorSize */
    ccvfsIoDeviceCharacteristics,  /* xDeviceCharacteristics */
    ccvfsIoShmMap,                 /* xShmMap */
    ccvfsIoShmLock,                /* xShmLock */
    ccvfsIoShmBarrier,             /* xShmBarrier */
    ccvfsIoShmUnmap,               /* xShmUnmap */
    ccvfsIoFetch,                  /* xFetch */
    ccvfsIoUnfetch                 /* xUnfetch */
};

/*
 * 关闭文件并清理资源
 * 保存页索引和文件头，释放内存，关闭底层文件
 * Close file and clean up resources
 * Save page index and header, free memory, close underlying file
 */
int ccvfsIoClose(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    int rc = SQLITE_OK;
    
    CCVFS_DEBUG("Closing CCVFS file");
    
    if (p->pReal) {
        // 刷新写入缓冲区（如果启用）
        // Flush write buffer if enabled
        if (p->is_ccvfs_file && p->write_buffer.enabled && p->write_buffer.entry_count > 0) {
            CCVFS_DEBUG("Flushing %u buffered entries before close", p->write_buffer.entry_count);
            int flushRc = ccvfs_flush_write_buffer(p);
            if (flushRc != SQLITE_OK) {
                CCVFS_ERROR("Failed to flush write buffer during close: %d", flushRc);
                rc = flushRc;
            }
        }
        
        // 关闭前保存页索引和文件头（仅对可写文件）
        // Save page index and header before closing (only for writable files)
        if (p->pPageIndex && p->header_loaded && !(p->open_flags & SQLITE_OPEN_READONLY)) {
            int saveRc = ccvfs_save_page_index(p);
            if (saveRc != SQLITE_OK) {
                CCVFS_ERROR("Failed to save page index: %d", saveRc);
                rc = saveRc;
            }
            
            // Save header
            saveRc = ccvfs_save_header(p);
            if (saveRc != SQLITE_OK) {
                CCVFS_ERROR("Failed to save header: %d", saveRc);
                rc = saveRc;
            }
        }
        
        // 关闭底层文件
        // Close underlying file
        int closeRc = p->pReal->pMethods->xClose(p->pReal);
        if (closeRc != SQLITE_OK) {
            CCVFS_ERROR("Failed to close underlying file: %d", closeRc);
            rc = closeRc;
        }
    }
    
    // 释放页索引内存
    // Free page index
    if (p->pPageIndex) {
        sqlite3_free(p->pPageIndex);
        p->pPageIndex = NULL;
    }

    // 清理写入缓冲区
    // Clean up write buffer
    if (p->is_ccvfs_file) {
        ccvfs_cleanup_write_buffer(p);
    }

    // 清理空洞管理器
    // Clean up hole manager
    if (p->is_ccvfs_file) {
        ccvfs_cleanup_hole_manager(p);
    }

    // 在关闭前报告文件健康状态
    // Report file health status before closing
    if (p->is_ccvfs_file && p->header_loaded) {
        ccvfs_report_file_health(p);
    }

    CCVFS_INFO("File closed: %s", p->filename ? p->filename : "(null)");

    // Free filename
    if (p->filename) {
        sqlite3_free(p->filename);
        p->filename = NULL;
    }
    
    return rc;
}

/*
 * 根据文件偏移量获取页编号
 * 计算公式：页编号 = 偏移量 / 页大小
 * Get page number from file offset
 * Formula: page_number = offset / page_size
 */
static uint32_t getPageNumber(sqlite3_int64 offset, uint32_t pageSize) {
    if (pageSize == 0) {
        CCVFS_ERROR("Page size is zero, using default");
        pageSize = CCVFS_DEFAULT_PAGE_SIZE;
    }
    return (uint32_t)(offset / pageSize);
}

/*
 * 获取页内偏移量
 * 计算公式：页内偏移 = 偏移量 % 页大小
 * Get offset within page
 * Formula: page_offset = offset % page_size
 */
static uint32_t getPageOffset(sqlite3_int64 offset, uint32_t pageSize) {
    if (pageSize == 0) {
        CCVFS_ERROR("Page size is zero, using default");
        pageSize = CCVFS_DEFAULT_PAGE_SIZE;
    }
    return (uint32_t)(offset % pageSize);
}

/*
 * 从文件读取并解压一个数据页
 * 处理流程：读取压缩数据 -> 校验和验证 -> 解密 -> 解压缩 -> 复制到缓冲区
 * Read and decompress a page from file
 * Process flow: read compressed data -> checksum verification -> decrypt -> decompress -> copy to buffer
 */
static int readPage(CCVFSFile *pFile, uint32_t pageNum, unsigned char *buffer, uint32_t bufferSize) {
    CCVFS_DEBUG("=== READING PAGE %u ===", pageNum);
    
    // 检查页索引是否已加载（应该在ccvfsOpen中已加载）
    // Check if page index is loaded (should already be loaded in ccvfsOpen)
    if (!pFile->pPageIndex) {
        CCVFS_DEBUG("Page index not loaded, loading now");
        int rc = ccvfs_load_page_index(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to load page index: %d", rc);
            return rc;
        }
    }
    
    // 检查页编号有效性
    // Check page number validity
    if (pageNum >= pFile->header.total_pages) {
        CCVFS_DEBUG("Page %u beyond total pages %u, treating as zero (sparse)", 
                   pageNum, pFile->header.total_pages);
        memset(buffer, 0, bufferSize);
        return SQLITE_OK;
    }
    
    CCVFSPageIndex *pIndex = &pFile->pPageIndex[pageNum];
    
    CCVFS_DEBUG("Page[%u] mapping: physical_offset=%llu, compressed_size=%u, original_size=%u, flags=0x%x",
               pageNum, (unsigned long long)pIndex->physical_offset, 
               pIndex->compressed_size, pIndex->original_size, pIndex->flags);
    
    // 如果页没有物理存储（稀疏页），返回零填充数据
    // If page has no physical storage (sparse), return zeros
    if (pIndex->physical_offset == 0 || (pIndex->flags & CCVFS_PAGE_SPARSE)) {
        CCVFS_DEBUG("Page %u is sparse, returning zeros", pageNum);
        memset(buffer, 0, bufferSize);
        return SQLITE_OK;
    }
    
    // 为压缩数据分配临时缓冲区
    // Allocate temporary buffer for compressed data
    unsigned char *compressedData = sqlite3_malloc(pIndex->compressed_size);
    if (!compressedData) {
        CCVFS_ERROR("Failed to allocate memory for compressed data");
        return SQLITE_NOMEM;
    }
    
    // 读取压缩页数据
    // Read compressed page data
    int rc = pFile->pReal->pMethods->xRead(pFile->pReal, compressedData, 
                                          pIndex->compressed_size, 
                                          pIndex->physical_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read compressed page data: %d", rc);
        sqlite3_free(compressedData);
        return rc;
    }
    
    // 验证校验和并提供数据恢复选项
    // Verify checksum with data recovery options
    uint32_t checksum = ccvfs_crc32(compressedData, pIndex->compressed_size);
    if (checksum != pIndex->checksum) {
        // 记录校验和错误统计
        // Record checksum error statistics
        pFile->checksum_error_count++;
        pFile->corrupted_page_count++;
        
        CCVFS_ERROR("Page %u checksum mismatch: expected 0x%08x, got 0x%08x (error #%u)", 
                   pageNum, pIndex->checksum, checksum, pFile->checksum_error_count);
        CCVFS_ERROR("Page %u details: phys_offset=%llu, comp_size=%u, orig_size=%u, flags=0x%x", 
                   pageNum, pIndex->physical_offset, pIndex->compressed_size,
                   pIndex->original_size, pIndex->flags);
        
        // 显示损坏数据的前几个字节用于调试
        // Show first few bytes of corrupted data for debugging
        CCVFS_ERROR("First 16 bytes of page data: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                   compressedData[0], compressedData[1], compressedData[2], compressedData[3],
                   compressedData[4], compressedData[5], compressedData[6], compressedData[7],
                   compressedData[8], compressedData[9], compressedData[10], compressedData[11],
                   compressedData[12], compressedData[13], compressedData[14], compressedData[15]);
        
        // 数据恢复策略：根据配置决定如何处理校验和失败
        // Data recovery strategy: decide how to handle checksum failure based on configuration
        
        // 选项1：严格模式 - 立即返回错误（默认行为）
        // Option 1: Strict mode - return error immediately (default behavior)
        if (!pFile->pOwner || pFile->pOwner->strict_checksum_mode) {
            CCVFS_ERROR("Strict checksum mode: aborting read operation");
            sqlite3_free(compressedData);
            return SQLITE_CORRUPT;
        }
        
        // 选项2：容错模式 - 尝试继续处理损坏的数据
        // Option 2: Tolerant mode - try to continue with corrupted data
        pFile->recovery_attempt_count++;
        CCVFS_ERROR("Tolerant mode: continuing with potentially corrupted page %u (attempt #%u)", 
                   pageNum, pFile->recovery_attempt_count);
        
        // 未来可以在这里实现更多恢复策略：
        // Future recovery strategies could be implemented here:
        // - 尝试从备份或镜像读取页数据
        // - 使用错误纠正码恢复数据
        // - 返回部分数据或零填充数据
        // - 记录损坏页位置用于后续修复
        // - Try reading page data from backup or mirror
        // - Use error correction codes to recover data
        // - Return partial data or zero-filled data
        // - Record corrupted page location for later repair
        
        // 尝试简单的恢复策略：检查数据是否仍然可解压
        // Try simple recovery: check if data can still be decompressed
        int canRecover = 0;
        if (pFile->pOwner && pFile->pOwner->enable_data_recovery) {
            // 这里可以添加更复杂的恢复逻辑
            // More complex recovery logic could be added here
            canRecover = 1; // 简单地假设可以恢复
        }
        
        if (canRecover) {
            pFile->successful_recovery_count++;
            CCVFS_ERROR("Data recovery enabled: attempting to continue (success #%u)", 
                       pFile->successful_recovery_count);
        }
    }
    
    // Decrypt if needed
    unsigned char *decryptedData = compressedData;
    if (pFile->pOwner->pEncryptAlg && (pIndex->flags & CCVFS_PAGE_ENCRYPTED)) {
        // 为解密数据专门分配缓冲区
        decryptedData = sqlite3_malloc(pIndex->compressed_size);
        if (!decryptedData) {
            CCVFS_ERROR("Failed to allocate memory for decrypted data");
            sqlite3_free(compressedData);
            return SQLITE_NOMEM;
        }
        
        // 使用全局设置的加密密钥
        // Use globally set encryption key
        unsigned char key[32];
        int keyLen = ccvfs_get_encryption_key(key, sizeof(key));
        
        if (keyLen > 0) {
            rc = pFile->pOwner->pEncryptAlg->decrypt(key, keyLen, compressedData, 
                                                   pIndex->compressed_size,
                                                   decryptedData, pIndex->compressed_size);
            if (rc < 0) {
                CCVFS_ERROR("Failed to decrypt page %u: %d", pageNum, rc);
                sqlite3_free(compressedData);
                sqlite3_free(decryptedData);
                return SQLITE_CORRUPT;
            }
            CCVFS_VERBOSE("Page %u decrypted with %d-byte key", pageNum, keyLen);
            
            // 清除栈上的密钥副本
            memset(key, 0, sizeof(key));
        } else {
            CCVFS_ERROR("No encryption key available for decrypting page %u", pageNum);
            sqlite3_free(compressedData);
            sqlite3_free(decryptedData);
            return SQLITE_CORRUPT;
        }
        sqlite3_free(compressedData);
    }
    
    // 如果需要则解压缩数据
    // Decompress if needed
    if (pFile->pOwner->pCompressAlg && (pIndex->flags & CCVFS_PAGE_COMPRESSED)) {
        // 解压前验证压缩大小
        // Validate compressed size before decompression
        if (pIndex->compressed_size == 0 || pIndex->original_size == 0) {
            CCVFS_ERROR("Invalid page %u sizes: compressed=%u, original=%u", 
                       pageNum, pIndex->compressed_size, pIndex->original_size);
            if (decryptedData != compressedData) sqlite3_free(decryptedData);
            return SQLITE_CORRUPT;
        }
        
        rc = pFile->pOwner->pCompressAlg->decompress(decryptedData, pIndex->compressed_size,
                                                   buffer, bufferSize);
        if (rc < 0) {
            CCVFS_ERROR("Failed to decompress page %u: %d (compressed_size=%u, original_size=%u)", 
                       pageNum, rc, pIndex->compressed_size, pIndex->original_size);
            if (decryptedData != compressedData) sqlite3_free(decryptedData);
            return SQLITE_CORRUPT;
        }
        
        // 验证解压后的大小
        // Validate decompressed size
        if ((uint32_t)rc != pIndex->original_size) {
            CCVFS_ERROR("Page %u decompressed size mismatch: expected %u, got %d", 
                       pageNum, pIndex->original_size, rc);
            if (decryptedData != compressedData) sqlite3_free(decryptedData);
            return SQLITE_CORRUPT;
        }
        
        // 用零填充剩余缓冲区空间
        // Fill remaining buffer with zeros
        if ((uint32_t)rc < bufferSize) {
            memset(buffer + rc, 0, bufferSize - rc);
        }
    } else {
        // 无压缩，直接复制数据
        // No compression, copy data directly
        uint32_t copySize = (pIndex->original_size < bufferSize) ? pIndex->original_size : bufferSize;
        memcpy(buffer, decryptedData, copySize);
        
        // 用零填充剩余缓冲区空间
        // Fill remaining buffer with zeros
        if (copySize < bufferSize) {
            memset(buffer + copySize, 0, bufferSize - copySize);
        }
    }
    
    if (decryptedData != compressedData) {
        sqlite3_free(decryptedData);
    }
    
    CCVFS_VERBOSE("Successfully read and decompressed page %u", pageNum);
    return SQLITE_OK;
}

/*
 * Read from file
 */
int ccvfsIoRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    unsigned char *buffer = (unsigned char*)zBuf;
    int bytesRead = 0;
    int rc;
    
    CCVFS_DEBUG("=== READING %d bytes at offset %lld from file: %s ===", iAmt, iOfst, p->filename ? p->filename : "unknown");
    
    // If not a CCVFS file, read directly from underlying file
    if (!p->is_ccvfs_file) {
        CCVFS_DEBUG("Reading from regular file");
        return p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
    }
    
    // For CCVFS files, check if we have any actual data yet
    sqlite3_int64 physicalSize;
    rc = p->pReal->pMethods->xFileSize(p->pReal, &physicalSize);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    CCVFS_DEBUG("Physical file size: %lld bytes", physicalSize);
    
    // If physical file is empty or too small, this is a read from an empty database
    if (physicalSize == 0 || (iOfst == 0 && physicalSize < CCVFS_HEADER_SIZE)) {
        CCVFS_DEBUG("Reading from empty CCVFS file, returning SQLITE_IOERR_SHORT_READ");
        return SQLITE_IOERR_SHORT_READ;
    }
    
    // For CCVFS files, ensure header is loaded
    if (!p->header_loaded) {
        // Try to load existing header
        CCVFS_DEBUG("Header not loaded, loading now");
        rc = ccvfs_load_header(p);
        if (rc != SQLITE_OK) {
            CCVFS_DEBUG("Failed to load header, treating as empty file");
            return SQLITE_IOERR_SHORT_READ;
        }
        
        // Initialize write buffer if not already initialized
        if (!p->write_buffer.enabled && p->pOwner && p->pOwner->enable_write_buffer) {
            CCVFS_DEBUG("Initializing write buffer on header load");
            rc = ccvfs_init_write_buffer(p);
            if (rc != SQLITE_OK) {
                CCVFS_ERROR("Failed to initialize write buffer on read: %d", rc);
                // Continue without buffering rather than failing
            }
        }
    }
    
    // CCVFS file - use page-based reading with buffer checking
    uint32_t pageSize = p->header.page_size;
    if (pageSize == 0) {
        CCVFS_ERROR("Invalid page size in header, using default");
        pageSize = CCVFS_DEFAULT_PAGE_SIZE;
        p->header.page_size = pageSize;
    }
    
    uint32_t startPage = getPageNumber(iOfst, pageSize);
    uint32_t startOffset = getPageOffset(iOfst, pageSize);
    
    CCVFS_DEBUG("Page-based read: pageSize=%u, startPage=%u, startOffset=%u, buffer_enabled=%d", 
               pageSize, startPage, startOffset, p->write_buffer.enabled);
    
    // Allocate page buffer
    unsigned char *pageBuffer = sqlite3_malloc(pageSize);
    if (!pageBuffer) {
        CCVFS_ERROR("Failed to allocate page buffer");
        return SQLITE_NOMEM;
    }
    
    while (bytesRead < iAmt) {
        uint32_t currentPage = startPage + (bytesRead + startOffset) / pageSize;
        uint32_t currentOffset = (startOffset + bytesRead) % pageSize;
        uint32_t bytesToRead = pageSize - currentOffset;
        
        if (bytesToRead > (uint32_t)(iAmt - bytesRead)) {
            bytesToRead = iAmt - bytesRead;
        }
        
        CCVFS_DEBUG("Reading iteration: currentPage=%u, currentOffset=%u, bytesToRead=%u", 
                   currentPage, currentOffset, bytesToRead);
        
        // 首先尝试从缓冲区读取页面
        // First try to read page from buffer
        rc = ccvfs_buffer_read(p, currentPage, pageBuffer, pageSize);
        if (rc == SQLITE_OK) {
            // Successfully read from buffer
            CCVFS_DEBUG("Buffer hit for page %u during read", currentPage);
        } else if (rc == SQLITE_NOTFOUND) {
            // Not in buffer, read from disk
            CCVFS_DEBUG("Buffer miss for page %u, reading from disk", currentPage);
            rc = readPage(p, currentPage, pageBuffer, pageSize);
            if (rc != SQLITE_OK) {
                CCVFS_ERROR("Failed to read page %u from disk: %d", currentPage, rc);
                sqlite3_free(pageBuffer);
                return rc;
            }
        } else {
            // Error reading from buffer
            CCVFS_ERROR("Error reading page %u from buffer: %d", currentPage, rc);
            sqlite3_free(pageBuffer);
            return rc;
        }
        
        // Copy data from page buffer to output buffer
        memcpy(buffer + bytesRead, pageBuffer + currentOffset, bytesToRead);
        bytesRead += bytesToRead;
        
        CCVFS_DEBUG("Copied %u bytes from page, total read: %d/%d", bytesToRead, bytesRead, iAmt);
    }
    
    sqlite3_free(pageBuffer);
    
    CCVFS_DEBUG("Read complete: buffer_hits=%u, buffer_entries=%u", 
               p->buffer_hit_count, p->write_buffer.entry_count);
    CCVFS_VERBOSE("Successfully read %d bytes from offset %lld", iAmt, iOfst);
    return SQLITE_OK;
}

/*
 * 压缩并将一个页写入文件
 * 处理流程：检查稀疏页 -> 压缩 -> 加密 -> 空间分配 -> 写入磁盘 -> 更新索引
 * Compress and write a page to file
 * Process flow: check sparse page -> compress -> encrypt -> space allocation -> write to disk -> update index
 */
static int writePage(CCVFSFile *pFile, uint32_t pageNum, const unsigned char *data, uint32_t dataSize) {
    CCVFS_DEBUG("=== WRITING PAGE %u ===", pageNum);
    CCVFS_DEBUG("Page %u: writing %u bytes", pageNum, dataSize);
    
    // Track whether this write is using hole allocation
    int isHoleAllocation = 0;
    
    // 确保页索引足够大
    // Ensure page index is large enough
    if (pageNum >= pFile->header.total_pages) {
        CCVFS_DEBUG("Need to expand page index from %u to %u", 
                   pFile->header.total_pages, pageNum + 1);
        int rc = ccvfs_expand_page_index(pFile, pageNum + 1);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to expand page index: %d", rc);
            return rc;
        }
    }
    
    CCVFSPageIndex *pIndex = &pFile->pPageIndex[pageNum];
    
    CCVFS_DEBUG("Page[%u] current mapping: physical_offset=%llu, compressed_size=%u, flags=0x%x",
               pageNum, (unsigned long long)pIndex->physical_offset, 
               pIndex->compressed_size, pIndex->flags);
    
    // 检查页是否全为零（稀疏页优化）
    // Check if page is all zeros (sparse page optimization)
    int isZeroPage = 1;
    for (uint32_t i = 0; i < dataSize; i++) {
        if (data[i] != 0) {
            isZeroPage = 0;
            break;
        }
    }
    
    if (isZeroPage) {
        CCVFS_DEBUG("Page %u is all zeros, treating as sparse", pageNum);
        
        // If page previously had physical storage, add it as a hole
        if (pIndex->physical_offset != 0 && pIndex->compressed_size > 0) {
            CCVFS_DEBUG("Converting page %u from physical to sparse, adding hole[%llu,%u]",
                       pageNum, (unsigned long long)pIndex->physical_offset, pIndex->compressed_size);
            
            int rc = ccvfs_add_hole(pFile, pIndex->physical_offset, pIndex->compressed_size);
            if (rc != SQLITE_OK) {
                CCVFS_ERROR("Failed to add hole for sparse page conversion: %d", rc);
                // Continue anyway, don't fail the operation
            }
        }
        
        pIndex->physical_offset = 0;
        pIndex->compressed_size = 0;
        pIndex->original_size = dataSize;
        pIndex->checksum = 0;
        pIndex->flags = CCVFS_PAGE_SPARSE;
        
        // 将索引标记为脏，但不立即保存
        // Mark index as dirty but don't save immediately
        pFile->index_dirty = 1;
        
        // 即使是稀疏页也要更新逻辑数据库大小
        // Update logical database size for sparse pages too
        if (pageNum + 1 > pFile->header.database_size_pages) {
            pFile->header.database_size_pages = pageNum + 1;
            CCVFS_DEBUG("Database size updated to %u pages for sparse page", 
                       pFile->header.database_size_pages);
        }
        
        CCVFS_DEBUG("Page[%u] updated to sparse, index marked dirty", pageNum);
        return SQLITE_OK;
    }
    
    // 准备压缩缓冲区
    // Prepare compression buffer
    unsigned char *compressedData = NULL;
    uint32_t compressedSize = dataSize;
    uint32_t flags = 0;
    
    if (pFile->pOwner->pCompressAlg) {
        int maxCompressedSize = pFile->pOwner->pCompressAlg->get_max_compressed_size(dataSize);
        compressedData = sqlite3_malloc(maxCompressedSize);
        if (!compressedData) {
            CCVFS_ERROR("Failed to allocate memory for compression");
            return SQLITE_NOMEM;
        }
        
        int rc = pFile->pOwner->pCompressAlg->compress(data, dataSize, compressedData, maxCompressedSize, 1);
        if (rc > 0 && (uint32_t)rc < dataSize) {
            // 压缩成功且有效
            // Compression successful and beneficial
            compressedSize = rc;
            flags |= CCVFS_PAGE_COMPRESSED;
            CCVFS_VERBOSE("Page %u compressed from %u to %u bytes", pageNum, dataSize, compressedSize);
        } else {
            // 压缩失败或无效，使用原始数据
            // Compression failed or not beneficial, use original data
            sqlite3_free(compressedData);
            compressedData = NULL;
            compressedSize = dataSize;
            CCVFS_DEBUG("Page %u compression not beneficial, using original data", pageNum);
        }
    }
    
    // 如果没有压缩或压缩失败，使用原始数据
    // Use original data if no compression or compression failed
    const unsigned char *dataToWrite = compressedData ? compressedData : data;
    
    // 如果需要则加密数据
    // Encrypt if needed
    unsigned char *encryptedData = NULL;
    if (pFile->pOwner->pEncryptAlg) {
        // AES-CBC需要额外空间用于IV和padding (最多15字节padding)
        // Allocate enough space for IV (16 bytes) + data + max padding (16 bytes)
        uint32_t encryptedBufferSize = compressedSize + 16 + 16; // IV + data + max padding
        encryptedData = sqlite3_malloc(encryptedBufferSize);
        if (!encryptedData) {
            CCVFS_ERROR("Failed to allocate memory for encryption buffer (need %u bytes)", encryptedBufferSize);
            if (compressedData) sqlite3_free(compressedData);
            return SQLITE_NOMEM;
        }
        
        // 使用全局设置的加密密钥
        // Use globally set encryption key
        unsigned char key[32];
        int keyLen = ccvfs_get_encryption_key(key, sizeof(key));
        
        if (keyLen > 0) {
            int rc = pFile->pOwner->pEncryptAlg->encrypt(key, keyLen, dataToWrite, compressedSize,
                                                       encryptedData, encryptedBufferSize);
            if (rc > 0) {
                compressedSize = rc;
                flags |= CCVFS_PAGE_ENCRYPTED;
                dataToWrite = encryptedData;
                CCVFS_VERBOSE("Page %u encrypted with %d-byte key, size %u", pageNum, keyLen, compressedSize);
            } else {
                CCVFS_ERROR("Failed to encrypt page %u: %d", pageNum, rc);
                sqlite3_free(encryptedData);
                if (compressedData) sqlite3_free(compressedData);
                return SQLITE_IOERR;
            }
            
            // 清除栈上的密钥副本
            memset(key, 0, sizeof(key));
        } else {
            CCVFS_ERROR("No encryption key available for page %u", pageNum);
            sqlite3_free(encryptedData);
            if (compressedData) sqlite3_free(compressedData);
            return SQLITE_IOERR;
        }
    }
    
    // 计算数据校验和
    uint32_t checksum = ccvfs_crc32(dataToWrite, compressedSize);
    
    // 确定写入偏移：重用现有页位置或分配新空间
    sqlite3_int64 writeOffset;
    
    // 检查页是否已存在且可以安全重用
    if (pIndex->physical_offset != 0) {
        uint32_t existingSpace = pIndex->compressed_size;
        
        // 【增强空间重用策略】：多种重用场景
        if (compressedSize <= existingSpace) {
            // 【场景1】：完美匹配或更小 - 直接重用
            writeOffset = pIndex->physical_offset;
            isHoleAllocation = 1;  // Mark as hole allocation since we're reusing existing space
            
            // 计算空间效率
            uint32_t wastedSpace = existingSpace - compressedSize;
            double spaceEfficiency = (double)compressedSize / (double)existingSpace;
            
            // 更新空间跟踪计数器
            pFile->space_reuse_count++;
            
            CCVFS_DEBUG("重用现有空间在偏移 %llu: 新=%u, 现有=%u, 浪费=%u (%.1f%% 效率)",
                       (unsigned long long)writeOffset, compressedSize, existingSpace, 
                       wastedSpace, spaceEfficiency * 100.0);
        } else {
            // 【场景2】：任意大小增长 - 检查是否可以安全扩展
            // 如果页后面有可用空间，允许无限制扩展
            sqlite3_int64 fileSize;
            int sizeRc = pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
            if (sizeRc == SQLITE_OK) {
                uint64_t pageEndOffset = pIndex->physical_offset + existingSpace;
                uint32_t expansionNeeded = compressedSize - existingSpace;
                CCVFS_INFO("-------");
                
                // 限制极端扩展以防止病态行为
                double growthRatio = (double)compressedSize / (double)existingSpace;
                if (growthRatio > 10.0) {
                    CCVFS_DEBUG("检测到极端增长 (%.1fx)，为稳定性分配新空间", growthRatio);
                    pFile->new_allocation_count++;
                    goto allocate_new_space;
                }
                
                // 检查是否可以安全扩展（没有数据与扩展后的区域重叠）
                int canExpand = 1;
                sqlite3_int64 expandedEnd = pageEndOffset + expansionNeeded;

                for (uint32_t i = 0; i < pFile->header.total_pages; i++) {
                    if (i != pageNum && pFile->pPageIndex[i].physical_offset != 0) {
                        sqlite3_int64 otherStart = pFile->pPageIndex[i].physical_offset;
                        sqlite3_int64 otherEnd = otherStart + pFile->pPageIndex[i].compressed_size;
                        
                        // 检查扩展后的区域是否与其他页面重叠（包含32字节安全边距）
                        if ((pageEndOffset < otherEnd + 32) && (expandedEnd + 32 > otherStart)) {
                            canExpand = 0;
                            CCVFS_DEBUG("无法扩展页面 %u：会与页面 %u 重叠 [%llu,%llu] vs [%llu,%llu]",
                                       pageNum, i, 
                                       (unsigned long long)pageEndOffset, (unsigned long long)expandedEnd,
                                       (unsigned long long)otherStart, (unsigned long long)otherEnd);
                            break;
                        }
                    }
                }
                
                if (canExpand && pageEndOffset + expansionNeeded <= fileSize) {
                    // 可以安全扩展现有空间
                    writeOffset = pIndex->physical_offset;
                    isHoleAllocation = 1;  // Mark as hole allocation since we're reusing existing space
                    pFile->space_expansion_count++;
                    CCVFS_DEBUG("扩展现有页在偏移 %llu: %u->%u 字节 (+%u 扩展, %.1fx 增长)",
                               (unsigned long long)writeOffset, existingSpace, compressedSize, expansionNeeded, growthRatio);
                } else {
                    // 不能安全扩展 - 分配新空间并添加旧空间为空洞
                    CCVFS_INFO("不能安全扩展（相邻页或EOF），分配新空间");
                    
                    // Add the old space as a hole since we're abandoning it
                    int rc = ccvfs_add_hole(pFile, pIndex->physical_offset, existingSpace);
                    if (rc != SQLITE_OK) {
                        CCVFS_ERROR("Failed to add hole for abandoned page space: %d", rc);
                        // Continue anyway, don't fail the operation
                    }
                    
                    pFile->new_allocation_count++;
                    goto allocate_new_space;
                }
            } else {
                CCVFS_DEBUG("获取文件大小失败，用于扩展检查，分配新空间");
                
                // Add the old space as a hole since we're abandoning it
                int rc = ccvfs_add_hole(pFile, pIndex->physical_offset, existingSpace);
                if (rc != SQLITE_OK) {
                    CCVFS_ERROR("Failed to add hole for abandoned page space: %d", rc);
                    // Continue anyway, don't fail the operation
                }
                
                pFile->new_allocation_count++;
                goto allocate_new_space;
            }
        }
    } else {
        allocate_new_space:
        // 【智能空间分配】：先尝试最佳适配，然后追加到文件末尾
        pFile->new_allocation_count++;
        
        // 尝试使用最佳适配算法找到合适的空洞
        uint32_t wastedSpace = 0;
        writeOffset = ccvfs_find_best_fit_space(pFile, compressedSize, &wastedSpace);
        
        if (writeOffset > 0) {
            // 找到合适的空洞 - 标记为空洞分配但暂不更新空洞记录
            // (空洞记录将在写入成功后更新)
            pFile->hole_reclaim_count++;
            pFile->best_fit_count++;
            isHoleAllocation = 1;  // Mark this as hole allocation
            CCVFS_DEBUG("使用最佳适配空洞在偏移 %llu 存储 %u 字节 (浪费: %u)", 
                       (unsigned long long)writeOffset, compressedSize, wastedSpace);
        } else {
            // 没有找到合适的空洞 - 追加到文件末尾
            sqlite3_int64 fileSize;
            int sizeRc = pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
            if (sizeRc != SQLITE_OK) {
                CCVFS_ERROR("获取文件大小失败: %d", sizeRc);
                if (encryptedData) sqlite3_free(encryptedData);
                if (compressedData) sqlite3_free(compressedData);
                return sizeRc;
            }
            
            // 检查顺序写入模式（多个连续页分配）
            if (pFile->last_written_page != UINT32_MAX && pageNum == pFile->last_written_page + 1) {
                pFile->sequential_write_count++;
                CCVFS_DEBUG("检测到顺序写入: 页 %u->%u", pFile->last_written_page, pageNum);
            }
            pFile->last_written_page = pageNum;
            
            // 确保我们在保留的索引表空间之后写入数据页
            writeOffset = fileSize;
            if (writeOffset < CCVFS_DATA_PAGES_OFFSET) {
                writeOffset = CCVFS_DATA_PAGES_OFFSET;
                CCVFS_DEBUG("调整写入偏移到 %llu (保留索引空间之后)", 
                           (unsigned long long)writeOffset);
            } else {
                // 确保新分配的空间不与现有页面重叠
                sqlite3_int64 candidateOffset = writeOffset;
                int foundSafeOffset = 0;
                
                // 尝试找到一个安全的写入位置
                for (int attempts = 0; attempts < 100 && !foundSafeOffset; attempts++) {
                    foundSafeOffset = 1;
                    sqlite3_int64 candidateEnd = candidateOffset + compressedSize;
                    
                    // 检查与所有现有页面的重叠
                    for (uint32_t i = 0; i < pFile->header.total_pages; i++) {
                        if (i != pageNum && pFile->pPageIndex[i].physical_offset != 0) {
                            sqlite3_int64 otherStart = pFile->pPageIndex[i].physical_offset;
                            sqlite3_int64 otherEnd = otherStart + pFile->pPageIndex[i].compressed_size;
                            
                            // 检查重叠
                            if (candidateOffset < otherEnd && candidateEnd > otherStart) {
                                // 发现重叠，尝试新位置
                                candidateOffset = otherEnd;
                                foundSafeOffset = 0;
                                CCVFS_DEBUG("避免与页面 %u 重叠，尝试新偏移: %llu", i, 
                                           (unsigned long long)candidateOffset);
                                break;
                            }
                        }
                    }
                }
                
                if (foundSafeOffset) {
                    writeOffset = candidateOffset;
                    CCVFS_DEBUG("在文件末尾分配新页: 偏移 %llu (顺序: %u, 安全检查通过)",
                               (unsigned long long)writeOffset, pFile->sequential_write_count);
                } else {
                    CCVFS_ERROR("无法找到安全的写入位置，页面布局可能损坏");
                    if (encryptedData) sqlite3_free(encryptedData);
                    if (compressedData) sqlite3_free(compressedData);
                    return SQLITE_IOERR;
                }
            }
        }
    }
    
    // Verify that the allocated space is valid and safe to write to
    if (writeOffset < CCVFS_DATA_PAGES_OFFSET) {
        CCVFS_ERROR("Invalid write offset %llu < %d (reserved space)", 
                   (unsigned long long)writeOffset, CCVFS_DATA_PAGES_OFFSET);
        if (encryptedData) sqlite3_free(encryptedData);
        if (compressedData) sqlite3_free(compressedData);
        return SQLITE_IOERR;
    }
    
    // Additional safety check: ensure we don't overwrite existing page data
    // BUT: allow hole reuse by checking if write offset came from hole allocation
    // Note: isHoleAllocation is determined during the allocation process above
    
    for (uint32_t i = 0; i < pFile->header.total_pages; i++) {
        if (i != pageNum && pFile->pPageIndex[i].physical_offset != 0) {
            sqlite3_int64 otherStart = pFile->pPageIndex[i].physical_offset;
            sqlite3_int64 otherEnd = otherStart + pFile->pPageIndex[i].compressed_size;
            sqlite3_int64 writeEnd = writeOffset + compressedSize;
            
            // Check for overlap
            if ((writeOffset < otherEnd && writeEnd > otherStart)) {
                // If this is a hole allocation, we need to check if this is legitimate space reuse
                if (isHoleAllocation) {
                    // Special case: Check if this is in-place expansion of the current page
                    if (writeOffset == pFile->pPageIndex[pageNum].physical_offset) {
                        CCVFS_DEBUG("Allowing in-place expansion: page %u expanding from offset %llu",
                                   pageNum, (unsigned long long)writeOffset);
                        continue; // Skip overlap check - this is in-place expansion
                    }
                    
                    // Check if we have a valid hole at this location that allows this allocation
                    CCVFSHoleManager *pManager = &pFile->hole_manager;
                    CCVFSSpaceHole *pHole = pManager->holes;
                    int validHoleAllocation = 0;
                    
                    while (pHole) {
                        sqlite3_int64 holeEnd = pHole->offset + pHole->size;
                        // Check if the write is within a valid hole
                        if (writeOffset >= pHole->offset && writeEnd <= holeEnd) {
                            validHoleAllocation = 1;
                            CCVFS_DEBUG("Valid hole allocation: write[%llu,%llu] within hole[%llu,%u]",
                                       (unsigned long long)writeOffset, (unsigned long long)writeEnd,
                                       (unsigned long long)pHole->offset, pHole->size);
                            break;
                        }
                        pHole = pHole->next;
                    }
                    
                    if (validHoleAllocation) {
                        CCVFS_DEBUG("Allowing valid hole reuse: write[%llu,%llu] overlaps page %u[%llu,%llu] but within valid hole",
                                   (unsigned long long)writeOffset, (unsigned long long)writeEnd,
                                   i, (unsigned long long)otherStart, (unsigned long long)otherEnd);
                        continue; // Skip this overlap check - it's legitimate hole reuse
                    }
                }
                
                CCVFS_ERROR("Write would overlap with page %u: write[%llu,%llu] vs existing[%llu,%llu] (hole_alloc=%d)",
                           i, (unsigned long long)writeOffset, (unsigned long long)writeEnd,
                           (unsigned long long)otherStart, (unsigned long long)otherEnd, isHoleAllocation);
                if (encryptedData) sqlite3_free(encryptedData);
                if (compressedData) sqlite3_free(compressedData);
                return SQLITE_IOERR;
            }
        }
    }
    
    int rc = pFile->pReal->pMethods->xWrite(pFile->pReal, dataToWrite, compressedSize, writeOffset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write page data: %d", rc);
        if (encryptedData) sqlite3_free(encryptedData);
        if (compressedData) sqlite3_free(compressedData);
        return rc;
    }
    
    // If this was a hole allocation, update the hole records now that write succeeded
    if (isHoleAllocation) {
        int holeRc = ccvfs_allocate_from_hole(pFile, writeOffset, compressedSize);
        if (holeRc != SQLITE_OK) {
            CCVFS_ERROR("Failed to update hole records after successful write: %d", holeRc);
            // Continue anyway - the write succeeded, hole tracking is just optimization
        } else {
            CCVFS_DEBUG("Successfully updated hole records after allocation");
        }
    }
    
    // Update page index entry
    pIndex->physical_offset = writeOffset;
    pIndex->compressed_size = compressedSize;
    pIndex->original_size = dataSize;
    pIndex->checksum = checksum;
    pIndex->flags = flags;
    
    // Update space utilization tracking
    ccvfs_update_space_tracking(pFile);
    
    // Mark index as dirty (will be saved on sync/close)
    pFile->index_dirty = 1;
    
    // Update logical database size
    uint32_t maxPage = 0;
    for (uint32_t i = 0; i < pFile->header.total_pages; i++) {
        if (pFile->pPageIndex[i].physical_offset != 0) { // Non-sparse page
            maxPage = i + 1;
        }
    }
    pFile->header.database_size_pages = maxPage;
    
    CCVFS_DEBUG("Page[%u] updated: physical_offset=%llu, compressed_size=%u, flags=0x%x",
               pageNum, (unsigned long long)writeOffset, compressedSize, flags);
    CCVFS_DEBUG("Database size updated to %u pages (%llu bytes)", 
               pFile->header.database_size_pages, 
               (unsigned long long)pFile->header.database_size_pages * pFile->header.page_size);
    CCVFS_DEBUG("Index marked dirty, will be saved on next sync/close");
    
    // Clean up
    if (encryptedData) sqlite3_free(encryptedData);
    if (compressedData) sqlite3_free(compressedData);
    
    CCVFS_VERBOSE("Successfully wrote page %u at offset %lld", pageNum, writeOffset);
    return SQLITE_OK;
}

/*
 * 报告文件的数据完整性和健康状态
 * Report file data integrity and health status
 */
static void ccvfs_report_file_health(CCVFSFile *pFile) {
    if (!pFile) return;
    
    uint32_t totalErrors = pFile->checksum_error_count + pFile->corrupted_page_count;
    if (totalErrors == 0) {
        CCVFS_INFO("文件健康状况良好：没有发现数据损坏 File health: Good - no data corruption detected");
        return;
    }
    
    // 计算数据完整性评分 (0-100, 100为最好)
    // Calculate data integrity score (0-100, 100 is best)
    uint32_t totalPages = pFile->header.total_pages;
    uint32_t integrityScore = 100;
    if (totalPages > 0) {
        uint32_t corruptionRate = (pFile->corrupted_page_count * 100) / totalPages;
        integrityScore = (corruptionRate > 100) ? 0 : (100 - corruptionRate);
    }
    
    // 计算恢复成功率
    // Calculate recovery success rate
    uint32_t recoveryRate = 0;
    if (pFile->recovery_attempt_count > 0) {
        recoveryRate = (pFile->successful_recovery_count * 100) / pFile->recovery_attempt_count;
    }
    
    const char *healthStatus;
    if (integrityScore >= 95) {
        healthStatus = "优秀 Excellent";
    } else if (integrityScore >= 80) {
        healthStatus = "良好 Good";
    } else if (integrityScore >= 60) {
        healthStatus = "一般 Fair";
    } else if (integrityScore >= 30) {
        healthStatus = "较差 Poor";
    } else {
        healthStatus = "严重损坏 Critical";
    }
    
    CCVFS_INFO("文件健康报告 File Health Report: %s (评分Score: %u/100)", healthStatus, integrityScore);
    CCVFS_INFO("  校验和错误 Checksum errors: %u", pFile->checksum_error_count);
    CCVFS_INFO("  损坏页数量 Corrupted pages: %u/%u (%.1f%%)",
               pFile->corrupted_page_count, totalPages, 
               totalPages > 0 ? (pFile->corrupted_page_count * 100.0f / totalPages) : 0.0f);
    CCVFS_INFO("  恢复尝试 Recovery attempts: %u (成功率Success rate: %u%%)", 
               pFile->recovery_attempt_count, recoveryRate);
    
    if (integrityScore < 80) {
        CCVFS_ERROR("警告：文件存在数据完整性问题，建议检查和修复");
        CCVFS_ERROR("WARNING: File has data integrity issues, recommend check and repair");
    }
}

/*
 * 更新空间利用跟踪指标
 * 更新空间利用跟踪指标
 */
static void ccvfs_update_space_tracking(CCVFSFile *pFile) {
    uint64_t totalAllocated = 0;
    uint64_t totalUsed = 0;
    uint32_t pageCount = 0;
    uint32_t wastedSpacePages = 0;
    uint64_t totalWastedSpace = 0;
    
    // 从所有页计算空间利用率
    for (uint32_t i = 0; i < pFile->header.total_pages; i++) {
        CCVFSPageIndex *pIndex = &pFile->pPageIndex[i];
        if (pIndex->physical_offset != 0) { // 非稀疏页
            pageCount++;
            totalAllocated += pIndex->compressed_size;
            totalUsed += pIndex->compressed_size; // 所有分配的空间目前都被使用
            
            // 检查显著的浪费空间（>10%浪费）
            if (pIndex->original_size > 0) {
                uint32_t wastedSpace = (pIndex->compressed_size > pIndex->original_size) ? 
                                     0 : (pIndex->original_size - pIndex->compressed_size);
                if (wastedSpace > pIndex->compressed_size * 0.1) {
                    wastedSpacePages++;
                    totalWastedSpace += wastedSpace;
                }
            }
        }
    }
    
    // 更新跟踪字段
    pFile->total_allocated_space = totalAllocated;
    pFile->total_used_space = totalUsed;
    
    // 计算高级碎片化评分（0-100）
    // 因素：浪费空间比率、重用效率、空洞回收效率、顺序写入效率
    if (totalAllocated > 0) {
        uint32_t wastedSpaceScore = (uint32_t)((totalWastedSpace * 30) / totalAllocated); // 0-30分
        
        uint32_t reuseEfficiencyScore = 0;
        uint32_t holeReclaimScore = 0;
        uint32_t sequentialWriteScore = 0;
        
        uint32_t totalOperations = pFile->space_reuse_count + pFile->space_expansion_count + pFile->new_allocation_count;
        if (totalOperations > 0) {
            // 重用效率（0-30分）
            uint32_t reuseRatio = (pFile->space_reuse_count * 100) / totalOperations;
            reuseEfficiencyScore = (100 - reuseRatio) * 30 / 100;
            
            // 空洞回收效率（0-25分）- 良好的空洞回收减少碎片化
            uint32_t holeReclaimRatio = (pFile->hole_reclaim_count * 100) / totalOperations;
            holeReclaimScore = (100 - holeReclaimRatio) * 25 / 100;
            
            // 顺序写入效率（0-15分）- 顺序写入减少碎片化
            if (pageCount > 1) {
                uint32_t sequentialRatio = (pFile->sequential_write_count * 100) / (pageCount - 1);
                sequentialWriteScore = (100 - sequentialRatio) * 15 / 100;
            }
        }
        
        pFile->fragmentation_score = wastedSpaceScore + reuseEfficiencyScore + holeReclaimScore + sequentialWriteScore;
        if (pFile->fragmentation_score > 100) pFile->fragmentation_score = 100;
    } else {
        pFile->fragmentation_score = 0;
    }
    
    CCVFS_DEBUG("Advanced space tracking: allocated=%llu, used=%llu, fragmentation=%u%%, "
               "reuse=%u, expansion=%u, new=%u, holes=%u, bestfit=%u, sequential=%u", 
               (unsigned long long)totalAllocated, (unsigned long long)totalUsed, 
               pFile->fragmentation_score, pFile->space_reuse_count, 
               pFile->space_expansion_count, pFile->new_allocation_count,
               pFile->hole_reclaim_count, pFile->best_fit_count, pFile->sequential_write_count);
    
    // 输出数据完整性统计信息
    // Output data integrity statistics
    if (pFile->checksum_error_count > 0 || pFile->corrupted_page_count > 0) {
        CCVFS_DEBUG("Data integrity stats: checksum_errors=%u, corrupted_pages=%u, "
                   "recovery_attempts=%u, successful_recoveries=%u", 
                   pFile->checksum_error_count, pFile->corrupted_page_count,
                   pFile->recovery_attempt_count, pFile->successful_recovery_count);
    }
}

/*
 * 将数据写入文件
 * 对于CCVFS文件：初始化文件头、使用基于页的写入、处理跨页写入、支持写入缓冲
 * 对于普通文件：直接传递给底层VFS
 * Write data to file
 * For CCVFS files: initialize header, use page-based writing, handle cross-page writes, support write buffering
 * For regular files: pass directly to underlying VFS
 */
int ccvfsIoWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    const unsigned char *data = (const unsigned char*)zBuf;
    int bytesWritten = 0;
    int rc;
    
    CCVFS_DEBUG("=== WRITING %d bytes at offset %lld to file: %s ===", iAmt, iOfst, p->filename ? p->filename : "unknown");
    
    // 对新CCVFS文件的首次写入初始化CCVFS文件头和写入缓冲区
    // Initialize CCVFS header and write buffer for new CCVFS files on first write
    if (p->is_ccvfs_file && p->header_loaded && !p->write_buffer.enabled && p->pOwner) {
        CCVFS_DEBUG("Initializing write buffer for CCVFS file");
        rc = ccvfs_init_write_buffer(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to initialize write buffer: %d", rc);
            return rc;
        }
        CCVFS_DEBUG("Write buffer initialized successfully");
    }
    
    // 如果不是CCVFS文件，直接写入底层文件
    // If not a CCVFS file, write directly to underlying file
    if (!p->is_ccvfs_file) {
        CCVFS_DEBUG("Writing to regular file");
        return p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
    }
    
    // CCVFS文件 - 使用基于页的写入，支持写入缓冲
    // CCVFS file - use page-based writing with buffering support
    uint32_t pageSize = p->header.page_size;
    if (pageSize == 0) {
        CCVFS_ERROR("Invalid page size in header, using default");
        pageSize = CCVFS_DEFAULT_PAGE_SIZE;
        p->header.page_size = pageSize;
    }
    
    uint32_t startPage = getPageNumber(iOfst, pageSize);
    uint32_t startOffset = getPageOffset(iOfst, pageSize);
    
    CCVFS_DEBUG("Page-based write: pageSize=%u, startPage=%u, startOffset=%u", 
               pageSize, startPage, startOffset);
    CCVFS_DEBUG("Current mapping state: total_pages=%u, index_dirty=%d, buffer_enabled=%d", 
               p->header.total_pages, p->index_dirty, p->write_buffer.enabled);
    
    // Allocate page buffer
    unsigned char *pageBuffer = sqlite3_malloc(pageSize);
    if (!pageBuffer) {
        CCVFS_ERROR("Failed to allocate page buffer");
        return SQLITE_NOMEM;
    }
    
    while (bytesWritten < iAmt) {
        uint32_t currentPage = startPage + (bytesWritten + startOffset) / pageSize;
        uint32_t currentOffset = (startOffset + bytesWritten) % pageSize;
        uint32_t bytesToWrite = pageSize - currentOffset;
        
        if (bytesToWrite > (uint32_t)(iAmt - bytesWritten)) {
            bytesToWrite = iAmt - bytesWritten;
        }
        
        CCVFS_DEBUG("Writing iteration: currentPage=%u, currentOffset=%u, bytesToWrite=%u", 
                   currentPage, currentOffset, bytesToWrite);
        
        // 如果我们不是写入一个完整的页，先读取现有数据
        // If we're not writing a full page, read existing data first
        if (currentOffset != 0 || bytesToWrite != pageSize) {
            CCVFS_DEBUG("Partial page write, reading existing data");
            
            // First check buffer for existing data
            rc = ccvfs_buffer_read(p, currentPage, pageBuffer, pageSize);
            if (rc == SQLITE_NOTFOUND) {
                // Not in buffer, try reading from disk
                rc = readPage(p, currentPage, pageBuffer, pageSize);
                if (rc != SQLITE_OK) {
                    // 如果页不存在，用零填充
                    // If page doesn't exist, fill with zeros
                    CCVFS_DEBUG("Page doesn't exist, filling with zeros");
                    memset(pageBuffer, 0, pageSize);
                }
            } else if (rc != SQLITE_OK) {
                CCVFS_ERROR("Failed to read from buffer for partial page write: %d", rc);
                sqlite3_free(pageBuffer);
                return rc;
            } else {
                CCVFS_DEBUG("Used buffered data for partial page write");
            }
        }
        
        // 将新数据复制到页缓冲区
        // Copy new data into page buffer
        memcpy(pageBuffer + currentOffset, data + bytesWritten, bytesToWrite);
        
        CCVFS_DEBUG("Modified page buffer, attempting to buffer/write page %u", currentPage);
        
        // 尝试将页面写入缓冲区，如果失败则直接写入磁盘
        // Try to write page to buffer, if it fails write directly to disk
        rc = ccvfs_buffer_write(p, currentPage, pageBuffer, pageSize);
        if (rc == SQLITE_NOTFOUND) {
            // Write buffering is disabled or not available, write directly
            CCVFS_DEBUG("Write buffering not available, writing page %u directly to disk", currentPage);
            rc = writePage(p, currentPage, pageBuffer, pageSize);
            if (rc != SQLITE_OK) {
                CCVFS_ERROR("Failed to write page %u directly: %d", currentPage, rc);
                sqlite3_free(pageBuffer);
                return rc;
            }
        } else if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to buffer/write page %u: %d", currentPage, rc);
            sqlite3_free(pageBuffer);
            return rc;
        } else {
            CCVFS_DEBUG("Successfully buffered page %u", currentPage);
        }
        
        bytesWritten += bytesToWrite;
        
        CCVFS_DEBUG("Wrote %u bytes, total written: %d/%d", bytesToWrite, bytesWritten, iAmt);
    }
    
    sqlite3_free(pageBuffer);
    
    CCVFS_DEBUG("Write complete: index_dirty=%d, total_pages=%u, buffer_entries=%u", 
               p->index_dirty, p->header.total_pages, p->write_buffer.entry_count);
    CCVFS_VERBOSE("Successfully wrote %d bytes to offset %lld", iAmt, iOfst);
    return SQLITE_OK;
}

/*
 * 将文件截断到指定大小
 * 对于CCVFS文件：更新元数据和页计数
 * 对于普通文件：直接截断底层文件
 * Truncate file to specified size
 * For CCVFS files: update metadata and page count
 * For regular files: truncate underlying file directly
 */
int ccvfsIoTruncate(sqlite3_file *pFile, sqlite3_int64 size) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Truncating file to %lld bytes", size);
    
    // 如果不是CCVFS文件，直接截断底层文件
    // If not a CCVFS file, truncate underlying file directly
    if (!p->is_ccvfs_file) {
        CCVFS_DEBUG("Truncating regular file");
        return p->pReal->pMethods->xTruncate(p->pReal, size);
    }
    
    // CCVFS文件 - 更新元数据
    // CCVFS file - update metadata
    uint32_t pageSize = p->header.page_size;
    uint32_t newPageCount = (uint32_t)((size + pageSize - 1) / pageSize);
    
    // 更新文件头
    // Update header
    p->header.database_size_pages = (uint32_t)(size / pageSize);
    
    // 如果减小大小，我们可以在这里释放未使用的页
    // 现在只更新页计数
    // If reducing size, we could free unused pages here
    // For now, just update the page count
    if (newPageCount < p->header.total_pages) {
        p->header.total_pages = newPageCount;
    }
    
    CCVFS_VERBOSE("CCVFS file truncated to size %lld", size);
    return SQLITE_OK;
}

/*
 * 将文件同步到磁盘
 * 先刷新写入缓冲区、保存CCVFS的页索引和文件头，然后同步底层文件
 * Sync file to disk
 * Flush write buffer first, save CCVFS page index and header, then sync underlying file
 */
int ccvfsIoSync(sqlite3_file *pFile, int flags) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Syncing file with flags %d", flags);
    
    // 如果是CCVFS文件，先刷新写入缓冲区
    // Flush write buffer first if this is a CCVFS file
    if (p->is_ccvfs_file && p->write_buffer.enabled && p->write_buffer.entry_count > 0) {
        CCVFS_DEBUG("Flushing %u buffered entries during sync", p->write_buffer.entry_count);
        int rc = ccvfs_flush_write_buffer(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to flush write buffer during sync: %d", rc);
            return rc;
        }
    }
    
    // 如果是CCVFS文件，保存页索引和文件头
    // Save page index and header if this is a CCVFS file
    if (p->pPageIndex && p->header_loaded) {
        int rc = ccvfs_save_page_index(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to save page index: %d", rc);
            return rc;
        }
        
        rc = ccvfs_save_header(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to save header: %d", rc);
            return rc;
        }
    }
    
    // 执行空洞维护操作（仅对CCVFS文件）
    // Perform hole maintenance operations (only for CCVFS files)
    if (p->is_ccvfs_file && p->hole_manager.enabled) {
        CCVFS_DEBUG("Performing hole maintenance during sync");
        
        // Merge adjacent holes to reduce fragmentation
        ccvfs_merge_adjacent_holes(p);
        
        // Clean up holes that are too small to be useful
        ccvfs_cleanup_small_holes(p);
        
        CCVFS_DEBUG("Hole maintenance completed: %u holes remaining", 
                   p->hole_manager.hole_count);
    }
    
    // 同步底层文件
    // Sync underlying file
    if (p->pReal && p->pReal->pMethods->xSync) {
        int rc = p->pReal->pMethods->xSync(p->pReal, flags);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to sync underlying file: %d", rc);
            return rc;
        }
    }
    
    // 输出同步统计信息
    // Output sync statistics
    if (p->is_ccvfs_file) {
        CCVFS_DEBUG("Sync completed: buffer_flushes=%u, buffer_hits=%u, total_buffered_writes=%u", 
                   p->buffer_flush_count, p->buffer_hit_count, p->total_buffered_writes);
    }
    
    CCVFS_VERBOSE("File synced successfully");
    return SQLITE_OK;
}

/*
 * 获取文件大小
 * 对于CCVFS文件：返回基于页结构的逻辑文件大小
 * 对于普通文件：返回底层文件大小
 * Get file size
 * For CCVFS files: return logical file size based on page structure
 * For regular files: return underlying file size
 */
int ccvfsIoFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Getting file size");
    
    // 如果不是CCVFS文件，从底层文件获取大小
    // If not a CCVFS file, get size from underlying file
    if (!p->is_ccvfs_file) {
        CCVFS_DEBUG("Getting size from regular file");
        return p->pReal->pMethods->xFileSize(p->pReal, pSize);
    }
    
    // 对于CCVFS文件，确保文件头已加载
    // For CCVFS files, ensure header is loaded
    if (!p->header_loaded) {
        // 对于尚未初始化的新文件，初始化文件头
        // For new files that haven't been initialized yet, initialize the header
        int rc = ccvfs_init_header(p, p->pOwner);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to initialize header for file size: %d", rc);
            return rc;
        }
    }
    
    // 基于页结构返回逻辑文件大小
    // Return logical file size based on page structure
    uint32_t pageSize = p->header.page_size;
    if (pageSize == 0) {
        CCVFS_ERROR("Invalid page size in header, using default");
        pageSize = CCVFS_DEFAULT_PAGE_SIZE;
        p->header.page_size = pageSize;
    }
    
    *pSize = (sqlite3_int64)p->header.database_size_pages * pageSize;
    
    CCVFS_VERBOSE("CCVFS file size: %lld bytes", *pSize);
    return SQLITE_OK;
}

/*
 * 锁定文件
 * 直接传递给底层VFS处理
 * Lock file
 * Pass through to underlying VFS
 */
int ccvfsIoLock(sqlite3_file *pFile, int eLock) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Locking file with level %d", eLock);
    
    if (p->pReal && p->pReal->pMethods->xLock) {
        return p->pReal->pMethods->xLock(p->pReal, eLock);
    }
    
    return SQLITE_OK;
}

/*
 * 解锁文件
 * 直接传递给底层VFS处理
 * Unlock file
 * Pass through to underlying VFS
 */
int ccvfsIoUnlock(sqlite3_file *pFile, int eLock) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Unlocking file with level %d", eLock);
    
    if (p->pReal && p->pReal->pMethods->xUnlock) {
        return p->pReal->pMethods->xUnlock(p->pReal, eLock);
    }
    
    return SQLITE_OK;
}

/*
 * 检查保留锁
 * 直接传递给底层VFS处理
 * Check reserved lock
 * Pass through to underlying VFS
 */
int ccvfsIoCheckReservedLock(sqlite3_file *pFile, int *pResOut) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Checking reserved lock");
    
    if (p->pReal && p->pReal->pMethods->xCheckReservedLock) {
        return p->pReal->pMethods->xCheckReservedLock(p->pReal, pResOut);
    }
    
    *pResOut = 0;
    return SQLITE_OK;
}

/*
 * 文件控制操作
 * 处理CCVFS特定操作，其他传递给底层VFS
 * File control operations
 * Handle CCVFS specific operations, others pass through to underlying VFS
 */
int ccvfsIoFileControl(sqlite3_file *pFile, int op, void *pArg) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("File control operation %d", op);
    
    // 在这里处理CCVFS特定操作（如果需要）
    // Handle CCVFS specific operations here if needed
    
    if (p->pReal && p->pReal->pMethods->xFileControl) {
        return p->pReal->pMethods->xFileControl(p->pReal, op, pArg);
    }
    
    return SQLITE_NOTFOUND;
}

/*
 * 获取扇区大小
 * 从底层VFS获取或返回默认值
 * Get sector size
 * Get from underlying VFS or return default
 */
int ccvfsIoSectorSize(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xSectorSize) {
        return p->pReal->pMethods->xSectorSize(p->pReal);
    }
    
    return 4096; // 默认扇区大小
}

/*
 * 获取设备特性
 * 从底层VFS获取或返回默认值
 * Get device characteristics
 * Get from underlying VFS or return default
 */
int ccvfsIoDeviceCharacteristics(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xDeviceCharacteristics) {
        return p->pReal->pMethods->xDeviceCharacteristics(p->pReal);
    }
    
    return 0;
}

/*
 * 共享内存映射 - 传递给底层VFS
 * Shared memory map - pass through to underlying VFS
 */
int ccvfsIoShmMap(sqlite3_file *pFile, int iPg, int pgsz, int bExtend, void volatile **pp) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xShmMap) {
        return p->pReal->pMethods->xShmMap(p->pReal, iPg, pgsz, bExtend, pp);
    }
    
    return SQLITE_IOERR_SHMMAP;
}

/*
 * 共享内存锁 - 传递给底层VFS
 * Shared memory lock - pass through to underlying VFS
 */
int ccvfsIoShmLock(sqlite3_file *pFile, int offset, int n, int flags) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xShmLock) {
        return p->pReal->pMethods->xShmLock(p->pReal, offset, n, flags);
    }
    
    return SQLITE_IOERR_SHMLOCK;
}

/*
 * 共享内存屏障 - 传递给底层VFS
 * Shared memory barrier - pass through to underlying VFS
 */
void ccvfsIoShmBarrier(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xShmBarrier) {
        p->pReal->pMethods->xShmBarrier(p->pReal);
    }
}

/*
 * 共享内存取消映射 - 传递给底层VFS
 * Shared memory unmap - pass through to underlying VFS
 */
int ccvfsIoShmUnmap(sqlite3_file *pFile, int deleteFlag) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xShmUnmap) {
        return p->pReal->pMethods->xShmUnmap(p->pReal, deleteFlag);
    }
    
    return SQLITE_OK;
}

/*
 * 获取页面 - 压缩VFS不支持此操作
 * Fetch page - not supported for compressed VFS
 */
int ccvfsIoFetch(sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp) {
    CCVFS_DEBUG("Fetch operation not supported for compressed VFS");
    return SQLITE_IOERR;
}

/*
 * 释放页面 - 压缩VFS不支持此操作
 * Unfetch page - not supported for compressed VFS
 */
int ccvfsIoUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage) {
    CCVFS_DEBUG("Unfetch operation not supported for compressed VFS");
    return SQLITE_OK;
}

// ============================================================================
// WRITE BUFFER MANAGEMENT IMPLEMENTATION
// ============================================================================

/*
 * 初始化写入缓冲管理器
 * Initialize write buffer manager with default configuration
 */
int ccvfs_init_write_buffer(CCVFSFile *pFile) {
    CCVFSWriteBuffer *pBuffer = &pFile->write_buffer;
    
    CCVFS_DEBUG("Initializing write buffer for file: %s", pFile->filename ? pFile->filename : "unknown");
    
    // Initialize write buffer structure
    memset(pBuffer, 0, sizeof(CCVFSWriteBuffer));
    
    // Set configuration from VFS settings
    if (pFile->pOwner) {
        pBuffer->enabled = pFile->pOwner->enable_write_buffer;
        pBuffer->max_entries = pFile->pOwner->max_buffer_entries;
        pBuffer->max_buffer_size = pFile->pOwner->max_buffer_size;
        pBuffer->auto_flush_pages = pFile->pOwner->auto_flush_pages;
    } else {
        // Use defaults if no VFS owner
        pBuffer->enabled = CCVFS_DEFAULT_BUFFER_ENABLED;
        pBuffer->max_entries = CCVFS_DEFAULT_MAX_BUFFER_ENTRIES;
        pBuffer->max_buffer_size = CCVFS_DEFAULT_MAX_BUFFER_SIZE;
        pBuffer->auto_flush_pages = CCVFS_DEFAULT_AUTO_FLUSH_PAGES;
    }
    
    // Validate configuration parameters
    if (pBuffer->max_entries < CCVFS_MIN_BUFFER_ENTRIES) {
        CCVFS_DEBUG("Adjusting max_entries from %u to minimum %u", 
                   pBuffer->max_entries, CCVFS_MIN_BUFFER_ENTRIES);
        pBuffer->max_entries = CCVFS_MIN_BUFFER_ENTRIES;
    } else if (pBuffer->max_entries > CCVFS_MAX_BUFFER_ENTRIES) {
        CCVFS_DEBUG("Adjusting max_entries from %u to maximum %u", 
                   pBuffer->max_entries, CCVFS_MAX_BUFFER_ENTRIES);
        pBuffer->max_entries = CCVFS_MAX_BUFFER_ENTRIES;
    }
    
    if (pBuffer->max_buffer_size < CCVFS_MIN_BUFFER_SIZE) {
        CCVFS_DEBUG("Adjusting max_buffer_size from %u to minimum %u", 
                   pBuffer->max_buffer_size, CCVFS_MIN_BUFFER_SIZE);
        pBuffer->max_buffer_size = CCVFS_MIN_BUFFER_SIZE;
    } else if (pBuffer->max_buffer_size > CCVFS_MAX_BUFFER_SIZE) {
        CCVFS_DEBUG("Adjusting max_buffer_size from %u to maximum %u", 
                   pBuffer->max_buffer_size, CCVFS_MAX_BUFFER_SIZE);
        pBuffer->max_buffer_size = CCVFS_MAX_BUFFER_SIZE;
    }
    
    // Initialize buffer list
    pBuffer->entries = NULL;
    pBuffer->entry_count = 0;
    pBuffer->buffer_size = 0;
    pBuffer->last_flush_time = 0;
    
    // Initialize buffer statistics
    pFile->buffer_hit_count = 0;
    pFile->buffer_flush_count = 0;
    pFile->buffer_merge_count = 0;
    pFile->total_buffered_writes = 0;
    
    CCVFS_INFO("Write buffer initialized: enabled=%d, max_entries=%u, max_size=%u KB, auto_flush=%u", 
              pBuffer->enabled, pBuffer->max_entries, pBuffer->max_buffer_size / 1024, pBuffer->auto_flush_pages);
    
    return SQLITE_OK;
}

/*
 * 清理写入缓冲管理器
 * Clean up write buffer manager and free all allocated memory
 */
void ccvfs_cleanup_write_buffer(CCVFSFile *pFile) {
    CCVFSWriteBuffer *pBuffer = &pFile->write_buffer;
    CCVFSBufferEntry *pEntry, *pNext;
    
    CCVFS_DEBUG("Cleaning up write buffer for file: %s", pFile->filename ? pFile->filename : "unknown");
    
    if (!pBuffer) {
        return;
    }
    
    // Flush any remaining buffered data before cleanup
    if (pBuffer->enabled && pBuffer->entry_count > 0) {
        CCVFS_DEBUG("Flushing %u buffered entries before cleanup", pBuffer->entry_count);
        ccvfs_flush_write_buffer(pFile);
    }
    
    // Free all entries in the buffer
    pEntry = pBuffer->entries;
    while (pEntry) {
        pNext = pEntry->next;
        if (pEntry->data) {
            sqlite3_free(pEntry->data);
        }
        sqlite3_free(pEntry);
        pEntry = pNext;
    }
    
    // Report final statistics
    if (1 || pBuffer->entry_count > 0 || pFile->total_buffered_writes > 0) {
        CCVFS_INFO("Write buffer cleanup stats: entries=%u, hits=%u, flushes=%u, merges=%u, total_writes=%u",
                  pBuffer->entry_count, pFile->buffer_hit_count, 
                  pFile->buffer_flush_count, pFile->buffer_merge_count, pFile->total_buffered_writes);
    }
    
    // Reset buffer structure
    memset(pBuffer, 0, sizeof(CCVFSWriteBuffer));
    
    // Reset buffer statistics
    pFile->buffer_hit_count = 0;
    pFile->buffer_flush_count = 0;
    pFile->buffer_merge_count = 0;
    pFile->total_buffered_writes = 0;
    
    CCVFS_DEBUG("Write buffer cleanup completed");
}

/*
 * 在缓冲区中查找指定页面的条目
 * Find buffer entry for specified page number
 */
static CCVFSBufferEntry* ccvfs_find_buffer_entry(CCVFSFile *pFile, uint32_t pageNum) {
    CCVFSWriteBuffer *pBuffer = &pFile->write_buffer;
    CCVFSBufferEntry *pEntry;
    
    if (!pBuffer->enabled || pBuffer->entry_count == 0) {
        return NULL;
    }
    
    pEntry = pBuffer->entries;
    while (pEntry) {
        if (pEntry->page_number == pageNum) {
            CCVFS_DEBUG("Found buffered entry for page %u", pageNum);
            return pEntry;
        }
        pEntry = pEntry->next;
    }
    
    return NULL;
}

/*
 * 移除缓冲区中的指定条目
 * Remove specified entry from buffer
 */
static int ccvfs_remove_buffer_entry(CCVFSFile *pFile, CCVFSBufferEntry *pTargetEntry) {
    CCVFSWriteBuffer *pBuffer = &pFile->write_buffer;
    CCVFSBufferEntry *pEntry, *pPrev;
    
    if (!pBuffer->enabled || !pTargetEntry) {
        return SQLITE_OK;
    }
    
    pEntry = pBuffer->entries;
    pPrev = NULL;
    
    while (pEntry) {
        if (pEntry == pTargetEntry) {
            // Remove from list
            if (pPrev) {
                pPrev->next = pEntry->next;
            } else {
                pBuffer->entries = pEntry->next;
            }
            
            // Update buffer statistics
            pBuffer->entry_count--;
            pBuffer->buffer_size -= pEntry->data_size;
            
            // Free memory
            if (pEntry->data) {
                sqlite3_free(pEntry->data);
            }
            sqlite3_free(pEntry);
            
            CCVFS_DEBUG("Removed buffer entry for page %u, remaining entries: %u", 
                       pTargetEntry->page_number, pBuffer->entry_count);
            return SQLITE_OK;
        }
        pPrev = pEntry;
        pEntry = pEntry->next;
    }
    
    CCVFS_ERROR("Buffer entry not found in list");
    return SQLITE_ERROR;
}

/*
 * 将页面写入缓冲区
 * Write page data to buffer
 */
int ccvfs_buffer_write(CCVFSFile *pFile, uint32_t pageNum, const unsigned char *data, uint32_t dataSize) {
    CCVFSWriteBuffer *pBuffer = &pFile->write_buffer;
    CCVFSBufferEntry *pEntry;
    
    CCVFS_DEBUG("Buffering write for page %u, size %u bytes", pageNum, dataSize);
    
    // Check if buffering is enabled
    if (!pBuffer->enabled) {
        CCVFS_DEBUG("Write buffering disabled, not buffering page %u", pageNum);
        return SQLITE_NOTFOUND;  // Indicate caller should write directly
    }
    
    // Check if we need to flush buffer before adding new entry
    if (pBuffer->entry_count >= pBuffer->max_entries || 
        (pBuffer->buffer_size + dataSize) > pBuffer->max_buffer_size) {
        CCVFS_DEBUG("Buffer full (entries: %u/%u, size: %u/%u), flushing before new write", 
                   pBuffer->entry_count, pBuffer->max_entries,
                   pBuffer->buffer_size, pBuffer->max_buffer_size);
        
        int rc = ccvfs_flush_write_buffer(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to flush buffer before new write: %d", rc);
            return rc;
        }
    }
    
    // Check if this page is already in buffer (update existing entry)
    pEntry = ccvfs_find_buffer_entry(pFile, pageNum);
    if (pEntry) {
        CCVFS_DEBUG("Updating existing buffer entry for page %u", pageNum);
        
        // Update buffer size statistics
        pBuffer->buffer_size -= pEntry->data_size;
        pBuffer->buffer_size += dataSize;
        
        // Reallocate data if size changed
        if (pEntry->data_size != dataSize) {
            sqlite3_free(pEntry->data);
            pEntry->data = sqlite3_malloc(dataSize);
            if (!pEntry->data) {
                CCVFS_ERROR("Failed to allocate memory for buffer entry update");
                return SQLITE_NOMEM;
            }
        }
        
        // Copy new data
        memcpy(pEntry->data, data, dataSize);
        pEntry->data_size = dataSize;
        pEntry->is_dirty = 1;
        
        pFile->buffer_merge_count++;
        pFile->total_buffered_writes++;
        
        CCVFS_DEBUG("Updated buffer entry for page %u, merge count: %u", pageNum, pFile->buffer_merge_count);
        return SQLITE_OK;
    }
    
    // Create new buffer entry
    pEntry = (CCVFSBufferEntry*)sqlite3_malloc(sizeof(CCVFSBufferEntry));
    if (!pEntry) {
        CCVFS_ERROR("Failed to allocate memory for new buffer entry");
        return SQLITE_NOMEM;
    }
    
    pEntry->data = sqlite3_malloc(dataSize);
    if (!pEntry->data) {
        CCVFS_ERROR("Failed to allocate memory for buffer entry data");
        sqlite3_free(pEntry);
        return SQLITE_NOMEM;
    }
    
    // Initialize entry
    pEntry->page_number = pageNum;
    pEntry->data_size = dataSize;
    pEntry->is_dirty = 1;
    memcpy(pEntry->data, data, dataSize);
    
    // Add to front of list
    pEntry->next = pBuffer->entries;
    pBuffer->entries = pEntry;
    
    // Update buffer statistics
    pBuffer->entry_count++;
    pBuffer->buffer_size += dataSize;
    pFile->total_buffered_writes++;
    
    CCVFS_DEBUG("Added new buffer entry for page %u, total entries: %u, buffer size: %u", 
               pageNum, pBuffer->entry_count, pBuffer->buffer_size);
    
    // Check if we should auto-flush
    if (pBuffer->auto_flush_pages > 0 && pBuffer->entry_count >= pBuffer->auto_flush_pages) {
        CCVFS_DEBUG("Auto-flush triggered: %u >= %u pages", pBuffer->entry_count, pBuffer->auto_flush_pages);
        int rc = ccvfs_flush_write_buffer(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Auto-flush failed: %d", rc);
            return rc;
        }
    }
    
    return SQLITE_OK;
}

/*
 * 从缓冲区读取页面数据
 * Read page data from buffer if available
 */
int ccvfs_buffer_read(CCVFSFile *pFile, uint32_t pageNum, unsigned char *buffer, uint32_t bufferSize) {
    CCVFSWriteBuffer *pWriteBuffer = &pFile->write_buffer;
    CCVFSBufferEntry *pEntry;
    
    CCVFS_DEBUG("Checking buffer for page %u read", pageNum);
    
    // Check if buffering is enabled
    if (!pWriteBuffer->enabled) {
        return SQLITE_NOTFOUND;  // Indicate caller should read from disk
    }
    
    // Look for the page in buffer
    pEntry = ccvfs_find_buffer_entry(pFile, pageNum);
    if (!pEntry) {
        CCVFS_DEBUG("Page %u not found in buffer", pageNum);
        return SQLITE_NOTFOUND;  // Indicate caller should read from disk
    }
    
    // Check buffer size compatibility
    if (pEntry->data_size > bufferSize) {
        CCVFS_ERROR("Buffer entry size %u exceeds read buffer size %u", pEntry->data_size, bufferSize);
        return SQLITE_ERROR;
    }
    
    // Copy data from buffer
    memcpy(buffer, pEntry->data, pEntry->data_size);
    
    // Zero-fill remaining space if needed
    if (pEntry->data_size < bufferSize) {
        memset(buffer + pEntry->data_size, 0, bufferSize - pEntry->data_size);
    }
    
    pFile->buffer_hit_count++;
    
    CCVFS_DEBUG("Buffer hit for page %u, hit count: %u", pageNum, pFile->buffer_hit_count);
    return SQLITE_OK;
}

/*
 * 刷新指定页面的缓冲条目到磁盘
 * Flush specific buffer entry to disk
 */
int ccvfs_flush_buffer_entry(CCVFSFile *pFile, uint32_t pageNum) {
    CCVFSBufferEntry *pEntry;
    int rc;
    
    CCVFS_DEBUG("Flushing buffer entry for page %u", pageNum);
    
    pEntry = ccvfs_find_buffer_entry(pFile, pageNum);
    if (!pEntry) {
        CCVFS_DEBUG("No buffer entry found for page %u", pageNum);
        return SQLITE_OK;  // Nothing to flush
    }
    
    if (!pEntry->is_dirty) {
        CCVFS_DEBUG("Buffer entry for page %u is not dirty, skipping", pageNum);
        return SQLITE_OK;
    }
    
    // Write the page directly using the original writePage function
    rc = writePage(pFile, pEntry->page_number, pEntry->data, pEntry->data_size);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to flush buffer entry for page %u: %d", pageNum, rc);
        return rc;
    }
    
    // Mark as clean but keep in buffer for potential reads
    pEntry->is_dirty = 0;
    
    CCVFS_DEBUG("Successfully flushed buffer entry for page %u", pageNum);
    return SQLITE_OK;
}

/*
 * 刷新所有缓冲区条目到磁盘
 * Flush all buffer entries to disk
 */
int ccvfs_flush_write_buffer(CCVFSFile *pFile) {
    CCVFSWriteBuffer *pBuffer = &pFile->write_buffer;
    CCVFSBufferEntry *pEntry;
    int rc = SQLITE_OK;
    int flushed_count = 0;
    int error_count = 0;
    
    CCVFS_DEBUG("Flushing write buffer: %u entries", pBuffer->entry_count);
    
    if (!pBuffer->enabled || pBuffer->entry_count == 0) {
        CCVFS_DEBUG("Buffer flush not needed: enabled=%d, entry_count=%u", 
                   pBuffer->enabled, pBuffer->entry_count);
        return SQLITE_OK;
    }
    
    // Flush all dirty entries
    pEntry = pBuffer->entries;
    while (pEntry) {
        if (pEntry->is_dirty) {
            int flush_rc = writePage(pFile, pEntry->page_number, pEntry->data, pEntry->data_size);
            if (flush_rc == SQLITE_OK) {
                pEntry->is_dirty = 0;  // Mark as clean
                flushed_count++;
                CCVFS_DEBUG("Flushed buffered page %u", pEntry->page_number);
            } else {
                CCVFS_ERROR("Failed to flush buffered page %u: %d", pEntry->page_number, flush_rc);
                error_count++;
                if (rc == SQLITE_OK) {
                    rc = flush_rc;  // Remember first error
                }
            }
        }
        pEntry = pEntry->next;
    }
    
    // Update statistics
    pFile->buffer_flush_count++;
    pBuffer->last_flush_time = time(NULL);
    
    if (error_count > 0) {
        CCVFS_ERROR("Buffer flush completed with errors: flushed=%d, errors=%d", flushed_count, error_count);
    } else {
        CCVFS_DEBUG("Buffer flush completed successfully: flushed=%d pages", flushed_count);
    }
    
    // Optional: Clear buffer after successful flush (configurable behavior)
    // For now, keep entries in buffer for potential reads
    
    return rc;
}

// ============================================================================
// HOLE MANAGEMENT IMPLEMENTATION
// ============================================================================

/*
 * 初始化空洞管理器
 * Initialize hole manager with default configuration
 */
int ccvfs_init_hole_manager(CCVFSFile *pFile) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    
    CCVFS_DEBUG("Initializing hole manager for file: %s", pFile->filename ? pFile->filename : "unknown");
    
    // Initialize hole manager structure
    memset(pManager, 0, sizeof(CCVFSHoleManager));
    
    // Set configuration from VFS settings
    if (pFile->pOwner) {
        pManager->enabled = pFile->pOwner->enable_hole_detection;
        pManager->max_holes = pFile->pOwner->max_holes;
        pManager->min_hole_size = pFile->pOwner->min_hole_size;
    } else {
        // Use defaults if no VFS owner
        pManager->enabled = 1;
        pManager->max_holes = CCVFS_DEFAULT_MAX_HOLES;
        pManager->min_hole_size = CCVFS_DEFAULT_MIN_HOLE_SIZE;
    }
    
    // Validate configuration parameters
    if (pManager->max_holes < CCVFS_MIN_MAX_HOLES) {
        CCVFS_DEBUG("Adjusting max_holes from %u to minimum %u", 
                   pManager->max_holes, CCVFS_MIN_MAX_HOLES);
        pManager->max_holes = CCVFS_MIN_MAX_HOLES;
    } else if (pManager->max_holes > CCVFS_MAX_MAX_HOLES) {
        CCVFS_DEBUG("Adjusting max_holes from %u to maximum %u", 
                   pManager->max_holes, CCVFS_MAX_MAX_HOLES);
        pManager->max_holes = CCVFS_MAX_MAX_HOLES;
    }
    
    if (pManager->min_hole_size < CCVFS_MIN_HOLE_SIZE) {
        CCVFS_DEBUG("Adjusting min_hole_size from %u to minimum %u", 
                   pManager->min_hole_size, CCVFS_MIN_HOLE_SIZE);
        pManager->min_hole_size = CCVFS_MIN_HOLE_SIZE;
    } else if (pManager->min_hole_size > CCVFS_MAX_HOLE_SIZE) {
        CCVFS_DEBUG("Adjusting min_hole_size from %u to maximum %u", 
                   pManager->min_hole_size, CCVFS_MAX_HOLE_SIZE);
        pManager->min_hole_size = CCVFS_MAX_HOLE_SIZE;
    }
    
    // Initialize hole list
    pManager->holes = NULL;
    pManager->hole_count = 0;
    
    // Initialize hole statistics
    pFile->hole_allocation_count = 0;
    pFile->hole_merge_count = 0;
    pFile->hole_cleanup_count = 0;
    pFile->hole_operations_count = 0;
    
    CCVFS_INFO("Hole manager initialized: enabled=%d, max_holes=%u, min_hole_size=%u", 
              pManager->enabled, pManager->max_holes, pManager->min_hole_size);
    
    return SQLITE_OK;
}

/*
 * 清理空洞管理器
 * Clean up hole manager and free all allocated memory
 */
void ccvfs_cleanup_hole_manager(CCVFSFile *pFile) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    CCVFSSpaceHole *pHole, *pNext;
    
    CCVFS_DEBUG("Cleaning up hole manager for file: %s", pFile->filename ? pFile->filename : "unknown");
    
    if (!pManager) {
        return;
    }
    
    // Free all holes in the linked list
    pHole = pManager->holes;
    while (pHole) {
        pNext = pHole->next;
        sqlite3_free(pHole);
        pHole = pNext;
    }
    
    // Report final statistics
    if (1 || pManager->hole_count > 0 || pFile->hole_allocation_count > 0) {
        CCVFS_INFO("Hole manager cleanup stats: tracked_holes=%u, allocations=%u, merges=%u, cleanups=%u",
                  pManager->hole_count, pFile->hole_allocation_count, 
                  pFile->hole_merge_count, pFile->hole_cleanup_count);
    }
    
    // Reset hole manager structure
    memset(pManager, 0, sizeof(CCVFSHoleManager));
    
    // Reset hole statistics
    pFile->hole_allocation_count = 0;
    pFile->hole_merge_count = 0;
    pFile->hole_cleanup_count = 0;
    
    CCVFS_DEBUG("Hole manager cleanup completed");
}

/*
 * 添加空洞到跟踪列表
 * Add a hole to the tracking list with proper sorting and merging
 */
int ccvfs_add_hole(CCVFSFile *pFile, sqlite3_int64 offset, uint32_t size) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    CCVFSSpaceHole *pNewHole, *pCurrent, *pPrev;
    
    CCVFS_DEBUG("Adding hole: offset=%llu, size=%u", (unsigned long long)offset, size);
    
    // Check if hole detection is enabled
    if (!pManager->enabled) {
        CCVFS_DEBUG("Hole detection disabled, ignoring hole");
        return SQLITE_OK;
    }
    
    // Validate hole parameters
    if (offset < 0 || size == 0) {
        CCVFS_ERROR("Invalid hole parameters: offset=%lld, size=%u", offset, size);
        return SQLITE_ERROR;
    }
    
    // Check if hole is too small to track
    if (size < pManager->min_hole_size) {
        CCVFS_DEBUG("Hole too small to track: size=%u < min=%u", size, pManager->min_hole_size);
        return SQLITE_OK;
    }
    
    // Check for overlaps with existing holes and merge if adjacent
    pCurrent = pManager->holes;
    pPrev = NULL;
    
    while (pCurrent) {
        sqlite3_int64 currentEnd = pCurrent->offset + pCurrent->size;
        sqlite3_int64 newEnd = offset + size;
        
        // Check for overlap or adjacency
        if ((offset <= currentEnd && newEnd >= pCurrent->offset) ||
            (offset == currentEnd) || (newEnd == pCurrent->offset)) {
            
            // Merge with existing hole
            sqlite3_int64 mergedStart = (offset < pCurrent->offset) ? offset : pCurrent->offset;
            sqlite3_int64 mergedEnd = (newEnd > currentEnd) ? newEnd : currentEnd;
            uint32_t mergedSize = (uint32_t)(mergedEnd - mergedStart);
            
            CCVFS_DEBUG("Merging holes: existing[%llu,%u] + new[%llu,%u] = merged[%llu,%u]",
                       (unsigned long long)pCurrent->offset, pCurrent->size,
                       (unsigned long long)offset, size,
                       (unsigned long long)mergedStart, mergedSize);
            
            // Update existing hole with merged parameters
            pCurrent->offset = mergedStart;
            pCurrent->size = mergedSize;
            
            pFile->hole_merge_count++;
            
            // Check if we need to merge with the next hole too
            CCVFSSpaceHole *pNext = pCurrent->next;
            if (pNext && (mergedStart + mergedSize) >= pNext->offset) {
                // Merge with next hole as well
                sqlite3_int64 nextEnd = pNext->offset + pNext->size;
                if (nextEnd > mergedEnd) {
                    mergedEnd = nextEnd;
                }
                pCurrent->size = (uint32_t)(mergedEnd - mergedStart);
                
                // Remove the next hole from the list
                pCurrent->next = pNext->next;
                sqlite3_free(pNext);
                pManager->hole_count--;
                
                CCVFS_DEBUG("Triple merge completed: final hole[%llu,%u]",
                           (unsigned long long)pCurrent->offset, pCurrent->size);
            }
            
            return SQLITE_OK;
        }
        
        // If we've passed the insertion point, break
        if (pCurrent->offset > offset) {
            break;
        }
        
        pPrev = pCurrent;
        pCurrent = pCurrent->next;
    }
    
    // Check if we've reached the maximum number of holes
    if (pManager->hole_count >= pManager->max_holes) {
        CCVFS_DEBUG("Maximum holes reached (%u), removing smallest hole", pManager->max_holes);
        
        // Find and remove the smallest hole
        CCVFSSpaceHole *pSmallest = pManager->holes;
        CCVFSSpaceHole *pSmallestPrev = NULL;
        CCVFSSpaceHole *pSearch = pManager->holes;
        CCVFSSpaceHole *pSearchPrev = NULL;
        
        while (pSearch) {
            if (pSearch->size < pSmallest->size) {
                pSmallest = pSearch;
                pSmallestPrev = pSearchPrev;
            }
            pSearchPrev = pSearch;
            pSearch = pSearch->next;
        }
        
        // Only remove if the new hole is larger than the smallest
        if (size > pSmallest->size) {
            CCVFS_DEBUG("Removing smallest hole[%llu,%u] to make room for new hole[%llu,%u]",
                       (unsigned long long)pSmallest->offset, pSmallest->size,
                       (unsigned long long)offset, size);
            
            if (pSmallestPrev) {
                pSmallestPrev->next = pSmallest->next;
            } else {
                pManager->holes = pSmallest->next;
            }
            sqlite3_free(pSmallest);
            pManager->hole_count--;
            pFile->hole_cleanup_count++;
        } else {
            CCVFS_DEBUG("New hole[%llu,%u] not larger than smallest[%llu,%u], ignoring",
                       (unsigned long long)offset, size,
                       (unsigned long long)pSmallest->offset, pSmallest->size);
            return SQLITE_OK;
        }
    }
    
    // Allocate new hole structure
    pNewHole = (CCVFSSpaceHole*)sqlite3_malloc(sizeof(CCVFSSpaceHole));
    if (!pNewHole) {
        CCVFS_ERROR("Failed to allocate memory for new hole");
        return SQLITE_NOMEM;
    }
    
    // Initialize new hole
    pNewHole->offset = offset;
    pNewHole->size = size;
    pNewHole->next = pCurrent;
    
    // Insert into sorted list
    if (pPrev) {
        pPrev->next = pNewHole;
    } else {
        pManager->holes = pNewHole;
    }
    
    pManager->hole_count++;
    
    CCVFS_DEBUG("Successfully added hole[%llu,%u], total holes: %u",
               (unsigned long long)offset, size, pManager->hole_count);
    
    // Check if maintenance is needed
    ccvfs_check_hole_maintenance_threshold(pFile);
    
    return SQLITE_OK;
}/*
 *
 从跟踪列表中移除空洞或分割空洞
 * Remove hole from tracking list or split hole if only part is used
 */
static int ccvfs_remove_hole(CCVFSFile *pFile, sqlite3_int64 offset) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    CCVFSSpaceHole *pCurrent, *pPrev;
    
    CCVFS_DEBUG("Removing/updating hole at offset: %llu", (unsigned long long)offset);
    
    // Check if hole detection is enabled
    if (!pManager->enabled) {
        CCVFS_DEBUG("Hole detection disabled, ignoring remove request");
        return SQLITE_OK;
    }
    
    // Find the hole that contains this offset
    pCurrent = pManager->holes;
    pPrev = NULL;
    
    while (pCurrent) {
        sqlite3_int64 holeEnd = pCurrent->offset + pCurrent->size;
        
        // Check if the offset falls within this hole
        if (offset >= pCurrent->offset && offset < holeEnd) {
            CCVFS_DEBUG("Found hole[%llu,%u] containing offset %llu",
                       (unsigned long long)pCurrent->offset, pCurrent->size,
                       (unsigned long long)offset);
            
            // Remove the entire hole from the list
            if (pPrev) {
                pPrev->next = pCurrent->next;
            } else {
                pManager->holes = pCurrent->next;
            }
            
            sqlite3_free(pCurrent);
            pManager->hole_count--;
            
            CCVFS_DEBUG("Removed hole, remaining holes: %u", pManager->hole_count);
            return SQLITE_OK;
        }
        
        pPrev = pCurrent;
        pCurrent = pCurrent->next;
    }
    
    CCVFS_DEBUG("No hole found containing offset %llu", (unsigned long long)offset);
    return SQLITE_OK;
}

/*
 * 从跟踪列表中移除或分割指定大小的空洞
 * Remove or split hole when allocating specific size from it
 */
int ccvfs_allocate_from_hole(CCVFSFile *pFile, sqlite3_int64 offset, uint32_t allocSize) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    CCVFSSpaceHole *pCurrent, *pPrev;
    
    CCVFS_DEBUG("Allocating %u bytes from hole at offset: %llu", allocSize, (unsigned long long)offset);
    
    // Check if hole detection is enabled
    if (!pManager->enabled) {
        CCVFS_DEBUG("Hole detection disabled, ignoring allocation request");
        return SQLITE_OK;
    }
    
    // Find the hole that contains this offset
    pCurrent = pManager->holes;
    pPrev = NULL;
    
    while (pCurrent) {
        sqlite3_int64 holeEnd = pCurrent->offset + pCurrent->size;
        
        // Check if the offset falls within this hole and we have enough space
        if (offset >= pCurrent->offset && offset < holeEnd && 
            (offset + allocSize) <= holeEnd) {
            
            CCVFS_DEBUG("Found suitable hole[%llu,%u] for allocation[%llu,%u]",
                       (unsigned long long)pCurrent->offset, pCurrent->size,
                       (unsigned long long)offset, allocSize);
            
            // Calculate remaining space before and after allocation
            uint32_t spaceBefore = (uint32_t)(offset - pCurrent->offset);
            uint32_t spaceAfter = (uint32_t)(holeEnd - (offset + allocSize));
            
            CCVFS_DEBUG("Space before: %u, space after: %u", spaceBefore, spaceAfter);
            
            // Case 1: Allocation uses the entire hole
            if (spaceBefore == 0 && spaceAfter == 0) {
                CCVFS_DEBUG("Allocation uses entire hole, removing hole");
                
                if (pPrev) {
                    pPrev->next = pCurrent->next;
                } else {
                    pManager->holes = pCurrent->next;
                }
                
                sqlite3_free(pCurrent);
                pManager->hole_count--;
                
            } 
            // Case 2: Allocation at the beginning, keep the end part
            else if (spaceBefore == 0 && spaceAfter >= pManager->min_hole_size) {
                CCVFS_DEBUG("Allocation at beginning, keeping end part[%llu,%u]",
                           (unsigned long long)(offset + allocSize), spaceAfter);
                
                pCurrent->offset = offset + allocSize;
                pCurrent->size = spaceAfter;
                
            }
            // Case 3: Allocation at the end, keep the beginning part
            else if (spaceAfter == 0 && spaceBefore >= pManager->min_hole_size) {
                CCVFS_DEBUG("Allocation at end, keeping beginning part[%llu,%u]",
                           (unsigned long long)pCurrent->offset, spaceBefore);
                
                pCurrent->size = spaceBefore;
                
            }
            // Case 4: Allocation in the middle, split into two holes
            else if (spaceBefore >= pManager->min_hole_size && spaceAfter >= pManager->min_hole_size) {
                CCVFS_DEBUG("Allocation in middle, splitting into two holes");
                
                // Create new hole for the space after allocation
                CCVFSSpaceHole *pNewHole = (CCVFSSpaceHole*)sqlite3_malloc(sizeof(CCVFSSpaceHole));
                if (!pNewHole) {
                    CCVFS_ERROR("Failed to allocate memory for split hole");
                    return SQLITE_NOMEM;
                }
                
                pNewHole->offset = offset + allocSize;
                pNewHole->size = spaceAfter;
                pNewHole->next = pCurrent->next;
                
                // Update current hole to keep the space before allocation
                pCurrent->size = spaceBefore;
                pCurrent->next = pNewHole;
                
                pManager->hole_count++;
                
                CCVFS_DEBUG("Split complete: hole1[%llu,%u], hole2[%llu,%u]",
                           (unsigned long long)pCurrent->offset, pCurrent->size,
                           (unsigned long long)pNewHole->offset, pNewHole->size);
                
            }
            // Case 5: One or both remaining parts are too small, remove entire hole
            else {
                CCVFS_DEBUG("Remaining parts too small, removing entire hole");
                
                if (pPrev) {
                    pPrev->next = pCurrent->next;
                } else {
                    pManager->holes = pCurrent->next;
                }
                
                sqlite3_free(pCurrent);
                pManager->hole_count--;
                pFile->hole_cleanup_count++;
            }
            
            pFile->hole_allocation_count++;
            CCVFS_DEBUG("Hole allocation completed, remaining holes: %u", pManager->hole_count);
            
            // Check if maintenance is needed
            ccvfs_check_hole_maintenance_threshold(pFile);
            
            return SQLITE_OK;
        }
        
        pPrev = pCurrent;
        pCurrent = pCurrent->next;
    }
    
    CCVFS_DEBUG("No suitable hole found for allocation at offset %llu", (unsigned long long)offset);
    return SQLITE_OK;
}/*
 * 
合并相邻或重叠的空洞
 * Merge adjacent or overlapping holes to reduce fragmentation
 */
static void ccvfs_merge_adjacent_holes(CCVFSFile *pFile) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    CCVFSSpaceHole *pCurrent, *pNext;
    int mergeCount = 0;
    
    CCVFS_DEBUG("Starting hole merge operation: %u holes", pManager->hole_count);
    
    if (!pManager->enabled || pManager->hole_count < 2) {
        CCVFS_DEBUG("Merge not needed: enabled=%d, hole_count=%u", 
                   pManager->enabled, pManager->hole_count);
        return;
    }
    
    pCurrent = pManager->holes;
    
    while (pCurrent && pCurrent->next) {
        pNext = pCurrent->next;
        
        sqlite3_int64 currentEnd = pCurrent->offset + pCurrent->size;
        sqlite3_int64 nextEnd = pNext->offset + pNext->size;
        
        // Check if holes are adjacent or overlapping
        if (currentEnd >= pNext->offset) {
            CCVFS_DEBUG("Merging adjacent holes: [%llu,%u] + [%llu,%u]",
                       (unsigned long long)pCurrent->offset, pCurrent->size,
                       (unsigned long long)pNext->offset, pNext->size);
            
            // Merge the holes
            sqlite3_int64 mergedStart = (pCurrent->offset < pNext->offset) ? 
                                       pCurrent->offset : pNext->offset;
            sqlite3_int64 mergedEnd = (currentEnd > nextEnd) ? currentEnd : nextEnd;
            
            pCurrent->offset = mergedStart;
            pCurrent->size = (uint32_t)(mergedEnd - mergedStart);
            
            // Remove the next hole from the list
            pCurrent->next = pNext->next;
            sqlite3_free(pNext);
            pManager->hole_count--;
            mergeCount++;
            
            CCVFS_DEBUG("Merged result: [%llu,%u], remaining holes: %u",
                       (unsigned long long)pCurrent->offset, pCurrent->size, 
                       pManager->hole_count);
            
            // Continue with the same current hole in case there are more merges
            continue;
        }
        
        pCurrent = pNext;
    }
    
    if (mergeCount > 0) {
        pFile->hole_merge_count += mergeCount;
        CCVFS_INFO("Merged %d holes, total merges: %u, remaining holes: %u",
                  mergeCount, pFile->hole_merge_count, pManager->hole_count);
    } else {
        CCVFS_DEBUG("No adjacent holes found to merge");
    }
}

/*
 * 清理过小的空洞
 * Remove holes that are too small to be useful
 */
static void ccvfs_cleanup_small_holes(CCVFSFile *pFile) {
    CCVFSHoleManager *pManager = &pFile->hole_manager;
    CCVFSSpaceHole *pCurrent, *pPrev, *pNext;
    int cleanupCount = 0;
    
    CCVFS_DEBUG("Starting small hole cleanup: min_size=%u, hole_count=%u", 
               pManager->min_hole_size, pManager->hole_count);
    
    if (!pManager->enabled || pManager->hole_count == 0) {
        CCVFS_DEBUG("Cleanup not needed: enabled=%d, hole_count=%u", 
                   pManager->enabled, pManager->hole_count);
        return;
    }
    
    pCurrent = pManager->holes;
    pPrev = NULL;
    
    while (pCurrent) {
        pNext = pCurrent->next;
        
        if (pCurrent->size < pManager->min_hole_size) {
            CCVFS_DEBUG("Removing small hole: [%llu,%u] < min_size=%u",
                       (unsigned long long)pCurrent->offset, pCurrent->size,
                       pManager->min_hole_size);
            
            // Remove the hole from the list
            if (pPrev) {
                pPrev->next = pNext;
            } else {
                pManager->holes = pNext;
            }
            
            sqlite3_free(pCurrent);
            pManager->hole_count--;
            cleanupCount++;
            
            // Don't update pPrev since we removed the current node
            pCurrent = pNext;
        } else {
            pPrev = pCurrent;
            pCurrent = pNext;
        }
    }
    
    if (cleanupCount > 0) {
        pFile->hole_cleanup_count += cleanupCount;
        CCVFS_INFO("Cleaned up %d small holes, total cleanups: %u, remaining holes: %u",
                  cleanupCount, pFile->hole_cleanup_count, pManager->hole_count);
    } else {
        CCVFS_DEBUG("No small holes found to cleanup");
    }
}/*
 * 检查是
否需要执行空洞维护
 * Check if hole maintenance should be triggered based on operation count
 */
static void ccvfs_check_hole_maintenance_threshold(CCVFSFile *pFile) {
    const uint32_t MAINTENANCE_THRESHOLD = 50; // Trigger maintenance every 50 operations
    
    if (!pFile->is_ccvfs_file || !pFile->hole_manager.enabled) {
        return;
    }
    
    pFile->hole_operations_count++;
    
    if (pFile->hole_operations_count >= MAINTENANCE_THRESHOLD) {
        CCVFS_DEBUG("Triggering threshold-based hole maintenance (operations: %u)", 
                   pFile->hole_operations_count);
        
        // Perform maintenance
        ccvfs_merge_adjacent_holes(pFile);
        ccvfs_cleanup_small_holes(pFile);
        
        // Reset counter
        pFile->hole_operations_count = 0;
        
        CCVFS_DEBUG("Threshold-based maintenance completed");
    }
}