#include "ccvfs_io.h"
#include "ccvfs_block.h"

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
 * Close file
 */
int ccvfsIoClose(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    int rc = SQLITE_OK;
    
    CCVFS_DEBUG("Closing CCVFS file");
    
    if (p->pReal) {
        rc = p->pReal->pMethods->xClose(p->pReal);
    }
    
    // Free block index
    if (p->pBlockIndex) {
        sqlite3_free(p->pBlockIndex);
        p->pBlockIndex = NULL;
    }
    
    // TODO: Free index manager cache
    
    CCVFS_DEBUG("CCVFS file closed with result: %d", rc);
    return rc;
}

/*
 * Read from file
 */
int ccvfsIoRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    CCVFS *pVfs = p->pOwner;
    int rc;
    
    CCVFS_DEBUG("Reading %d bytes at offset %lld", iAmt, iOfst);
    
    // Load header if not already loaded
    if (!p->header_loaded) {
        rc = ccvfs_load_header(p);
        if (rc != SQLITE_OK) {
            // Not a CCVFS file, read directly
            CCVFS_DEBUG("Not a CCVFS file, reading directly");
            return p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
        }
    }
    
    // If no compression algorithm, read directly
    if (!pVfs || !pVfs->pCompressAlg) {
        CCVFS_DEBUG("No compression algorithm, reading directly");
        return p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
    }
    
    // TODO: Implement full compressed read logic
    // For now, just read directly (placeholder)
    CCVFS_DEBUG("Compressed read not fully implemented, reading directly");
    return p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
}

/*
 * Write to file
 */
int ccvfsIoWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    CCVFS *pVfs = p->pOwner;
    int rc;
    
    CCVFS_DEBUG("Writing %d bytes at offset %lld", iAmt, iOfst);
    
    // Initialize header for new files
    if (!p->header_loaded && iOfst == 0) {
        rc = ccvfs_init_header(p, pVfs);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to initialize CCVFS header");
            return rc;
        }
        
        // Save header first
        rc = ccvfs_save_header(p);
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Failed to save CCVFS header");
            return rc;
        }
    }
    
    // If no compression algorithm, write directly
    if (!pVfs || !pVfs->pCompressAlg) {
        CCVFS_DEBUG("No compression algorithm, writing directly");
        return p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
    }
    
    // TODO: Implement full compressed write logic
    // For now, just write directly (placeholder)
    CCVFS_DEBUG("Compressed write not fully implemented, writing directly");
    return p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
}

/*
 * Truncate file
 */
int ccvfsIoTruncate(sqlite3_file *pFile, sqlite3_int64 size) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Truncating file to %lld bytes", size);
    
    return p->pReal->pMethods->xTruncate(p->pReal, size);
}

/*
 * Sync file to disk
 */
int ccvfsIoSync(sqlite3_file *pFile, int flags) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    CCVFS_DEBUG("Syncing file with flags %d", flags);
    
    return p->pReal->pMethods->xSync(p->pReal, flags);
}

/*
 * Get file size
 */
int ccvfsIoFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    int rc = p->pReal->pMethods->xFileSize(p->pReal, pSize);
    
    CCVFS_DEBUG("File size: %lld bytes", *pSize);
    
    return rc;
}

/*
 * Lock file
 */
int ccvfsIoLock(sqlite3_file *pFile, int eLock) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xLock(p->pReal, eLock);
}

/*
 * Unlock file
 */
int ccvfsIoUnlock(sqlite3_file *pFile, int eLock) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xUnlock(p->pReal, eLock);
}

/*
 * Check reserved lock
 */
int ccvfsIoCheckReservedLock(sqlite3_file *pFile, int *pResOut) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xCheckReservedLock(p->pReal, pResOut);
}

/*
 * File control
 */
int ccvfsIoFileControl(sqlite3_file *pFile, int op, void *pArg) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xFileControl(p->pReal, op, pArg);
}

/*
 * Get sector size
 */
int ccvfsIoSectorSize(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xSectorSize(p->pReal);
}

/*
 * Get device characteristics
 */
int ccvfsIoDeviceCharacteristics(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xDeviceCharacteristics(p->pReal);
}

/*
 * Shared memory map
 */
int ccvfsIoShmMap(sqlite3_file *pFile, int iPg, int pgsz, int bExtend, void volatile **pp) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xShmMap(p->pReal, iPg, pgsz, bExtend, pp);
}

/*
 * Shared memory lock
 */
int ccvfsIoShmLock(sqlite3_file *pFile, int offset, int n, int flags) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xShmLock(p->pReal, offset, n, flags);
}

/*
 * Shared memory barrier
 */
void ccvfsIoShmBarrier(sqlite3_file *pFile) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    p->pReal->pMethods->xShmBarrier(p->pReal);
}

/*
 * Shared memory unmap
 */
int ccvfsIoShmUnmap(sqlite3_file *pFile, int deleteFlag) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xShmUnmap(p->pReal, deleteFlag);
}

/*
 * Fetch
 */
int ccvfsIoFetch(sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xFetch(p->pReal, iOfst, iAmt, pp);
}

/*
 * Unfetch
 */
int ccvfsIoUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    return p->pReal->pMethods->xUnfetch(p->pReal, iOfst, pPage);
}