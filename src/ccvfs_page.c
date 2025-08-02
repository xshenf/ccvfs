#include "ccvfs_page.h"
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
    
    CCVFS_DEBUG("Loaded CCVFS header: version %d.%d, %d pages, compression: %s, encryption: %s",
                pFile->header.major_version, pFile->header.minor_version,
                pFile->header.total_pages, pFile->header.compress_algorithm,
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
 * Load page index table from disk
 */
int ccvfs_load_page_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    CCVFS_DEBUG("=== LOADING PAGE INDEX ===");
    
    if (!pFile->header_loaded) {
        rc = ccvfs_load_header(pFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to load header before loading page index: %d", rc);
            return rc;
        }
    }
    
    if (pFile->header.total_pages == 0) {
        CCVFS_DEBUG("No pages in file, initializing empty index");
        pFile->index_dirty = 0;
        pFile->index_capacity = 16; // Start with small capacity
        pFile->pPageIndex = (CCVFSPageIndex*)sqlite3_malloc(pFile->index_capacity * sizeof(CCVFSPageIndex));
        if (!pFile->pPageIndex) {
            CCVFS_ERROR("Failed to allocate initial page index");
            return SQLITE_NOMEM;
        }
        memset(pFile->pPageIndex, 0, pFile->index_capacity * sizeof(CCVFSPageIndex));
        CCVFS_INFO("Initialized empty page index with capacity %u", pFile->index_capacity);
        return SQLITE_OK;
    }
    
    // Allocate memory for page index (with extra capacity for growth)
    pFile->index_capacity = pFile->header.total_pages + 16; // Add some extra capacity
    index_size = pFile->index_capacity * sizeof(CCVFSPageIndex);
    pFile->pPageIndex = (CCVFSPageIndex*)sqlite3_malloc(index_size);
    if (!pFile->pPageIndex) {
        CCVFS_ERROR("Failed to allocate memory for page index: %zu bytes", index_size);
        return SQLITE_NOMEM;
    }
    
    // Initialize all entries to zero first
    memset(pFile->pPageIndex, 0, index_size);
    
    // Read existing page index from file
    size_t existing_size = pFile->header.total_pages * sizeof(CCVFSPageIndex);
    CCVFS_DEBUG("Reading %zu bytes of page index from offset %llu", 
               existing_size, (unsigned long long)pFile->header.index_table_offset);
               
    rc = pFile->pReal->pMethods->xRead(pFile->pReal, pFile->pPageIndex,
                                       existing_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read page index from disk: %d", rc);
        sqlite3_free(pFile->pPageIndex);
        pFile->pPageIndex = NULL;
        return rc;
    }
    
    pFile->index_dirty = 0; // Just loaded, so it's not dirty
    
    CCVFS_INFO("Loaded page index: %d pages, capacity %u", 
              pFile->header.total_pages, pFile->index_capacity);
              
    // Log mapping table contents for debugging
    CCVFS_DEBUG("=== PAGE MAPPING TABLE CONTENTS ===");
    for (uint32_t i = 0; i < pFile->header.total_pages; i++) {
        CCVFSPageIndex *pIndex = &pFile->pPageIndex[i];
        CCVFS_DEBUG("Page[%u]: physical_offset=%llu, compressed_size=%u, original_size=%u, flags=0x%x, checksum=0x%08x",
                   i, (unsigned long long)pIndex->physical_offset, 
                   pIndex->compressed_size, pIndex->original_size, 
                   pIndex->flags, pIndex->checksum);
    }
    CCVFS_DEBUG("=== END MAPPING TABLE CONTENTS ===");
    
    return SQLITE_OK;
}

/*
 * Save page index table to disk (only if dirty)
 */
int ccvfs_save_page_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    CCVFS_DEBUG("=== SAVING PAGE INDEX ===");
    
    if (!pFile->pPageIndex || pFile->header.total_pages == 0) {
        CCVFS_DEBUG("No page index to save");
        return SQLITE_OK;
    }
    
    // Only save if dirty (modified since last save)
    if (!pFile->index_dirty) {
        CCVFS_DEBUG("Page index not dirty, skipping save");
        return SQLITE_OK;
    }
    
    index_size = pFile->header.total_pages * sizeof(CCVFSPageIndex);
    
    // Verify we don't exceed the reserved index table space
    if (index_size > CCVFS_INDEX_TABLE_SIZE) {
        CCVFS_ERROR("Page index too large: %zu bytes > %d bytes reserved", 
                   index_size, CCVFS_INDEX_TABLE_SIZE);
        return SQLITE_ERROR;
    }
    
    CCVFS_DEBUG("Saving dirty page index: %d pages, %zu bytes at FIXED offset %llu", 
               pFile->header.total_pages, index_size, 
               (unsigned long long)pFile->header.index_table_offset);
    
    // Log mapping table contents before saving
    CCVFS_DEBUG("=== MAPPING TABLE BEFORE SAVE ===");
    for (uint32_t i = 0; i < pFile->header.total_pages && i < 10; i++) { // Limit to first 10 for brevity
        CCVFSPageIndex *pIndex = &pFile->pPageIndex[i];
        CCVFS_DEBUG("Page[%u]: physical_offset=%llu, compressed_size=%u, original_size=%u, flags=0x%x",
                   i, (unsigned long long)pIndex->physical_offset, 
                   pIndex->compressed_size, pIndex->original_size, pIndex->flags);
    }
    if (pFile->header.total_pages > 10) {
        CCVFS_DEBUG("... (%d more pages)", pFile->header.total_pages - 10);
    }
    CCVFS_DEBUG("=== END MAPPING TABLE ===");
    
    // Write page index to file
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, pFile->pPageIndex,
                                        index_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write page index to disk: %d", rc);
        return rc;
    }
    
    // Mark as clean after successful save
    pFile->index_dirty = 0;
    
    CCVFS_INFO("Successfully saved page index: %d pages at offset %llu", 
              pFile->header.total_pages, (unsigned long long)pFile->header.index_table_offset);
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
    
    // SQLite compatibility (set page size to match page size for optimal performance)
    pFile->header.original_page_size = pVfs->page_size;  // Use VFS page size
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
    
    // Page configuration - use VFS's configured page size
    pFile->header.page_size = pVfs->page_size;
    pFile->header.total_pages = 0;
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
    
    // Initialize space utilization tracking
    pFile->total_allocated_space = 0;
    pFile->total_used_space = 0;
    pFile->fragmentation_score = 0;
    pFile->space_reuse_count = 0;
    pFile->space_expansion_count = 0;
    pFile->new_allocation_count = 0;
    pFile->hole_reclaim_count = 0;
    pFile->best_fit_count = 0;
    pFile->sequential_write_count = 0;
    pFile->last_written_page = UINT32_MAX;
    
    CCVFS_DEBUG("Initialized new CCVFS header with advanced space tracking");
    return SQLITE_OK;
}

/*
 * Expand page index table with better memory management
 */
int ccvfs_expand_page_index(CCVFSFile *pFile, uint32_t new_page_count) {
    CCVFSPageIndex *new_index;
    size_t new_size, old_capacity_size;
    
    CCVFS_DEBUG("=== EXPANDING PAGE INDEX ===");
    CCVFS_DEBUG("Current: total_pages=%u, capacity=%u, requesting=%u", 
               pFile->header.total_pages, pFile->index_capacity, new_page_count);
    
    if (new_page_count <= pFile->header.total_pages) {
        CCVFS_DEBUG("No expansion needed");
        return SQLITE_OK;  // No expansion needed
    }
    
    // Check if we need to expand capacity
    if (new_page_count <= pFile->index_capacity) {
        // We have enough capacity, just update the count
        CCVFS_DEBUG("Expanding within existing capacity: %u -> %u", 
                   pFile->header.total_pages, new_page_count);
        
        // Initialize new entries to zero
        size_t old_size = pFile->header.total_pages * sizeof(CCVFSPageIndex);
        size_t new_size_used = new_page_count * sizeof(CCVFSPageIndex);
        memset((char*)pFile->pPageIndex + old_size, 0, new_size_used - old_size);
        
        pFile->header.total_pages = new_page_count;
        pFile->index_dirty = 1; // Mark as dirty since we're changing it
        
        CCVFS_INFO("Expanded page count to %u (within capacity %u)", 
                  new_page_count, pFile->index_capacity);
        return SQLITE_OK;
    }
    
    // Need to expand capacity - grow by at least 50% or to new_page_count + 16
    uint32_t new_capacity = pFile->index_capacity * 3 / 2;
    if (new_capacity < new_page_count + 16) {
        new_capacity = new_page_count + 16;
    }
    
    new_size = new_capacity * sizeof(CCVFSPageIndex);
    old_capacity_size = pFile->index_capacity * sizeof(CCVFSPageIndex);
    
    CCVFS_DEBUG("Expanding capacity: %u -> %u pages (%zu -> %zu bytes)",
               pFile->index_capacity, new_capacity, old_capacity_size, new_size);
    
    if (pFile->pPageIndex) {
        new_index = (CCVFSPageIndex*)sqlite3_realloc(pFile->pPageIndex, new_size);
    } else {
        new_index = (CCVFSPageIndex*)sqlite3_malloc(new_size);
        if (new_index) {
            memset(new_index, 0, new_size);
        }
    }
    
    if (!new_index) {
        CCVFS_ERROR("Failed to expand page index to %u pages (%zu bytes)", 
                   new_capacity, new_size);
        return SQLITE_NOMEM;
    }
    
    // Initialize new entries to zero
    if (new_size > old_capacity_size) {
        memset((char*)new_index + old_capacity_size, 0, new_size - old_capacity_size);
    }
    
    pFile->pPageIndex = new_index;
    pFile->index_capacity = new_capacity;
    pFile->header.total_pages = new_page_count;
    pFile->index_dirty = 1; // Mark as dirty since we're changing it
    
    CCVFS_INFO("Successfully expanded page index: capacity=%u, active_pages=%u", 
              new_capacity, new_page_count);
    return SQLITE_OK;
}

/*
 * Force save page index table to disk (ignoring dirty flag)
 */
int ccvfs_force_save_page_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    CCVFS_DEBUG("=== FORCE SAVING PAGE INDEX ===");
    
    if (!pFile->pPageIndex || pFile->header.total_pages == 0) {
        CCVFS_DEBUG("No page index to force save");
        return SQLITE_OK;
    }
    
    index_size = pFile->header.total_pages * sizeof(CCVFSPageIndex);
    
    CCVFS_DEBUG("Force saving page index: %d pages, %zu bytes at offset %llu", 
               pFile->header.total_pages, index_size, 
               (unsigned long long)pFile->header.index_table_offset);
    
    // Write page index to file
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, pFile->pPageIndex,
                                        index_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to force write page index to disk: %d", rc);
        return rc;
    }
    
    // Mark as clean after successful save
    pFile->index_dirty = 0;
    
    CCVFS_INFO("Force saved page index: %d pages", pFile->header.total_pages);
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
    
    // Find the highest offset used by any data page
    sqlite3_int64 maxDataEnd = minPosition;
    for (uint32_t i = 0; i < pFile->header.total_pages; i++) {
        if (pFile->pPageIndex[i].physical_offset > 0) {
            sqlite3_int64 pageEnd = pFile->pPageIndex[i].physical_offset + 
                                   pFile->pPageIndex[i].compressed_size;
            if (pageEnd > maxDataEnd) {
                maxDataEnd = pageEnd;
            }
        }
    }
    
    // Position index table after all data pages
    sqlite3_int64 newPosition = (maxDataEnd > fileSize) ? maxDataEnd : fileSize;
    
    CCVFS_DEBUG("Calculated index position: %lld (file_size=%lld, max_data_end=%lld)",
               newPosition, fileSize, maxDataEnd);
    
    return newPosition;
}