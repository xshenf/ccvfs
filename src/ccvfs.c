#include "ccvfs_core.h"
#include "ccvfs_algorithm.h"

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

/*
 * Create compression and encryption VFS
 */
int sqlite3_ccvfs_create(
    const char *zVfsName,
    sqlite3_vfs *pRootVfs,
    const char *zCompressType,
    const char *zEncryptType,
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
                zVfsName, zCompressType ? zCompressType : "(none)", 
                zEncryptType ? zEncryptType : "(none)", pageSize, flags);
    
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
    
    // Calculate memory needed
    nName = (int)strlen(zVfsName) + 1;
    nByte = sizeof(CCVFS) + nName;
    if (zCompressType) nByte += (int)strlen(zCompressType) + 1;
    if (zEncryptType) nByte += (int)strlen(zEncryptType) + 1;
    
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
    pNew->page_size = pageSize;  // Use the validated page size
    
    // Copy algorithm names
    char *pDest = (char*)&pNew[1] + nName;
    
    if (zCompressType) {
        pNew->zCompressType = pDest;
        strcpy(pDest, zCompressType);
        pDest += strlen(zCompressType) + 1;
        
        // Find compression algorithm
        pNew->pCompressAlg = ccvfs_find_compress_algorithm(zCompressType);
        if (!pNew->pCompressAlg) {
            CCVFS_ERROR("Compression algorithm not found: %s", zCompressType);
            sqlite3_free(pNew);
            return SQLITE_ERROR;
        }
    }
    
    if (zEncryptType) {
        pNew->zEncryptType = pDest;
        strcpy(pDest, zEncryptType);
        
        // Find encryption algorithm
        pNew->pEncryptAlg = ccvfs_find_encrypt_algorithm(zEncryptType);
        if (!pNew->pEncryptAlg) {
            CCVFS_ERROR("Encryption algorithm not found: %s", zEncryptType);
            sqlite3_free(pNew);
            return SQLITE_ERROR;
        }
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
 * Activate CCVFS (similar to sqlite3_activate_cerod)
 */
int sqlite3_activate_ccvfs(const char *zCompressType, const char *zEncryptType) {
    static int isActivated = 0;
    int rc;
    
    CCVFS_DEBUG("Activating CCVFS: compression=%s, encryption=%s", 
                zCompressType ? zCompressType : "(none)", 
                zEncryptType ? zEncryptType : "(none)");
    
    if (isActivated) {
        CCVFS_INFO("CCVFS already activated");
        return SQLITE_OK;
    }
    
    rc = sqlite3_ccvfs_create("ccvfs", NULL, zCompressType, zEncryptType, 0, CCVFS_CREATE_REALTIME);
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