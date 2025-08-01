#ifndef CCVFS_CORE_H
#define CCVFS_CORE_H

#include "ccvfs_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * VFS method declarations
 */
int ccvfsOpen(sqlite3_vfs *pVfs, sqlite3_filename zName, sqlite3_file *pFile, int flags, int *pOutFlags);
int ccvfsDelete(sqlite3_vfs *pVfs, const char *zName, int syncDir);
int ccvfsAccess(sqlite3_vfs *pVfs, const char *zName, int flags, int *pResOut);
int ccvfsFullPathname(sqlite3_vfs *pVfs, const char *zName, int nOut, char *zOut);
void *ccvfsDlOpen(sqlite3_vfs *pVfs, const char *zFilename);
void ccvfsDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg);
void *(*ccvfsDlSym(sqlite3_vfs *pVfs, void *pHandle, const char *zSymbol))(void);
void ccvfsDlClose(sqlite3_vfs *pVfs, void *pHandle);
int ccvfsRandomness(sqlite3_vfs *pVfs, int nByte, char *zOut);
int ccvfsSleep(sqlite3_vfs *pVfs, int microseconds);
int ccvfsCurrentTime(sqlite3_vfs *pVfs, double *pTimeOut);
int ccvfsGetLastError(sqlite3_vfs *pVfs, int nBuf, char *zBuf);
int ccvfsCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTimeOut);
int ccvfsSetSystemCall(sqlite3_vfs *pVfs, const char *zName, sqlite3_syscall_ptr pNewFunc);
sqlite3_syscall_ptr ccvfsGetSystemCall(sqlite3_vfs *pVfs, const char *zName);
const char *ccvfsNextSystemCall(sqlite3_vfs *pVfs, const char *zName);

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_CORE_H */