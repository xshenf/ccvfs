#include "ccvfs_io.h"
#include "ccvfs_block.h"
#include "ccvfs_core.h"
#include "ccvfs_utils.h"
#include <string.h>

// Forward declarations
static void ccvfs_update_space_tracking(CCVFSFile *pFile);
static sqlite3_int64 ccvfs_find_best_fit_space(CCVFSFile *pFile, uint32_t requiredSize, uint32_t *pWastedSpace);

/*
 * 寻找最佳匹配的可用空间洞或间隙来满足所需大小
 * 暂时禁用：总是返回0以使用文件末尾分配策略，确保稳定性
 * Find the best fitting available space hole or gap for the required size
 * DISABLED: Always return 0 to use end-of-file allocation for stability
 */
static sqlite3_int64 ccvfs_find_best_fit_space(CCVFSFile *pFile, uint32_t requiredSize, uint32_t *pWastedSpace) {
    // 暂时完全禁用空洞检测算法
    // Disable hole detection completely for now
    *pWastedSpace = 0;
    return 0;
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
 * 保存块索引和文件头，释放内存，关闭底层文件
 * Close file and clean up resources
 * Save block index and header, free memory, close underlying file
 */
int ccvfsIoClose(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    int rc = SQLITE_OK;
    
    CCVFS_DEBUG("Closing CCVFS file");
    
    if (p->pReal) {
        // 关闭前保存块索引和文件头（仅对可写文件）
        // Save block index and header before closing (only for writable files)
        if (p->pBlockIndex && p->header_loaded && !(p->open_flags & SQLITE_OPEN_READONLY)) {
            int saveRc = ccvfs_save_block_index(p);
            if (saveRc != SQLITE_OK) {
                CCVFS_ERROR("Failed to save block index: %d", saveRc);
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
    
    // 释放块索引内存
    // Free block index
    if (p->pBlockIndex) {
        sqlite3_free(p->pBlockIndex);
        p->pBlockIndex = NULL;
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
 * 根据文件偏移量获取块编号
 * 计算公式：块编号 = 偏移量 / 块大小
 * Get block number from file offset
 * Formula: block_number = offset / block_size
 */
static uint32_t getBlockNumber(sqlite3_int64 offset, uint32_t blockSize) {
    if (blockSize == 0) {
        CCVFS_ERROR("Block size is zero, using default");
        blockSize = CCVFS_DEFAULT_BLOCK_SIZE;
    }
    return (uint32_t)(offset / blockSize);
}

/*
 * 获取块内偏移量
 * 计算公式：块内偏移 = 偏移量 % 块大小
 * Get offset within block
 * Formula: block_offset = offset % block_size
 */
static uint32_t getBlockOffset(sqlite3_int64 offset, uint32_t blockSize) {
    if (blockSize == 0) {
        CCVFS_ERROR("Block size is zero, using default");
        blockSize = CCVFS_DEFAULT_BLOCK_SIZE;
    }
    return (uint32_t)(offset % blockSize);
}

/*
 * 从文件读取并解压一个数据块
 * 处理流程：读取压缩数据 -> 校验和验证 -> 解密 -> 解压缩 -> 复制到缓冲区
 * Read and decompress a block from file
 * Process flow: read compressed data -> checksum verification -> decrypt -> decompress -> copy to buffer
 */
static int readBlock(CCVFSFile *pFile, uint32_t blockNum, unsigned char *buffer, uint32_t bufferSize) {
    CCVFS_DEBUG("=== READING BLOCK %u ===", blockNum);
    
    // 检查块索引是否已加载（应该在ccvfsOpen中已加载）
    // Check if block index is loaded (should already be loaded in ccvfsOpen)
    if (!pFile->pBlockIndex) {
        CCVFS_DEBUG("Block index not loaded, loading now");
        int rc = ccvfs_load_block_index(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to load block index: %d", rc);
            return rc;
        }
    }
    
    // 检查块编号有效性
    // Check block number validity
    if (blockNum >= pFile->header.total_blocks) {
        CCVFS_DEBUG("Block %u beyond total blocks %u, treating as zero (sparse)", 
                   blockNum, pFile->header.total_blocks);
        memset(buffer, 0, bufferSize);
        return SQLITE_OK;
    }
    
    CCVFSBlockIndex *pIndex = &pFile->pBlockIndex[blockNum];
    
    CCVFS_DEBUG("Block[%u] mapping: physical_offset=%llu, compressed_size=%u, original_size=%u, flags=0x%x",
               blockNum, (unsigned long long)pIndex->physical_offset, 
               pIndex->compressed_size, pIndex->original_size, pIndex->flags);
    
    // 如果块没有物理存储（稀疏块），返回零填充数据
    // If block has no physical storage (sparse), return zeros
    if (pIndex->physical_offset == 0 || (pIndex->flags & CCVFS_BLOCK_SPARSE)) {
        CCVFS_DEBUG("Block %u is sparse, returning zeros", blockNum);
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
    
    // 读取压缩块数据
    // Read compressed block data
    int rc = pFile->pReal->pMethods->xRead(pFile->pReal, compressedData, 
                                          pIndex->compressed_size, 
                                          pIndex->physical_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read compressed block data: %d", rc);
        sqlite3_free(compressedData);
        return rc;
    }
    
    // 验证校验和
    // Verify checksum
    uint32_t checksum = ccvfs_crc32(compressedData, pIndex->compressed_size);
    if (checksum != pIndex->checksum) {
        CCVFS_ERROR("Block %u checksum mismatch: expected 0x%08x, got 0x%08x", 
                   blockNum, pIndex->checksum, checksum);
        CCVFS_ERROR("Block %u details: phys_offset=%llu, comp_size=%u, orig_size=%u, flags=0x%x", 
                   blockNum, pIndex->physical_offset, pIndex->compressed_size, 
                   pIndex->original_size, pIndex->flags);
        
        // Show first few bytes of corrupted data for debugging
        CCVFS_ERROR("First 16 bytes of block data: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                   compressedData[0], compressedData[1], compressedData[2], compressedData[3],
                   compressedData[4], compressedData[5], compressedData[6], compressedData[7],
                   compressedData[8], compressedData[9], compressedData[10], compressedData[11],
                   compressedData[12], compressedData[13], compressedData[14], compressedData[15]);
        
        sqlite3_free(compressedData);
        return SQLITE_CORRUPT;
    }
    
    // Decrypt if needed
    unsigned char *decryptedData = compressedData;
    if (pFile->pOwner->pEncryptAlg && (pIndex->flags & CCVFS_BLOCK_ENCRYPTED)) {
        // 为解密数据专门分配缓冲区
        decryptedData = sqlite3_malloc(pIndex->compressed_size);
        if (!decryptedData) {
            CCVFS_ERROR("Failed to allocate memory for decrypted data");
            sqlite3_free(compressedData);
            return SQLITE_NOMEM;
        }
        
        // 暂时使用简单密钥（实际实现中应该来自用户）
        // Use a simple key for now (in real implementation, this should come from user)
        unsigned char key[16] = "default_key_123";
        rc = pFile->pOwner->pEncryptAlg->decrypt(key, 16, compressedData, 
                                               pIndex->compressed_size,
                                               decryptedData, pIndex->compressed_size);
        if (rc < 0) {
            CCVFS_ERROR("Failed to decrypt block %u: %d", blockNum, rc);
            sqlite3_free(compressedData);
            sqlite3_free(decryptedData);
            return SQLITE_CORRUPT;
        }
        sqlite3_free(compressedData);
    }
    
    // 如果需要则解压缩数据
    // Decompress if needed
    if (pFile->pOwner->pCompressAlg && (pIndex->flags & CCVFS_BLOCK_COMPRESSED)) {
        // 解压前验证压缩大小
        // Validate compressed size before decompression
        if (pIndex->compressed_size == 0 || pIndex->original_size == 0) {
            CCVFS_ERROR("Invalid block %u sizes: compressed=%u, original=%u", 
                       blockNum, pIndex->compressed_size, pIndex->original_size);
            if (decryptedData != compressedData) sqlite3_free(decryptedData);
            return SQLITE_CORRUPT;
        }
        
        rc = pFile->pOwner->pCompressAlg->decompress(decryptedData, pIndex->compressed_size,
                                                   buffer, bufferSize);
        if (rc < 0) {
            CCVFS_ERROR("Failed to decompress block %u: %d (compressed_size=%u, original_size=%u)", 
                       blockNum, rc, pIndex->compressed_size, pIndex->original_size);
            if (decryptedData != compressedData) sqlite3_free(decryptedData);
            return SQLITE_CORRUPT;
        }
        
        // 验证解压后的大小
        // Validate decompressed size
        if ((uint32_t)rc != pIndex->original_size) {
            CCVFS_ERROR("Block %u decompressed size mismatch: expected %u, got %d", 
                       blockNum, pIndex->original_size, rc);
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
    
    CCVFS_VERBOSE("Successfully read and decompressed block %u", blockNum);
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
    }
    
    // CCVFS file - use block-based reading
    uint32_t blockSize = p->header.block_size;
    if (blockSize == 0) {
        CCVFS_ERROR("Invalid block size in header, using default");
        blockSize = CCVFS_DEFAULT_BLOCK_SIZE;
        p->header.block_size = blockSize;
    }
    
    uint32_t startBlock = getBlockNumber(iOfst, blockSize);
    uint32_t startOffset = getBlockOffset(iOfst, blockSize);
    
    CCVFS_DEBUG("Block-based read: blockSize=%u, startBlock=%u, startOffset=%u", 
               blockSize, startBlock, startOffset);
    
    // Allocate block buffer
    unsigned char *blockBuffer = sqlite3_malloc(blockSize);
    if (!blockBuffer) {
        CCVFS_ERROR("Failed to allocate block buffer");
        return SQLITE_NOMEM;
    }
    
    while (bytesRead < iAmt) {
        uint32_t currentBlock = startBlock + (bytesRead + startOffset) / blockSize;
        uint32_t currentOffset = (startOffset + bytesRead) % blockSize;
        uint32_t bytesToRead = blockSize - currentOffset;
        
        if (bytesToRead > (uint32_t)(iAmt - bytesRead)) {
            bytesToRead = iAmt - bytesRead;
        }
        
        CCVFS_DEBUG("Reading iteration: currentBlock=%u, currentOffset=%u, bytesToRead=%u", 
                   currentBlock, currentOffset, bytesToRead);
        
        // Read block
        rc = readBlock(p, currentBlock, blockBuffer, blockSize);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to read block %u: %d", currentBlock, rc);
            sqlite3_free(blockBuffer);
            return rc;
        }
        
        // Copy data from block buffer
        memcpy(buffer + bytesRead, blockBuffer + currentOffset, bytesToRead);
        bytesRead += bytesToRead;
        
        CCVFS_DEBUG("Copied %u bytes, total read: %d/%d", bytesToRead, bytesRead, iAmt);
    }
    
    sqlite3_free(blockBuffer);
    
    CCVFS_VERBOSE("Successfully read %d bytes from offset %lld", iAmt, iOfst);
    return SQLITE_OK;
}

/*
 * 压缩并将一个块写入文件
 * 处理流程：检查稀疏块 -> 压缩 -> 加密 -> 空间分配 -> 写入磁盘 -> 更新索引
 * Compress and write a block to file
 * Process flow: check sparse block -> compress -> encrypt -> space allocation -> write to disk -> update index
 */
static int writeBlock(CCVFSFile *pFile, uint32_t blockNum, const unsigned char *data, uint32_t dataSize) {
    CCVFS_DEBUG("=== WRITING BLOCK %u ===", blockNum);
    CCVFS_DEBUG("Block %u: writing %u bytes", blockNum, dataSize);
    
    // 确保块索引足够大
    // Ensure block index is large enough
    if (blockNum >= pFile->header.total_blocks) {
        CCVFS_DEBUG("Need to expand block index from %u to %u", 
                   pFile->header.total_blocks, blockNum + 1);
        int rc = ccvfs_expand_block_index(pFile, blockNum + 1);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to expand block index: %d", rc);
            return rc;
        }
    }
    
    CCVFSBlockIndex *pIndex = &pFile->pBlockIndex[blockNum];
    
    CCVFS_DEBUG("Block[%u] current mapping: physical_offset=%llu, compressed_size=%u, flags=0x%x",
               blockNum, (unsigned long long)pIndex->physical_offset, 
               pIndex->compressed_size, pIndex->flags);
    
    // 检查块是否全为零（稀疏块优化）
    // Check if block is all zeros (sparse block optimization)
    int isZeroBlock = 1;
    for (uint32_t i = 0; i < dataSize; i++) {
        if (data[i] != 0) {
            isZeroBlock = 0;
            break;
        }
    }
    
    if (isZeroBlock) {
        CCVFS_DEBUG("Block %u is all zeros, treating as sparse", blockNum);
        pIndex->physical_offset = 0;
        pIndex->compressed_size = 0;
        pIndex->original_size = dataSize;
        pIndex->checksum = 0;
        pIndex->flags = CCVFS_BLOCK_SPARSE;
        
        // 将索引标记为脏，但不立即保存
        // Mark index as dirty but don't save immediately
        pFile->index_dirty = 1;
        
        // 即使是稀疏块也要更新逻辑数据库大小
        // Update logical database size for sparse blocks too
        if (blockNum + 1 > pFile->header.database_size_pages) {
            pFile->header.database_size_pages = blockNum + 1;
            CCVFS_DEBUG("Database size updated to %u pages for sparse block", 
                       pFile->header.database_size_pages);
        }
        
        CCVFS_DEBUG("Block[%u] updated to sparse, index marked dirty", blockNum);
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
            flags |= CCVFS_BLOCK_COMPRESSED;
            CCVFS_VERBOSE("Block %u compressed from %u to %u bytes", blockNum, dataSize, compressedSize);
        } else {
            // 压缩失败或无效，使用原始数据
            // Compression failed or not beneficial, use original data
            sqlite3_free(compressedData);
            compressedData = NULL;
            compressedSize = dataSize;
            CCVFS_DEBUG("Block %u compression not beneficial, using original data", blockNum);
        }
    }
    
    // 如果没有压缩或压缩失败，使用原始数据
    // Use original data if no compression or compression failed
    const unsigned char *dataToWrite = compressedData ? compressedData : data;
    
    // 如果需要则加密数据
    // Encrypt if needed
    unsigned char *encryptedData = NULL;
    if (pFile->pOwner->pEncryptAlg) {
        encryptedData = sqlite3_malloc(compressedSize + 16); // 为加密添加填充
        if (!encryptedData) {
            CCVFS_ERROR("Failed to allocate memory for encryption");
            if (compressedData) sqlite3_free(compressedData);
            return SQLITE_NOMEM;
        }
        
        // 暂时使用简单密钥（实际实现中应该来自用户）
        // Use a simple key for now (in real implementation, this should come from user)
        unsigned char key[16] = "default_key_123";
        int rc = pFile->pOwner->pEncryptAlg->encrypt(key, 16, dataToWrite, compressedSize,
                                                   encryptedData, compressedSize + 16);
        if (rc > 0) {
            compressedSize = rc;
            flags |= CCVFS_BLOCK_ENCRYPTED;
            dataToWrite = encryptedData;
            CCVFS_VERBOSE("Block %u encrypted, size %u", blockNum, compressedSize);
        } else {
            CCVFS_ERROR("Failed to encrypt block %u: %d", blockNum, rc);
            sqlite3_free(encryptedData);
            if (compressedData) sqlite3_free(compressedData);
            return SQLITE_IOERR;
        }
    }
    
    // 计算数据校验和
    uint32_t checksum = ccvfs_crc32(dataToWrite, compressedSize);
    
    // 确定写入偏移：重用现有块位置或分配新空间
    sqlite3_int64 writeOffset;
    
    // 检查块是否已存在且可以安全重用
    if (pIndex->physical_offset != 0) {
        uint32_t existingSpace = pIndex->compressed_size;
        
        // 【增强空间重用策略】：多种重用场景
        if (compressedSize <= existingSpace) {
            // 【场景1】：完美匹配或更小 - 直接重用
            writeOffset = pIndex->physical_offset;
            
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
            // 如果块后面有可用空间，允许无限制扩展
            sqlite3_int64 fileSize;
            int sizeRc = pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
            if (sizeRc == SQLITE_OK) {
                uint64_t blockEndOffset = pIndex->physical_offset + existingSpace;
                uint32_t expansionNeeded = compressedSize - existingSpace;
                
                // 限制极端扩展以防止病态行为
                double growthRatio = (double)compressedSize / (double)existingSpace;
                if (growthRatio > 10.0) {
                    CCVFS_DEBUG("检测到极端增长 (%.1fx)，为稳定性分配新空间", growthRatio);
                    pFile->new_allocation_count++;
                    goto allocate_new_space;
                }
                
                // 检查是否可以安全扩展（没有数据紧跟在这个块后面）
                int canExpand = 1;
                for (uint32_t i = 0; i < pFile->header.total_blocks; i++) {
                    if (i != blockNum && pFile->pBlockIndex[i].physical_offset != 0) {
                        uint64_t otherBlockStart = pFile->pBlockIndex[i].physical_offset;
                        // 如果其他块在扩展范围内，则不能扩展（包含32字节安全边距）
                        if (otherBlockStart >= blockEndOffset && 
                            otherBlockStart < blockEndOffset + expansionNeeded + 32) { // 32字节安全边距
                            canExpand = 0;
                            break;
                        }
                    }
                }
                
                if (canExpand && blockEndOffset + expansionNeeded <= fileSize) {
                    // 可以安全扩展现有空间
                    writeOffset = pIndex->physical_offset;
                    pFile->space_expansion_count++;
                    CCVFS_DEBUG("扩展现有块在偏移 %llu: %u->%u 字节 (+%u 扩展, %.1fx 增长)", 
                               (unsigned long long)writeOffset, existingSpace, compressedSize, expansionNeeded, growthRatio);
                } else {
                    // 不能安全扩展 - 分配新空间
                    CCVFS_DEBUG("不能安全扩展（相邻块或EOF），分配新空间");
                    pFile->new_allocation_count++;
                    goto allocate_new_space;
                }
            } else {
                CCVFS_DEBUG("获取文件大小失败，用于扩展检查，分配新空间");
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
            // 找到合适的空洞 - 使用它
            pFile->hole_reclaim_count++;
            pFile->best_fit_count++;
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
            
            // 检查顺序写入模式（多个连续块分配）
            if (pFile->last_written_block != UINT32_MAX && blockNum == pFile->last_written_block + 1) {
                pFile->sequential_write_count++;
                CCVFS_DEBUG("检测到顺序写入: 块 %u->%u", pFile->last_written_block, blockNum);
            }
            pFile->last_written_block = blockNum;
            
            // 确保我们在保留的索引表空间之后写入数据块
            writeOffset = fileSize;
            if (writeOffset < CCVFS_DATA_BLOCKS_OFFSET) {
                writeOffset = CCVFS_DATA_BLOCKS_OFFSET;
                CCVFS_DEBUG("调整写入偏移到 %llu (保留索引空间之后)", 
                           (unsigned long long)writeOffset);
            } else {
                CCVFS_DEBUG("在文件末尾分配新块: 偏移 %llu (顺序: %u)", 
                           (unsigned long long)writeOffset, pFile->sequential_write_count);
            }
        }
    }
    
    int rc = pFile->pReal->pMethods->xWrite(pFile->pReal, dataToWrite, compressedSize, writeOffset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write block data: %d", rc);
        if (encryptedData) sqlite3_free(encryptedData);
        if (compressedData) sqlite3_free(compressedData);
        return rc;
    }
    
    // Update block index entry
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
    uint32_t maxBlock = 0;
    for (uint32_t i = 0; i < pFile->header.total_blocks; i++) {
        if (pFile->pBlockIndex[i].physical_offset != 0) { // Non-sparse block
            maxBlock = i + 1;
        }
    }
    pFile->header.database_size_pages = maxBlock;
    
    CCVFS_DEBUG("Block[%u] updated: physical_offset=%llu, compressed_size=%u, flags=0x%x",
               blockNum, (unsigned long long)writeOffset, compressedSize, flags);
    CCVFS_DEBUG("Database size updated to %u pages (%llu bytes)", 
               pFile->header.database_size_pages, 
               (unsigned long long)pFile->header.database_size_pages * pFile->header.block_size);
    CCVFS_DEBUG("Index marked dirty, will be saved on next sync/close");
    
    // Clean up
    if (encryptedData) sqlite3_free(encryptedData);
    if (compressedData) sqlite3_free(compressedData);
    
    CCVFS_VERBOSE("Successfully wrote block %u at offset %lld", blockNum, writeOffset);
    return SQLITE_OK;
}

/*
 * 更新空间利用跟踪指标
 */
static void ccvfs_update_space_tracking(CCVFSFile *pFile) {
    uint64_t totalAllocated = 0;
    uint64_t totalUsed = 0;
    uint32_t blockCount = 0;
    uint32_t wastedSpaceBlocks = 0;
    uint64_t totalWastedSpace = 0;
    
    // 从所有块计算空间利用率
    for (uint32_t i = 0; i < pFile->header.total_blocks; i++) {
        CCVFSBlockIndex *pIndex = &pFile->pBlockIndex[i];
        if (pIndex->physical_offset != 0) { // 非稀疏块
            blockCount++;
            totalAllocated += pIndex->compressed_size;
            totalUsed += pIndex->compressed_size; // 所有分配的空间目前都被使用
            
            // 检查显著的浪费空间（>10%浪费）
            if (pIndex->original_size > 0) {
                uint32_t wastedSpace = (pIndex->compressed_size > pIndex->original_size) ? 
                                     0 : (pIndex->original_size - pIndex->compressed_size);
                if (wastedSpace > pIndex->compressed_size * 0.1) {
                    wastedSpaceBlocks++;
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
            if (blockCount > 1) {
                uint32_t sequentialRatio = (pFile->sequential_write_count * 100) / (blockCount - 1);
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
}

/*
 * 将数据写入文件
 * 对于CCVFS文件：初始化文件头、使用基于块的写入、处理跨块写入
 * 对于普通文件：直接传递给底层VFS
 * Write data to file
 * For CCVFS files: initialize header, use block-based writing, handle cross-block writes
 * For regular files: pass directly to underlying VFS
 */
int ccvfsIoWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    const unsigned char *data = (const unsigned char*)zBuf;
    int bytesWritten = 0;
    int rc;
    
    CCVFS_DEBUG("=== WRITING %d bytes at offset %lld to file: %s ===", iAmt, iOfst, p->filename ? p->filename : "unknown");
    
    // 对新CCVFS文件的首次写入初始化CCVFS文件头
    // Initialize CCVFS header for new CCVFS files on first write
    if (p->is_ccvfs_file && !p->header_loaded && iOfst == 0) {
        CCVFS_DEBUG("First write to new CCVFS file, initializing header");
        rc = ccvfs_init_header(p, p->pOwner);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to initialize CCVFS header: %d", rc);
            return rc;
        }
        
        // 先保存文件头
        // Save header first
        rc = ccvfs_save_header(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to save CCVFS header: %d", rc);
            return rc;
        }
        CCVFS_DEBUG("CCVFS header initialized and saved");
    }
    
    // 如果不是CCVFS文件，直接写入底层文件
    // If not a CCVFS file, write directly to underlying file
    if (!p->is_ccvfs_file) {
        CCVFS_DEBUG("Writing to regular file");
        return p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
    }
    
    // CCVFS文件 - 使用基于块的写入
    // CCVFS file - use block-based writing
    uint32_t blockSize = p->header.block_size;
    if (blockSize == 0) {
        CCVFS_ERROR("Invalid block size in header, using default");
        blockSize = CCVFS_DEFAULT_BLOCK_SIZE;
        p->header.block_size = blockSize;
    }
    
    uint32_t startBlock = getBlockNumber(iOfst, blockSize);
    uint32_t startOffset = getBlockOffset(iOfst, blockSize);
    
    CCVFS_DEBUG("Block-based write: blockSize=%u, startBlock=%u, startOffset=%u", 
               blockSize, startBlock, startOffset);
    CCVFS_DEBUG("Current mapping state: total_blocks=%u, index_dirty=%d", 
               p->header.total_blocks, p->index_dirty);
    
    // Allocate block buffer
    unsigned char *blockBuffer = sqlite3_malloc(blockSize);
    if (!blockBuffer) {
        CCVFS_ERROR("Failed to allocate block buffer");
        return SQLITE_NOMEM;
    }
    
    while (bytesWritten < iAmt) {
        uint32_t currentBlock = startBlock + (bytesWritten + startOffset) / blockSize;
        uint32_t currentOffset = (startOffset + bytesWritten) % blockSize;
        uint32_t bytesToWrite = blockSize - currentOffset;
        
        if (bytesToWrite > (uint32_t)(iAmt - bytesWritten)) {
            bytesToWrite = iAmt - bytesWritten;
        }
        
        CCVFS_DEBUG("Writing iteration: currentBlock=%u, currentOffset=%u, bytesToWrite=%u", 
                   currentBlock, currentOffset, bytesToWrite);
        
        // 如果我们不是写入一个完整的块，先读取现有数据
        // If we're not writing a full block, read existing data first
        if (currentOffset != 0 || bytesToWrite != blockSize) {
            CCVFS_DEBUG("Partial block write, reading existing data");
            rc = readBlock(p, currentBlock, blockBuffer, blockSize);
            if (rc != SQLITE_OK) {
                // 如果块不存在，用零填充
                // If block doesn't exist, fill with zeros
                CCVFS_DEBUG("Block doesn't exist, filling with zeros");
                memset(blockBuffer, 0, blockSize);
            }
        }
        
        // 将新数据复制到块缓冲区
        // Copy new data into block buffer
        memcpy(blockBuffer + currentOffset, data + bytesWritten, bytesToWrite);
        
        CCVFS_DEBUG("Modified block buffer, writing block %u", currentBlock);
        
        // 写入修改后的块
        // Write modified block
        rc = writeBlock(p, currentBlock, blockBuffer, blockSize);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to write block %u: %d", currentBlock, rc);
            sqlite3_free(blockBuffer);
            return rc;
        }
        
        bytesWritten += bytesToWrite;
        
        CCVFS_DEBUG("Wrote %u bytes, total written: %d/%d", bytesToWrite, bytesWritten, iAmt);
    }
    
    sqlite3_free(blockBuffer);
    
    CCVFS_DEBUG("Write complete: index_dirty=%d, total_blocks=%u", 
               p->index_dirty, p->header.total_blocks);
    CCVFS_VERBOSE("Successfully wrote %d bytes to offset %lld", iAmt, iOfst);
    return SQLITE_OK;
}

/*
 * 将文件截断到指定大小
 * 对于CCVFS文件：更新元数据和块计数
 * 对于普通文件：直接截断底层文件
 * Truncate file to specified size
 * For CCVFS files: update metadata and block count
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
    uint32_t blockSize = p->header.block_size;
    uint32_t newBlockCount = (uint32_t)((size + blockSize - 1) / blockSize);
    
    // 更新文件头
    // Update header
    p->header.database_size_pages = (uint32_t)(size / blockSize);
    
    // 如果减小大小，我们可以在这里释放未使用的块
    // 现在只更新块计数
    // If reducing size, we could free unused blocks here
    // For now, just update the block count
    if (newBlockCount < p->header.total_blocks) {
        p->header.total_blocks = newBlockCount;
    }
    
    CCVFS_VERBOSE("CCVFS file truncated to size %lld", size);
    return SQLITE_OK;
}

/*
 * 将文件同步到磁盘
 * 先保存CCVFS的块索引和文件头，然后同步底层文件
 * Sync file to disk
 * Save CCVFS block index and header first, then sync underlying file
 */
int ccvfsIoSync(sqlite3_file *pFile, int flags) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Syncing file with flags %d", flags);
    
    // 如果是CCVFS文件，先保存块索引和文件头
    // Save block index and header first if this is a CCVFS file
    if (p->pBlockIndex && p->header_loaded) {
        int rc = ccvfs_save_block_index(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to save block index: %d", rc);
            return rc;
        }
        
        rc = ccvfs_save_header(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to save header: %d", rc);
            return rc;
        }
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
    
    CCVFS_VERBOSE("File synced successfully");
    return SQLITE_OK;
}

/*
 * 获取文件大小
 * 对于CCVFS文件：返回基于块结构的逻辑文件大小
 * 对于普通文件：返回底层文件大小
 * Get file size
 * For CCVFS files: return logical file size based on block structure
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
    
    // 基于块结构返回逻辑文件大小
    // Return logical file size based on block structure
    uint32_t blockSize = p->header.block_size;
    if (blockSize == 0) {
        CCVFS_ERROR("Invalid block size in header, using default");
        blockSize = CCVFS_DEFAULT_BLOCK_SIZE;
        p->header.block_size = blockSize;
    }
    
    *pSize = (sqlite3_int64)p->header.database_size_pages * blockSize;
    
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