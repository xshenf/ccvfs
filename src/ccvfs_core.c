#include "ccvfs_core.h"
#include "ccvfs_io.h"
#include "ccvfs_algorithm.h"
#include "ccvfs_block.h"

/*
 * Open file
 */
int ccvfsOpen(sqlite3_vfs *pVfs, sqlite3_filename zName, sqlite3_file *pFile, 
              int flags, int *pOutFlags) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    CCVFSFile *pCcvfsFile = (CCVFSFile*)pFile;
    sqlite3_file *pRealFile;
    int rc;
    
    CCVFS_DEBUG("Opening file: %s, flags: %d", zName ? zName : "(temp)", flags);
    
    // Initialize CCVFS file structure
    memset(pCcvfsFile, 0, sizeof(CCVFSFile));
    pCcvfsFile->base.pMethods = &ccvfsIoMethods;
    pCcvfsFile->pOwner = pCcvfs;
    pCcvfsFile->open_flags = flags;
    pCcvfsFile->is_ccvfs_file = 0;  // Default to non-CCVFS file
    pCcvfsFile->header_loaded = 0;
    
    // Allocate space for the real file structure
    pRealFile = (sqlite3_file*)&pCcvfsFile[1];
    
    // Open the underlying file
    rc = pCcvfs->pRootVfs->xOpen(pCcvfs->pRootVfs, zName, pRealFile, flags, pOutFlags);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to open underlying file: %d", rc);
        return rc;
    }
    
    pCcvfsFile->pReal = pRealFile;
    
    // Determine file type at open time
    if (flags & SQLITE_OPEN_CREATE) {
        // Creating new file - it will be CCVFS format if we have compression/encryption
        if (pCcvfs->pCompressAlg || pCcvfs->pEncryptAlg) {
            pCcvfsFile->is_ccvfs_file = 1;
            CCVFS_DEBUG("Creating new CCVFS file");
        } else {
            CCVFS_DEBUG("Creating new regular file (no compression/encryption)");
        }
    } else {
        // Opening existing file - check if it's CCVFS format
        sqlite3_int64 fileSize;
        rc = pRealFile->pMethods->xFileSize(pRealFile, &fileSize);
        if (rc == SQLITE_OK && fileSize >= CCVFS_HEADER_SIZE) {
            // Try to load header to determine file type
            rc = ccvfs_load_header(pCcvfsFile);
            if (rc == SQLITE_OK) {
                pCcvfsFile->is_ccvfs_file = 1;
                CCVFS_DEBUG("Opened existing CCVFS file");
                
                // Load block index for existing CCVFS files
                rc = ccvfs_load_block_index(pCcvfsFile);
                if (rc != SQLITE_OK) {
                    CCVFS_ERROR("Failed to load block index: %d", rc);
                    pRealFile->pMethods->xClose(pRealFile);
                    return rc;
                }
            } else {
                // Not a CCVFS file or invalid header
                pCcvfsFile->is_ccvfs_file = 0;
                pCcvfsFile->header_loaded = 0;
                CCVFS_DEBUG("Opened existing regular file");
            }
        } else {
            // File too small or error reading - treat as regular file
            pCcvfsFile->is_ccvfs_file = 0;
            CCVFS_DEBUG("Opened existing regular file (too small for CCVFS)");
        }
    }
    
    CCVFS_DEBUG("Successfully opened file (CCVFS: %s)", 
                pCcvfsFile->is_ccvfs_file ? "yes" : "no");
    return SQLITE_OK;
}

/*
 * Delete file
 */
int ccvfsDelete(sqlite3_vfs *pVfs, const char *zName, int syncDir) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    CCVFS_DEBUG("Deleting file: %s", zName);
    
    return pCcvfs->pRootVfs->xDelete(pCcvfs->pRootVfs, zName, syncDir);
}

/*
 * Check file access
 */
int ccvfsAccess(sqlite3_vfs *pVfs, const char *zName, int flags, int *pResOut) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xAccess(pCcvfs->pRootVfs, zName, flags, pResOut);
}

/*
 * Get full pathname
 */
int ccvfsFullPathname(sqlite3_vfs *pVfs, const char *zName, int nOut, char *zOut) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xFullPathname(pCcvfs->pRootVfs, zName, nOut, zOut);
}

/*
 * Dynamic library loading
 */
void *ccvfsDlOpen(sqlite3_vfs *pVfs, const char *zFilename) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xDlOpen(pCcvfs->pRootVfs, zFilename);
}

void ccvfsDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    pCcvfs->pRootVfs->xDlError(pCcvfs->pRootVfs, nByte, zErrMsg);
}

void *(*ccvfsDlSym(sqlite3_vfs *pVfs, void *pHandle, const char *zSymbol))(void) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xDlSym(pCcvfs->pRootVfs, pHandle, zSymbol);
}

void ccvfsDlClose(sqlite3_vfs *pVfs, void *pHandle) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    pCcvfs->pRootVfs->xDlClose(pCcvfs->pRootVfs, pHandle);
}

/*
 * Other VFS methods
 */
int ccvfsRandomness(sqlite3_vfs *pVfs, int nByte, char *zOut) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xRandomness(pCcvfs->pRootVfs, nByte, zOut);
}

int ccvfsSleep(sqlite3_vfs *pVfs, int microseconds) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xSleep(pCcvfs->pRootVfs, microseconds);
}

int ccvfsCurrentTime(sqlite3_vfs *pVfs, double *pTimeOut) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xCurrentTime(pCcvfs->pRootVfs, pTimeOut);
}

int ccvfsGetLastError(sqlite3_vfs *pVfs, int nBuf, char *zBuf) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xGetLastError(pCcvfs->pRootVfs, nBuf, zBuf);
}

int ccvfsCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTimeOut) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xCurrentTimeInt64(pCcvfs->pRootVfs, pTimeOut);
}

int ccvfsSetSystemCall(sqlite3_vfs *pVfs, const char *zName, sqlite3_syscall_ptr pNewFunc) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xSetSystemCall(pCcvfs->pRootVfs, zName, pNewFunc);
}

sqlite3_syscall_ptr ccvfsGetSystemCall(sqlite3_vfs *pVfs, const char *zName) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xGetSystemCall(pCcvfs->pRootVfs, zName);
}

const char *ccvfsNextSystemCall(sqlite3_vfs *pVfs, const char *zName) {
    CCVFS *pCcvfs = (CCVFS*)pVfs;
    
    return pCcvfs->pRootVfs->xNextSystemCall(pCcvfs->pRootVfs, zName);
}