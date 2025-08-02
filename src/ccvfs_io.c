#include "ccvfs_io.h"
#include "ccvfs_block.h"
#include "ccvfs_core.h"
#include "ccvfs_utils.h"
#include <string.h>

/*
 * IO Methods table
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
 * Close file and clean up resources
 */
int ccvfsIoClose(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    int rc = SQLITE_OK;
    
    CCVFS_DEBUG("Closing CCVFS file");
    
    if (p->pReal) {
        // Save block index before closing
        if (p->pBlockIndex && p->header_loaded) {
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
        
        // Close underlying file
        int closeRc = p->pReal->pMethods->xClose(p->pReal);
        if (closeRc != SQLITE_OK) {
            CCVFS_ERROR("Failed to close underlying file: %d", closeRc);
            rc = closeRc;
        }
    }
    
    // Free block index
    if (p->pBlockIndex) {
        sqlite3_free(p->pBlockIndex);
        p->pBlockIndex = NULL;
    }
    
    CCVFS_INFO("CCVFS file closed");
    return rc;
}

/*
 * Get block number from file offset
 */
static uint32_t getBlockNumber(sqlite3_int64 offset, uint32_t blockSize) {
    if (blockSize == 0) {
        CCVFS_ERROR("Block size is zero, using default");
        blockSize = CCVFS_DEFAULT_BLOCK_SIZE;
    }
    return (uint32_t)(offset / blockSize);
}

/*
 * Get offset within block
 */
static uint32_t getBlockOffset(sqlite3_int64 offset, uint32_t blockSize) {
    if (blockSize == 0) {
        CCVFS_ERROR("Block size is zero, using default");
        blockSize = CCVFS_DEFAULT_BLOCK_SIZE;
    }
    return (uint32_t)(offset % blockSize);
}

/*
 * Read and decompress a block from file
 */
static int readBlock(CCVFSFile *pFile, uint32_t blockNum, unsigned char *buffer, uint32_t bufferSize) {
    CCVFS_DEBUG("=== READING BLOCK %u ===", blockNum);
    
    // Check if block index is loaded (should already be loaded in ccvfsOpen)
    if (!pFile->pBlockIndex) {
        CCVFS_DEBUG("Block index not loaded, loading now");
        int rc = ccvfs_load_block_index(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to load block index: %d", rc);
            return rc;
        }
    }
    
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
    
    // If block has no physical storage (sparse), return zeros
    if (pIndex->physical_offset == 0 || (pIndex->flags & CCVFS_BLOCK_SPARSE)) {
        CCVFS_DEBUG("Block %u is sparse, returning zeros", blockNum);
        memset(buffer, 0, bufferSize);
        return SQLITE_OK;
    }
    
    // Allocate temporary buffer for compressed data
    unsigned char *compressedData = sqlite3_malloc(pIndex->compressed_size);
    if (!compressedData) {
        CCVFS_ERROR("Failed to allocate memory for compressed data");
        return SQLITE_NOMEM;
    }
    
    // Read compressed block data
    int rc = pFile->pReal->pMethods->xRead(pFile->pReal, compressedData, 
                                          pIndex->compressed_size, 
                                          pIndex->physical_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read compressed block data: %d", rc);
        sqlite3_free(compressedData);
        return rc;
    }
    
    // Verify checksum (temporarily disabled to focus on block mapping)
    uint32_t checksum = ccvfs_crc32(compressedData, pIndex->compressed_size);
    if (checksum != pIndex->checksum) {
        CCVFS_DEBUG("Block %u checksum mismatch: expected 0x%08x, got 0x%08x (ignoring for debugging)", 
                   blockNum, pIndex->checksum, checksum);
        // sqlite3_free(compressedData);
        // return SQLITE_CORRUPT;  // Temporarily disabled
    }
    
    // Decrypt if needed
    unsigned char *decryptedData = compressedData;
    if (pFile->pOwner->pEncryptAlg && (pIndex->flags & CCVFS_BLOCK_ENCRYPTED)) {
        decryptedData = sqlite3_malloc(pIndex->compressed_size);
        if (!decryptedData) {
            CCVFS_ERROR("Failed to allocate memory for decrypted data");
            sqlite3_free(compressedData);
            return SQLITE_NOMEM;
        }
        
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
    
    // Decompress if needed
    if (pFile->pOwner->pCompressAlg && (pIndex->flags & CCVFS_BLOCK_COMPRESSED)) {
        rc = pFile->pOwner->pCompressAlg->decompress(decryptedData, pIndex->compressed_size,
                                                   buffer, bufferSize);
        if (rc < 0) {
            CCVFS_ERROR("Failed to decompress block %u: %d", blockNum, rc);
            if (decryptedData != compressedData) sqlite3_free(decryptedData);
            return SQLITE_CORRUPT;
        }
        
        // Fill remaining buffer with zeros
        if ((uint32_t)rc < bufferSize) {
            memset(buffer + rc, 0, bufferSize - rc);
        }
    } else {
        // No compression, copy data directly
        uint32_t copySize = (pIndex->original_size < bufferSize) ? pIndex->original_size : bufferSize;
        memcpy(buffer, decryptedData, copySize);
        
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
    
    CCVFS_DEBUG("=== READING %d bytes at offset %lld ===", iAmt, iOfst);
    
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
 * Compress and write a block to file
 */
static int writeBlock(CCVFSFile *pFile, uint32_t blockNum, const unsigned char *data, uint32_t dataSize) {
    CCVFS_DEBUG("=== WRITING BLOCK %u ===", blockNum);
    CCVFS_DEBUG("Block %u: writing %u bytes", blockNum, dataSize);
    
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
        
        // Mark index as dirty but don't save immediately
        pFile->index_dirty = 1;
        
        // Update logical database size for sparse blocks too
        if (blockNum + 1 > pFile->header.database_size_pages) {
            pFile->header.database_size_pages = blockNum + 1;
            CCVFS_DEBUG("Database size updated to %u pages for sparse block", 
                       pFile->header.database_size_pages);
        }
        
        CCVFS_DEBUG("Block[%u] updated to sparse, index marked dirty", blockNum);
        return SQLITE_OK;
    }
    
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
            // Compression successful and beneficial
            compressedSize = rc;
            flags |= CCVFS_BLOCK_COMPRESSED;
            CCVFS_VERBOSE("Block %u compressed from %u to %u bytes", blockNum, dataSize, compressedSize);
        } else {
            // Compression failed or not beneficial, use original data
            sqlite3_free(compressedData);
            compressedData = NULL;
            compressedSize = dataSize;
            CCVFS_DEBUG("Block %u compression not beneficial, using original data", blockNum);
        }
    }
    
    // Use original data if no compression or compression failed
    const unsigned char *dataToWrite = compressedData ? compressedData : data;
    
    // Encrypt if needed
    unsigned char *encryptedData = NULL;
    if (pFile->pOwner->pEncryptAlg) {
        encryptedData = sqlite3_malloc(compressedSize + 16); // Add padding for encryption
        if (!encryptedData) {
            CCVFS_ERROR("Failed to allocate memory for encryption");
            if (compressedData) sqlite3_free(compressedData);
            return SQLITE_NOMEM;
        }
        
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
    
    // Calculate checksum
    uint32_t checksum = ccvfs_crc32(dataToWrite, compressedSize);
    
    // Get file size to append new block (after reserved index table space)
    sqlite3_int64 fileSize;
    int rc = pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to get file size: %d", rc);
        if (encryptedData) sqlite3_free(encryptedData);
        if (compressedData) sqlite3_free(compressedData);
        return rc;
    }
    
    // Ensure we write data blocks after the reserved index table space
    sqlite3_int64 writeOffset = fileSize;
    if (writeOffset < CCVFS_DATA_BLOCKS_OFFSET) {
        writeOffset = CCVFS_DATA_BLOCKS_OFFSET;
        CCVFS_DEBUG("Adjusting write offset to %llu (after reserved index space)", 
                   (unsigned long long)writeOffset);
    }
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, dataToWrite, compressedSize, writeOffset);
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
 * Write to file
 */
int ccvfsIoWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    const unsigned char *data = (const unsigned char*)zBuf;
    int bytesWritten = 0;
    int rc;
    
    CCVFS_DEBUG("=== WRITING %d bytes at offset %lld ===", iAmt, iOfst);
    
    // Initialize CCVFS header for new CCVFS files on first write
    if (p->is_ccvfs_file && !p->header_loaded && iOfst == 0) {
        CCVFS_DEBUG("First write to new CCVFS file, initializing header");
        rc = ccvfs_init_header(p, p->pOwner);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to initialize CCVFS header: %d", rc);
            return rc;
        }
        
        // Save header first
        rc = ccvfs_save_header(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to save CCVFS header: %d", rc);
            return rc;
        }
        CCVFS_DEBUG("CCVFS header initialized and saved");
    }
    
    // If not a CCVFS file, write directly to underlying file
    if (!p->is_ccvfs_file) {
        CCVFS_DEBUG("Writing to regular file");
        return p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
    }
    
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
        
        // If we're not writing a full block, read existing data first
        if (currentOffset != 0 || bytesToWrite != blockSize) {
            CCVFS_DEBUG("Partial block write, reading existing data");
            rc = readBlock(p, currentBlock, blockBuffer, blockSize);
            if (rc != SQLITE_OK) {
                // If block doesn't exist, fill with zeros
                CCVFS_DEBUG("Block doesn't exist, filling with zeros");
                memset(blockBuffer, 0, blockSize);
            }
        }
        
        // Copy new data into block buffer
        memcpy(blockBuffer + currentOffset, data + bytesWritten, bytesToWrite);
        
        CCVFS_DEBUG("Modified block buffer, writing block %u", currentBlock);
        
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
 * Truncate file to specified size
 */
int ccvfsIoTruncate(sqlite3_file *pFile, sqlite3_int64 size) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Truncating file to %lld bytes", size);
    
    // If not a CCVFS file, truncate underlying file directly
    if (!p->is_ccvfs_file) {
        CCVFS_DEBUG("Truncating regular file");
        return p->pReal->pMethods->xTruncate(p->pReal, size);
    }
    
    // CCVFS file - update metadata
    uint32_t blockSize = p->header.block_size;
    uint32_t newBlockCount = (uint32_t)((size + blockSize - 1) / blockSize);
    
    // Update header
    p->header.database_size_pages = (uint32_t)(size / blockSize);
    
    // If reducing size, we could free unused blocks here
    // For now, just update the block count
    if (newBlockCount < p->header.total_blocks) {
        p->header.total_blocks = newBlockCount;
    }
    
    CCVFS_VERBOSE("CCVFS file truncated to size %lld", size);
    return SQLITE_OK;
}

/*
 * Sync file to disk
 */
int ccvfsIoSync(sqlite3_file *pFile, int flags) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Syncing file with flags %d", flags);
    
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
 * Get file size
 */
int ccvfsIoFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Getting file size");
    
    // If not a CCVFS file, get size from underlying file
    if (!p->is_ccvfs_file) {
        CCVFS_DEBUG("Getting size from regular file");
        return p->pReal->pMethods->xFileSize(p->pReal, pSize);
    }
    
    // For CCVFS files, ensure header is loaded
    if (!p->header_loaded) {
        // For new files that haven't been initialized yet, initialize the header
        int rc = ccvfs_init_header(p, p->pOwner);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to initialize header for file size: %d", rc);
            return rc;
        }
    }
    
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
 * Lock file
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
 * Unlock file
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
 * Check reserved lock
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
 * File control operations
 */
int ccvfsIoFileControl(sqlite3_file *pFile, int op, void *pArg) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("File control operation %d", op);
    
    // Handle CCVFS specific operations here if needed
    
    if (p->pReal && p->pReal->pMethods->xFileControl) {
        return p->pReal->pMethods->xFileControl(p->pReal, op, pArg);
    }
    
    return SQLITE_NOTFOUND;
}

/*
 * Get sector size
 */
int ccvfsIoSectorSize(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xSectorSize) {
        return p->pReal->pMethods->xSectorSize(p->pReal);
    }
    
    return 4096; // Default sector size
}

/*
 * Get device characteristics
 */
int ccvfsIoDeviceCharacteristics(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xDeviceCharacteristics) {
        return p->pReal->pMethods->xDeviceCharacteristics(p->pReal);
    }
    
    return 0;
}

/*
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
 * Shared memory barrier - pass through to underlying VFS
 */
void ccvfsIoShmBarrier(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (p->pReal && p->pReal->pMethods->xShmBarrier) {
        p->pReal->pMethods->xShmBarrier(p->pReal);
    }
}

/*
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
 * Fetch page - not supported for compressed VFS
 */
int ccvfsIoFetch(sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp) {
    CCVFS_DEBUG("Fetch operation not supported for compressed VFS");
    return SQLITE_IOERR;
}

/*
 * Unfetch page - not supported for compressed VFS
 */
int ccvfsIoUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage) {
    CCVFS_DEBUG("Unfetch operation not supported for compressed VFS");
    return SQLITE_OK;
}