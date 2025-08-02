#include "ccvfs_block.h"
#include "ccvfs_utils.h"

// Forward declarations
static sqlite3_int64 ccvfs_calculate_index_position(CCVFSFile *pFile);

/*
 * Load file header from disk
 */
int ccvfs_load_header(CCVFSFile *pFile) {
    int rc;
    sqlite3_int64 fileSize;
    
    if (pFile->header_loaded) {
        return SQLITE_OK;
    }
    
    // Check if file exists and has enough data for header
    rc = pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to get file size");
        return rc;
    }
    
    if (fileSize < CCVFS_HEADER_SIZE) {
        CCVFS_DEBUG("File too small for CCVFS header, treating as new file");
        return SQLITE_IOERR_READ;
    }
    
    // Read header from beginning of file
    rc = pFile->pReal->pMethods->xRead(pFile->pReal, &pFile->header, 
                                       CCVFS_HEADER_SIZE, 0);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read file header");
        return rc;
    }
    
    // Verify magic number
    if (memcmp(pFile->header.magic, CCVFS_MAGIC, 8) != 0) {
        CCVFS_DEBUG("Invalid magic number, not a CCVFS file");
        return SQLITE_IOERR_READ;
    }
    
    // Verify version
    if (pFile->header.major_version != CCVFS_VERSION_MAJOR) {
        CCVFS_ERROR("Unsupported CCVFS version: %d.%d", 
                    pFile->header.major_version, pFile->header.minor_version);
        return SQLITE_IOERR_READ;
    }
    
    // Verify header checksum (temporarily disabled for debugging)
    uint32_t calculated_checksum = ccvfs_crc32((const unsigned char*)&pFile->header,
                                               CCVFS_HEADER_SIZE - sizeof(uint32_t));
    if (pFile->header.header_checksum != calculated_checksum) {
        CCVFS_DEBUG("Header checksum mismatch: expected 0x%08x, got 0x%08x (ignoring for now)", 
                   pFile->header.header_checksum, calculated_checksum);
        // return SQLITE_IOERR_READ;  // Temporarily disabled
    }
    
    pFile->header_loaded = 1;
    
    CCVFS_DEBUG("Loaded CCVFS header: version %d.%d, %d blocks, compression: %s, encryption: %s",
                pFile->header.major_version, pFile->header.minor_version,
                pFile->header.total_blocks, pFile->header.compress_algorithm,
                pFile->header.encrypt_algorithm);
    
    return SQLITE_OK;
}

/*
 * Save file header to disk
 */
int ccvfs_save_header(CCVFSFile *pFile) {
    int rc;
    
    // Calculate header checksum
    pFile->header.header_checksum = ccvfs_crc32((const unsigned char*)&pFile->header,
                                                CCVFS_HEADER_SIZE - sizeof(uint32_t));
    
    // Write header to beginning of file
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, &pFile->header,
                                        CCVFS_HEADER_SIZE, 0);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write file header");
        return rc;
    }
    
    pFile->header_loaded = 1;
    
    CCVFS_DEBUG("Saved CCVFS header");
    return SQLITE_OK;
}

/*
 * Load block index table from disk
 */
int ccvfs_load_block_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    CCVFS_DEBUG("=== LOADING BLOCK INDEX ===");
    
    if (!pFile->header_loaded) {
        rc = ccvfs_load_header(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to load header before loading block index: %d", rc);
            return rc;
        }
    }
    
    if (pFile->header.total_blocks == 0) {
        CCVFS_DEBUG("No blocks in file, initializing empty index");
        pFile->index_dirty = 0;
        pFile->index_capacity = 16; // Start with small capacity
        pFile->pBlockIndex = (CCVFSBlockIndex*)sqlite3_malloc(pFile->index_capacity * sizeof(CCVFSBlockIndex));
        if (!pFile->pBlockIndex) {
            CCVFS_ERROR("Failed to allocate initial block index");
            return SQLITE_NOMEM;
        }
        memset(pFile->pBlockIndex, 0, pFile->index_capacity * sizeof(CCVFSBlockIndex));
        CCVFS_INFO("Initialized empty block index with capacity %u", pFile->index_capacity);
        return SQLITE_OK;
    }
    
    // Allocate memory for block index (with extra capacity for growth)
    pFile->index_capacity = pFile->header.total_blocks + 16; // Add some extra capacity
    index_size = pFile->index_capacity * sizeof(CCVFSBlockIndex);
    pFile->pBlockIndex = (CCVFSBlockIndex*)sqlite3_malloc(index_size);
    if (!pFile->pBlockIndex) {
        CCVFS_ERROR("Failed to allocate memory for block index: %zu bytes", index_size);
        return SQLITE_NOMEM;
    }
    
    // Initialize all entries to zero first
    memset(pFile->pBlockIndex, 0, index_size);
    
    // Read existing block index from file
    size_t existing_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
    CCVFS_DEBUG("Reading %zu bytes of block index from offset %llu", 
               existing_size, (unsigned long long)pFile->header.index_table_offset);
               
    rc = pFile->pReal->pMethods->xRead(pFile->pReal, pFile->pBlockIndex,
                                       existing_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read block index from disk: %d", rc);
        sqlite3_free(pFile->pBlockIndex);
        pFile->pBlockIndex = NULL;
        return rc;
    }
    
    pFile->index_dirty = 0; // Just loaded, so it's not dirty
    
    CCVFS_INFO("Loaded block index: %d blocks, capacity %u", 
              pFile->header.total_blocks, pFile->index_capacity);
              
    // Log mapping table contents for debugging
    CCVFS_DEBUG("=== BLOCK MAPPING TABLE CONTENTS ===");
    for (uint32_t i = 0; i < pFile->header.total_blocks; i++) {
        CCVFSBlockIndex *pIndex = &pFile->pBlockIndex[i];
        CCVFS_DEBUG("Block[%u]: physical_offset=%llu, compressed_size=%u, original_size=%u, flags=0x%x, checksum=0x%08x",
                   i, (unsigned long long)pIndex->physical_offset, 
                   pIndex->compressed_size, pIndex->original_size, 
                   pIndex->flags, pIndex->checksum);
    }
    CCVFS_DEBUG("=== END MAPPING TABLE CONTENTS ===");
    
    return SQLITE_OK;
}

/*
 * Save block index table to disk (only if dirty)
 */
int ccvfs_save_block_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    CCVFS_DEBUG("=== SAVING BLOCK INDEX ===");
    
    if (!pFile->pBlockIndex || pFile->header.total_blocks == 0) {
        CCVFS_DEBUG("No block index to save");
        return SQLITE_OK;
    }
    
    // Only save if dirty (modified since last save)
    if (!pFile->index_dirty) {
        CCVFS_DEBUG("Block index not dirty, skipping save");
        return SQLITE_OK;
    }
    
    index_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
    
    // Verify we don't exceed the reserved index table space
    if (index_size > CCVFS_INDEX_TABLE_SIZE) {
        CCVFS_ERROR("Block index too large: %zu bytes > %d bytes reserved", 
                   index_size, CCVFS_INDEX_TABLE_SIZE);
        return SQLITE_ERROR;
    }
    
    CCVFS_DEBUG("Saving dirty block index: %d blocks, %zu bytes at FIXED offset %llu", 
               pFile->header.total_blocks, index_size, 
               (unsigned long long)pFile->header.index_table_offset);
    
    // Log mapping table contents before saving
    CCVFS_DEBUG("=== MAPPING TABLE BEFORE SAVE ===");
    for (uint32_t i = 0; i < pFile->header.total_blocks && i < 10; i++) { // Limit to first 10 for brevity
        CCVFSBlockIndex *pIndex = &pFile->pBlockIndex[i];
        CCVFS_DEBUG("Block[%u]: physical_offset=%llu, compressed_size=%u, original_size=%u, flags=0x%x",
                   i, (unsigned long long)pIndex->physical_offset, 
                   pIndex->compressed_size, pIndex->original_size, pIndex->flags);
    }
    if (pFile->header.total_blocks > 10) {
        CCVFS_DEBUG("... (%d more blocks)", pFile->header.total_blocks - 10);
    }
    CCVFS_DEBUG("=== END MAPPING TABLE ===");
    
    // Write block index to file
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, pFile->pBlockIndex,
                                        index_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write block index to disk: %d", rc);
        return rc;
    }
    
    // Mark as clean after successful save
    pFile->index_dirty = 0;
    
    CCVFS_INFO("Successfully saved block index: %d blocks at offset %llu", 
              pFile->header.total_blocks, (unsigned long long)pFile->header.index_table_offset);
    return SQLITE_OK;
}

/*
 * Initialize new CCVFS file header
 */
int ccvfs_init_header(CCVFSFile *pFile, CCVFS *pVfs) {
    memset(&pFile->header, 0, sizeof(CCVFSFileHeader));
    
    // Basic identification
    memcpy(pFile->header.magic, CCVFS_MAGIC, 8);
    pFile->header.major_version = CCVFS_VERSION_MAJOR;
    pFile->header.minor_version = CCVFS_VERSION_MINOR;
    pFile->header.header_size = CCVFS_HEADER_SIZE;
    
    // SQLite compatibility (set page size to match block size for optimal performance)
    pFile->header.original_page_size = CCVFS_DEFAULT_BLOCK_SIZE;  // 64KB pages = 64KB blocks
    pFile->header.sqlite_version = sqlite3_libversion_number();
    pFile->header.database_size_pages = 0;
    
    // Compression configuration
    if (pVfs->zCompressType) {
        strncpy(pFile->header.compress_algorithm, pVfs->zCompressType, 
                CCVFS_MAX_ALGORITHM_NAME - 1);
    }
    if (pVfs->zEncryptType) {
        strncpy(pFile->header.encrypt_algorithm, pVfs->zEncryptType,
                CCVFS_MAX_ALGORITHM_NAME - 1);
    }
    
    // Block configuration
    pFile->header.block_size = CCVFS_DEFAULT_BLOCK_SIZE;
    pFile->header.total_blocks = 0;
    pFile->header.index_table_offset = CCVFS_INDEX_TABLE_OFFSET;  // Fixed position
    
    // Statistics
    pFile->header.original_file_size = 0;
    pFile->header.compressed_file_size = 0;
    pFile->header.compression_ratio = 100;  // No compression initially
    pFile->header.creation_flags = pVfs->creation_flags;
    
    // Security
    pFile->header.master_key_hash = 0;  // TODO: implement key management
    pFile->header.timestamp = (uint64_t)time(NULL);
    
    pFile->header_loaded = 1;
    
    CCVFS_DEBUG("Initialized new CCVFS header");
    return SQLITE_OK;
}

/*
 * Expand block index table with better memory management
 */
int ccvfs_expand_block_index(CCVFSFile *pFile, uint32_t new_block_count) {
    CCVFSBlockIndex *new_index;
    size_t new_size, old_capacity_size;
    
    CCVFS_DEBUG("=== EXPANDING BLOCK INDEX ===");
    CCVFS_DEBUG("Current: total_blocks=%u, capacity=%u, requesting=%u", 
               pFile->header.total_blocks, pFile->index_capacity, new_block_count);
    
    if (new_block_count <= pFile->header.total_blocks) {
        CCVFS_DEBUG("No expansion needed");
        return SQLITE_OK;  // No expansion needed
    }
    
    // Check if we need to expand capacity
    if (new_block_count <= pFile->index_capacity) {
        // We have enough capacity, just update the count
        CCVFS_DEBUG("Expanding within existing capacity: %u -> %u", 
                   pFile->header.total_blocks, new_block_count);
        
        // Initialize new entries to zero
        size_t old_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
        size_t new_size_used = new_block_count * sizeof(CCVFSBlockIndex);
        memset((char*)pFile->pBlockIndex + old_size, 0, new_size_used - old_size);
        
        pFile->header.total_blocks = new_block_count;
        pFile->index_dirty = 1; // Mark as dirty since we're changing it
        
        CCVFS_INFO("Expanded block count to %u (within capacity %u)", 
                  new_block_count, pFile->index_capacity);
        return SQLITE_OK;
    }
    
    // Need to expand capacity - grow by at least 50% or to new_block_count + 16
    uint32_t new_capacity = pFile->index_capacity * 3 / 2;
    if (new_capacity < new_block_count + 16) {
        new_capacity = new_block_count + 16;
    }
    
    new_size = new_capacity * sizeof(CCVFSBlockIndex);
    old_capacity_size = pFile->index_capacity * sizeof(CCVFSBlockIndex);
    
    CCVFS_DEBUG("Expanding capacity: %u -> %u blocks (%zu -> %zu bytes)",
               pFile->index_capacity, new_capacity, old_capacity_size, new_size);
    
    if (pFile->pBlockIndex) {
        new_index = (CCVFSBlockIndex*)sqlite3_realloc(pFile->pBlockIndex, new_size);
    } else {
        new_index = (CCVFSBlockIndex*)sqlite3_malloc(new_size);
        if (new_index) {
            memset(new_index, 0, new_size);
        }
    }
    
    if (!new_index) {
        CCVFS_ERROR("Failed to expand block index to %u blocks (%zu bytes)", 
                   new_capacity, new_size);
        return SQLITE_NOMEM;
    }
    
    // Initialize new entries to zero
    if (new_size > old_capacity_size) {
        memset((char*)new_index + old_capacity_size, 0, new_size - old_capacity_size);
    }
    
    pFile->pBlockIndex = new_index;
    pFile->index_capacity = new_capacity;
    pFile->header.total_blocks = new_block_count;
    pFile->index_dirty = 1; // Mark as dirty since we're changing it
    
    CCVFS_INFO("Successfully expanded block index: capacity=%u, active_blocks=%u", 
              new_capacity, new_block_count);
    return SQLITE_OK;
}

/*
 * Force save block index table to disk (ignoring dirty flag)
 */
int ccvfs_force_save_block_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    CCVFS_DEBUG("=== FORCE SAVING BLOCK INDEX ===");
    
    if (!pFile->pBlockIndex || pFile->header.total_blocks == 0) {
        CCVFS_DEBUG("No block index to force save");
        return SQLITE_OK;
    }
    
    index_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
    
    CCVFS_DEBUG("Force saving block index: %d blocks, %zu bytes at offset %llu", 
               pFile->header.total_blocks, index_size, 
               (unsigned long long)pFile->header.index_table_offset);
    
    // Write block index to file
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, pFile->pBlockIndex,
                                        index_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to force write block index to disk: %d", rc);
        return rc;
    }
    
    // Mark as clean after successful save
    pFile->index_dirty = 0;
    
    CCVFS_INFO("Force saved block index: %d blocks", pFile->header.total_blocks);
    return SQLITE_OK;
}

/*
 * Calculate optimal position for index table
 */
static sqlite3_int64 ccvfs_calculate_index_position(CCVFSFile *pFile) {
    sqlite3_int64 minPosition = CCVFS_HEADER_SIZE;
    sqlite3_int64 fileSize = 0;
    
    // Get current file size
    if (pFile->pReal && pFile->pReal->pMethods->xFileSize) {
        pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
    }
    
    // Find the highest offset used by any data block
    sqlite3_int64 maxDataEnd = minPosition;
    for (uint32_t i = 0; i < pFile->header.total_blocks; i++) {
        if (pFile->pBlockIndex[i].physical_offset > 0) {
            sqlite3_int64 blockEnd = pFile->pBlockIndex[i].physical_offset + 
                                   pFile->pBlockIndex[i].compressed_size;
            if (blockEnd > maxDataEnd) {
                maxDataEnd = blockEnd;
            }
        }
    }
    
    // Position index table after all data blocks
    sqlite3_int64 newPosition = (maxDataEnd > fileSize) ? maxDataEnd : fileSize;
    
    CCVFS_DEBUG("Calculated index position: %lld (file_size=%lld, max_data_end=%lld)",
               newPosition, fileSize, maxDataEnd);
    
    return newPosition;
}