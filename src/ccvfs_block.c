#include "ccvfs_block.h"
#include "ccvfs_utils.h"

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
    
    // Verify header checksum
    uint32_t calculated_checksum = ccvfs_crc32((const unsigned char*)&pFile->header,
                                               CCVFS_HEADER_SIZE - sizeof(uint32_t));
    if (pFile->header.header_checksum != calculated_checksum) {
        CCVFS_ERROR("Header checksum mismatch");
        return SQLITE_IOERR_READ;
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
    
    if (!pFile->header_loaded) {
        rc = ccvfs_load_header(pFile);
        if (rc != SQLITE_OK) {
            return rc;
        }
    }
    
    if (pFile->header.total_blocks == 0) {
        CCVFS_DEBUG("No blocks in file");
        return SQLITE_OK;
    }
    
    // Allocate memory for block index
    index_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
    pFile->pBlockIndex = (CCVFSBlockIndex*)sqlite3_malloc(index_size);
    if (!pFile->pBlockIndex) {
        CCVFS_ERROR("Failed to allocate memory for block index");
        return SQLITE_NOMEM;
    }
    
    // Read block index from file
    rc = pFile->pReal->pMethods->xRead(pFile->pReal, pFile->pBlockIndex,
                                       index_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read block index");
        sqlite3_free(pFile->pBlockIndex);
        pFile->pBlockIndex = NULL;
        return rc;
    }
    
    CCVFS_DEBUG("Loaded block index: %d blocks", pFile->header.total_blocks);
    return SQLITE_OK;
}

/*
 * Save block index table to disk
 */
int ccvfs_save_block_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    if (!pFile->pBlockIndex || pFile->header.total_blocks == 0) {
        return SQLITE_OK;
    }
    
    index_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
    
    // Write block index to file
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, pFile->pBlockIndex,
                                        index_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write block index");
        return rc;
    }
    
    CCVFS_DEBUG("Saved block index: %d blocks", pFile->header.total_blocks);
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
    
    // SQLite compatibility (will be filled when first SQLite page is written)
    pFile->header.original_page_size = 4096;  // Default SQLite page size
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
    pFile->header.index_table_offset = CCVFS_HEADER_SIZE;
    
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
 * Expand block index table
 */
int ccvfs_expand_block_index(CCVFSFile *pFile, uint32_t new_block_count) {
    CCVFSBlockIndex *new_index;
    size_t new_size, old_size;
    
    if (new_block_count <= pFile->header.total_blocks) {
        return SQLITE_OK;  // No expansion needed
    }
    
    new_size = new_block_count * sizeof(CCVFSBlockIndex);
    old_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
    
    if (pFile->pBlockIndex) {
        new_index = (CCVFSBlockIndex*)sqlite3_realloc(pFile->pBlockIndex, new_size);
    } else {
        new_index = (CCVFSBlockIndex*)sqlite3_malloc(new_size);
    }
    
    if (!new_index) {
        CCVFS_ERROR("Failed to expand block index");
        return SQLITE_NOMEM;
    }
    
    // Initialize new entries to zero
    if (new_size > old_size) {
        memset((char*)new_index + old_size, 0, new_size - old_size);
    }
    
    pFile->pBlockIndex = new_index;
    pFile->header.total_blocks = new_block_count;
    
    CCVFS_DEBUG("Expanded block index to %d blocks", new_block_count);
    return SQLITE_OK;
}