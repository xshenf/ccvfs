#ifndef CCVFS_IO_H
#define CCVFS_IO_H

#include "ccvfs_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IO method declarations
 */
int ccvfsIoClose(sqlite3_file *pFile);
int ccvfsIoRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst);
int ccvfsIoWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst);
int ccvfsIoTruncate(sqlite3_file *pFile, sqlite3_int64 size);
int ccvfsIoSync(sqlite3_file *pFile, int flags);
int ccvfsIoFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize);
int ccvfsIoLock(sqlite3_file *pFile, int eLock);
int ccvfsIoUnlock(sqlite3_file *pFile, int eLock);
int ccvfsIoCheckReservedLock(sqlite3_file *pFile, int *pResOut);
int ccvfsIoFileControl(sqlite3_file *pFile, int op, void *pArg);
int ccvfsIoSectorSize(sqlite3_file *pFile);
int ccvfsIoDeviceCharacteristics(sqlite3_file *pFile);
int ccvfsIoShmMap(sqlite3_file *pFile, int iPg, int pgsz, int bExtend, void volatile **pp);
int ccvfsIoShmLock(sqlite3_file *pFile, int offset, int n, int flags);
void ccvfsIoShmBarrier(sqlite3_file *pFile);
int ccvfsIoShmUnmap(sqlite3_file *pFile, int deleteFlag);
int ccvfsIoFetch(sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp);
int ccvfsIoUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage);

/*
 * IO methods table (declared here, defined in ccvfs_io.c)
 */
extern sqlite3_io_methods ccvfsIoMethods;

/*
 * Hole management functions (declared here, defined in ccvfs_io.c)
 */
int ccvfs_init_hole_manager(CCVFSFile *pFile);
void ccvfs_cleanup_hole_manager(CCVFSFile *pFile);
int ccvfs_add_hole(CCVFSFile *pFile, sqlite3_int64 offset, uint32_t size);
int ccvfs_allocate_from_hole(CCVFSFile *pFile, sqlite3_int64 offset, uint32_t allocSize);

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_IO_H */