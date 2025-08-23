#include "ccvfs_core.h"
#include "ccvfs_algorithm.h"

#include <string.h>

#include "ccvfs_io.h"

/* 全局变量存储解析后的密钥 */
static unsigned char g_encryption_key[32];  /* 支持最大32字节密钥 */
static int g_key_length = 0;
static int g_key_set = 0;

/*
** 设置全局加密密钥
*/
void ccvfs_set_encryption_key(const unsigned char *key, int keyLen) {
    if (key && keyLen > 0 && keyLen <= sizeof(g_encryption_key)) {
        memcpy(g_encryption_key, key, keyLen);
        g_key_length = keyLen;
        g_key_set = 1;
    } else {
        g_key_set = 0;
        g_key_length = 0;
    }
}

/*
** 获取全局加密密钥
*/
int ccvfs_get_encryption_key(unsigned char *key, int maxLen) {
    if (!g_key_set || !key) {
        return 0;
    }

    int copyLen = (g_key_length > maxLen) ? maxLen : g_key_length;
    memcpy(key, g_encryption_key, copyLen);
    return copyLen;
}

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

/*
 * Create compression and encryption VFS
 */
int sqlite3_ccvfs_create(
    const char *zVfsName,
    sqlite3_vfs *pRootVfs,
    const CompressAlgorithm *pCompressAlg,
    const EncryptAlgorithm *pEncryptAlg,
    uint32_t pageSize,
    uint32_t flags
) {
    CCVFS *pNew;
    sqlite3_vfs *pExist;
    int nName;
    int nByte;
    
    // Use default page size if not specified
    if (pageSize == 0) {
        pageSize = CCVFS_DEFAULT_PAGE_SIZE;
    } else {
        // Validate page size
        if (pageSize < CCVFS_MIN_PAGE_SIZE || pageSize > CCVFS_MAX_PAGE_SIZE) {
            CCVFS_ERROR("Invalid page size: %u (must be between %u and %u)",
                        pageSize, CCVFS_MIN_PAGE_SIZE, CCVFS_MAX_PAGE_SIZE);
            return SQLITE_ERROR;
        }
        
        // Check if page size is power of 2
        if ((pageSize & (pageSize - 1)) != 0) {
            CCVFS_ERROR("Page size must be a power of 2: %u", pageSize);
            return SQLITE_ERROR;
        }
    }
    
    CCVFS_DEBUG("Creating CCVFS: name=%s, compression=%s, encryption=%s, page_size=%u, flags=0x%x",
                zVfsName, 
                pCompressAlg ? pCompressAlg->name : "(none)", 
                pEncryptAlg ? pEncryptAlg->name : "(none)", 
                pageSize, flags);
    
    // Initialize builtin algorithms
    ccvfs_init_builtin_algorithms();
    
    // Check if VFS already exists
    pExist = sqlite3_vfs_find(zVfsName);
    if (pExist) {
        CCVFS_ERROR("VFS already exists: %s", zVfsName);
        return SQLITE_ERROR;
    }
    
    // Use default VFS if not specified
    if (!pRootVfs) {
        pRootVfs = sqlite3_vfs_find(NULL);
        if (!pRootVfs) {
            CCVFS_ERROR("No default VFS available");
            return SQLITE_ERROR;
        }
    }
    
    // Calculate memory needed (no need to store algorithm names)
    nName = (int)strlen(zVfsName) + 1;
    nByte = sizeof(CCVFS) + nName;
    
    // Allocate memory
    pNew = (CCVFS*)sqlite3_malloc(nByte);
    if (!pNew) {
        CCVFS_ERROR("Failed to allocate memory for CCVFS");
        return SQLITE_NOMEM;
    }
    
    memset(pNew, 0, nByte);
    
    // Initialize VFS structure
    pNew->base.iVersion = 3;
    pNew->base.szOsFile = sizeof(CCVFSFile) + pRootVfs->szOsFile;
    pNew->base.mxPathname = pRootVfs->mxPathname;
    pNew->base.zName = (char*)&pNew[1];
    strcpy((char*)pNew->base.zName, zVfsName);
    
    // Set VFS methods
    pNew->base.xOpen = ccvfsOpen;
    pNew->base.xDelete = ccvfsDelete;
    pNew->base.xAccess = ccvfsAccess;
    pNew->base.xFullPathname = ccvfsFullPathname;
    pNew->base.xDlOpen = ccvfsDlOpen;
    pNew->base.xDlError = ccvfsDlError;
    pNew->base.xDlSym = ccvfsDlSym;
    pNew->base.xDlClose = ccvfsDlClose;
    pNew->base.xRandomness = ccvfsRandomness;
    pNew->base.xSleep = ccvfsSleep;
    pNew->base.xCurrentTime = ccvfsCurrentTime;
    pNew->base.xGetLastError = ccvfsGetLastError;
    pNew->base.xCurrentTimeInt64 = ccvfsCurrentTimeInt64;
    pNew->base.xSetSystemCall = ccvfsSetSystemCall;
    pNew->base.xGetSystemCall = ccvfsGetSystemCall;
    pNew->base.xNextSystemCall = ccvfsNextSystemCall;
    
    // Set CCVFS specific data
    pNew->pRootVfs = pRootVfs;
    pNew->creation_flags = flags;
    pNew->page_size = pageSize;
    
    // Initialize hole detection configuration with defaults
    pNew->enable_hole_detection = 1;
    pNew->max_holes = CCVFS_DEFAULT_MAX_HOLES;
    pNew->min_hole_size = CCVFS_DEFAULT_MIN_HOLE_SIZE;
    
    // Initialize write buffer configuration with defaults
    pNew->enable_write_buffer = CCVFS_DEFAULT_BUFFER_ENABLED;
    pNew->max_buffer_entries = CCVFS_DEFAULT_MAX_BUFFER_ENTRIES;
    pNew->max_buffer_size = CCVFS_DEFAULT_MAX_BUFFER_SIZE;
    pNew->auto_flush_pages = CCVFS_DEFAULT_AUTO_FLUSH_PAGES;
    
    // Initialize data integrity configuration with defaults
    pNew->strict_checksum_mode = 1;
    pNew->enable_data_recovery = 0;
    pNew->corruption_tolerance = 0;
    
    // Directly assign algorithm pointers (much more efficient!)
    pNew->pCompressAlg = (CompressAlgorithm*)pCompressAlg;
    pNew->pEncryptAlg = (EncryptAlgorithm*)pEncryptAlg;
    
    // Store algorithm names for compatibility (optional)
    if (pCompressAlg) {
        pNew->zCompressType = pCompressAlg->name;
    }
    if (pEncryptAlg) {
        pNew->zEncryptType = pEncryptAlg->name;
    }
    
    // Register VFS
    int rc = sqlite3_vfs_register(&pNew->base, 0);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to register VFS: %d", rc);
        sqlite3_free(pNew);
        return rc;
    }
    
    CCVFS_INFO("Successfully created CCVFS: %s", zVfsName);
    return SQLITE_OK;
}

/*
 * Destroy compression and encryption VFS
 */
int sqlite3_ccvfs_destroy(const char *zVfsName) {
    sqlite3_vfs *pVfs;
    CCVFS *pCcvfs;
    
    CCVFS_DEBUG("Destroying CCVFS: %s", zVfsName);
    
    pVfs = sqlite3_vfs_find(zVfsName);
    if (!pVfs) {
        CCVFS_ERROR("VFS not found: %s", zVfsName);
        return SQLITE_ERROR;
    }
    
    pCcvfs = (CCVFS*)pVfs;
    
    // Unregister VFS
    sqlite3_vfs_unregister(pVfs);
    
    // Free memory
    sqlite3_free(pCcvfs);
    
    CCVFS_INFO("Successfully destroyed CCVFS: %s", zVfsName);
    return SQLITE_OK;
}

/*
 * Configure write buffer settings for a VFS
 */
int sqlite3_ccvfs_configure_write_buffer(
    const char *zVfsName,
    int enabled,
    uint32_t max_entries,
    uint32_t max_buffer_size,
    uint32_t auto_flush_pages
) {
    sqlite3_vfs *pVfs;
    CCVFS *pCcvfs;
    
    CCVFS_DEBUG("Configuring write buffer for VFS: %s", zVfsName);
    
    pVfs = sqlite3_vfs_find(zVfsName);
    if (!pVfs) {
        CCVFS_ERROR("VFS not found: %s", zVfsName);
        return SQLITE_ERROR;
    }
    
    pCcvfs = (CCVFS*)pVfs;
    
    // Configure write buffer settings
    pCcvfs->enable_write_buffer = enabled ? 1 : 0;
    
    if (max_entries > 0) {
        if (max_entries < CCVFS_MIN_BUFFER_ENTRIES) {
            max_entries = CCVFS_MIN_BUFFER_ENTRIES;
        } else if (max_entries > CCVFS_MAX_BUFFER_ENTRIES) {
            max_entries = CCVFS_MAX_BUFFER_ENTRIES;
        }
        pCcvfs->max_buffer_entries = max_entries;
    }
    
    if (max_buffer_size > 0) {
        if (max_buffer_size < CCVFS_MIN_BUFFER_SIZE) {
            max_buffer_size = CCVFS_MIN_BUFFER_SIZE;
        } else if (max_buffer_size > CCVFS_MAX_BUFFER_SIZE) {
            max_buffer_size = CCVFS_MAX_BUFFER_SIZE;
        }
        pCcvfs->max_buffer_size = max_buffer_size;
    }
    
    if (auto_flush_pages > 0) {
        pCcvfs->auto_flush_pages = auto_flush_pages;
    }
    
    CCVFS_INFO("Write buffer configured: enabled=%d, max_entries=%u, max_size=%u KB, auto_flush=%u",
              pCcvfs->enable_write_buffer, pCcvfs->max_buffer_entries, 
              pCcvfs->max_buffer_size / 1024, pCcvfs->auto_flush_pages);
    
    return SQLITE_OK;
}

/*
 * Get write buffer statistics for an open database
 */
int sqlite3_ccvfs_get_buffer_stats(
    sqlite3 *db,
    uint32_t *buffer_hits,
    uint32_t *buffer_flushes,
    uint32_t *buffer_merges,
    uint32_t *total_buffered_writes
) {
    sqlite3_file *pFile;
    CCVFSFile *pCcvfsFile;
    
    if (!db) {
        CCVFS_ERROR("Database connection is NULL");
        return SQLITE_ERROR;
    }
    
    // Get the main database file
    int rc = sqlite3_file_control(db, NULL, SQLITE_FCNTL_FILE_POINTER, &pFile);
    if (rc != SQLITE_OK || !pFile) {
        CCVFS_ERROR("Failed to get file pointer from database: %d", rc);
        return SQLITE_ERROR;
    }
    
    // Check if this is a CCVFS file
    pCcvfsFile = (CCVFSFile*)pFile;
    if (!pCcvfsFile->is_ccvfs_file) {
        CCVFS_ERROR("Database is not using CCVFS");
        return SQLITE_ERROR;
    }
    
    // Return statistics
    if (buffer_hits) *buffer_hits = pCcvfsFile->buffer_hit_count;
    if (buffer_flushes) *buffer_flushes = pCcvfsFile->buffer_flush_count;
    if (buffer_merges) *buffer_merges = pCcvfsFile->buffer_merge_count;
    if (total_buffered_writes) *total_buffered_writes = pCcvfsFile->total_buffered_writes;
    
    CCVFS_DEBUG("Buffer stats: hits=%u, flushes=%u, merges=%u, total_writes=%u",
               pCcvfsFile->buffer_hit_count, pCcvfsFile->buffer_flush_count,
               pCcvfsFile->buffer_merge_count, pCcvfsFile->total_buffered_writes);
    
    return SQLITE_OK;
}

/*
 * Force flush write buffer for an open database
 */
int sqlite3_ccvfs_flush_write_buffer(sqlite3 *db) {
    sqlite3_file *pFile;
    CCVFSFile *pCcvfsFile;
    
    if (!db) {
        CCVFS_ERROR("Database connection is NULL");
        return SQLITE_ERROR;
    }
    
    // Get the main database file
    int rc = sqlite3_file_control(db, NULL, SQLITE_FCNTL_FILE_POINTER, &pFile);
    if (rc != SQLITE_OK || !pFile) {
        CCVFS_ERROR("Failed to get file pointer from database: %d", rc);
        return SQLITE_ERROR;
    }
    
    // Check if this is a CCVFS file
    pCcvfsFile = (CCVFSFile*)pFile;
    if (!pCcvfsFile->is_ccvfs_file) {
        CCVFS_ERROR("Database is not using CCVFS");
        return SQLITE_ERROR;
    }
    
    // Flush write buffer
    if (pCcvfsFile->write_buffer.enabled && pCcvfsFile->write_buffer.entry_count > 0) {
        CCVFS_DEBUG("Force flushing %u buffered entries", pCcvfsFile->write_buffer.entry_count);
        rc = ccvfs_flush_write_buffer(pCcvfsFile);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to flush write buffer: %d", rc);
            return rc;
        }
        CCVFS_INFO("Write buffer flushed successfully");
    } else {
        CCVFS_DEBUG("No buffered data to flush");
    }
    
    return SQLITE_OK;
}

/*
 * Activate CCVFS (similar to sqlite3_activate_cerod)
 */
int sqlite3_activate_ccvfs(const CompressAlgorithm *pCompressAlg, const EncryptAlgorithm *pEncryptAlg) {
    static int isActivated = 0;
    int rc;
    
    CCVFS_DEBUG("Activating CCVFS: compression=%s, encryption=%s", 
                pCompressAlg ? pCompressAlg->name : "(none)", 
                pEncryptAlg ? pEncryptAlg->name : "(none)");
    
    if (isActivated) {
        CCVFS_INFO("CCVFS already activated");
        return SQLITE_OK;
    }
    
    rc = sqlite3_ccvfs_create("ccvfs", NULL, pCompressAlg, pEncryptAlg, 0, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to activate CCVFS: %d", rc);
        return rc;
    }
    
    // Set ccvfs as default VFS
    sqlite3_vfs *ccvfs = sqlite3_vfs_find("ccvfs");
    if (ccvfs) {
        sqlite3_vfs_register(ccvfs, 1);
        isActivated = 1;
        CCVFS_INFO("CCVFS activated successfully, set as default VFS");
        return SQLITE_OK;
    } else {
        CCVFS_ERROR("Cannot find the newly created CCVFS");
        return SQLITE_ERROR;
    }
}