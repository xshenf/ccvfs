#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/compress_vfs.h"

// 数据块大小定义
#define CCVFS_DEFAULT_BLOCK_SIZE 8192
#define CCVFS_HEADER_SIZE 16
#define CCVFS_LENGTH_FIELD_SIZE 4


// Debug macro definition
#ifdef DEBUG
#define CCVFS_DEBUG(fmt, ...) fprintf(stderr, "[CCVFS DEBUG] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define CCVFS_DEBUG(fmt, ...)
#endif

#ifdef VERBOSE
#define CCVFS_VERBOSE(fmt, ...) fprintf(stderr, "[CCVFS VERBOSE] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define CCVFS_VERBOSE(fmt, ...)
#endif

#define CCVFS_INFO(fmt, ...) fprintf(stdout, "[CCVFS INFO] " fmt "\n", ##__VA_ARGS__)
#define CCVFS_ERROR(fmt, ...) fprintf(stderr, "[CCVFS ERROR] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)


// 数据块头部结构
typedef struct {
    unsigned int magic;          // 魔数标识
    unsigned int block_number;   // 块序列号
    unsigned int checksum;       // 校验和
    unsigned int flags;          // 标志位
} CCVFSBlockHeader;

/*
 * CCVFS结构体
 * 包含基础VFS结构和一些额外信息
 */
typedef struct CCVFS {
    sqlite3_vfs base;           /* 基础VFS结构 */
    sqlite3_vfs *pRootVfs;      /* 底层VFS */
    char *zCompressType;        /* 压缩算法类型 */
    char *zEncryptType;         /* 加密算法类型 */
    CompressAlgorithm *pCompressAlg; /* 压缩算法实现 */
    EncryptAlgorithm *pEncryptAlg;   /* 加密算法实现 */
} CCVFS;

/*
 * CCVFS文件结构体
 */
typedef struct CCVFSFile {
    sqlite3_file base;          /* 基础文件结构 */
    sqlite3_file *pReal;        /* 实际的文件指针 */
    CCVFS *pOwner;              /* 拥有此文件的VFS */
} CCVFSFile;

/*
 * IO方法声明
 */
static int ccvfsIoClose(sqlite3_file*);
static int ccvfsIoRead(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
static int ccvfsIoWrite(sqlite3_file*, const void*, int iAmt, sqlite3_int64 iOfst);
static int ccvfsIoTruncate(sqlite3_file*, sqlite3_int64 size);
static int ccvfsIoSync(sqlite3_file*, int flags);
static int ccvfsIoFileSize(sqlite3_file*, sqlite3_int64 *pSize);
static int ccvfsIoLock(sqlite3_file*, int);
static int ccvfsIoUnlock(sqlite3_file*, int);
static int ccvfsIoCheckReservedLock(sqlite3_file*, int *pResOut);
static int ccvfsIoFileControl(sqlite3_file*, int op, void *pArg);
static int ccvfsIoSectorSize(sqlite3_file*);
static int ccvfsIoDeviceCharacteristics(sqlite3_file*);
static int ccvfsIoShmMap(sqlite3_file*, int iPg, int pgsz, int, void volatile**);
static int ccvfsIoShmLock(sqlite3_file*, int offset, int n, int flags);
static void ccvfsIoShmBarrier(sqlite3_file*);
static int ccvfsIoShmUnmap(sqlite3_file*, int deleteFlag);
static int ccvfsIoFetch(sqlite3_file*, sqlite3_int64 iOfst, int iAmt, void **pp);
static int ccvfsIoUnfetch(sqlite3_file*, sqlite3_int64 iOfst, void *p);

/*
 * IO方法表
 */
static sqlite3_io_methods ccvfsIoMethods = {
    3,                          /* iVersion */
    ccvfsIoClose,               /* xClose */
    ccvfsIoRead,                /* xRead */
    ccvfsIoWrite,               /* xWrite */
    ccvfsIoTruncate,            /* xTruncate */
    ccvfsIoSync,                /* xSync */
    ccvfsIoFileSize,            /* xFileSize */
    ccvfsIoLock,                /* xLock */
    ccvfsIoUnlock,              /* xUnlock */
    ccvfsIoCheckReservedLock,   /* xCheckReservedLock */
    ccvfsIoFileControl,         /* xFileControl */
    ccvfsIoSectorSize,          /* xSectorSize */
    ccvfsIoDeviceCharacteristics, /* xDeviceCharacteristics */
    ccvfsIoShmMap,              /* xShmMap */
    ccvfsIoShmLock,             /* xShmLock */
    ccvfsIoShmBarrier,          /* xShmBarrier */
    ccvfsIoShmUnmap,            /* xShmUnmap */
    ccvfsIoFetch,               /* xFetch */
    ccvfsIoUnfetch              /* xUnfetch */
};

/*
 * VFS方法声明
 */
static int ccvfsOpen(sqlite3_vfs*, sqlite3_filename zName, sqlite3_file*, int flags, int *pOutFlags);
static int ccvfsDelete(sqlite3_vfs*, const char *zName, int syncDir);
static int ccvfsAccess(sqlite3_vfs*, const char *zName, int flags, int *pResOut);
static int ccvfsFullPathname(sqlite3_vfs*, const char *zName, int nOut, char *zOut);
static void *ccvfsDlOpen(sqlite3_vfs*, const char *zFilename);
static void ccvfsDlError(sqlite3_vfs*, int nByte, char *zErrMsg);
static void *(*ccvfsDlSym(sqlite3_vfs*,void*, const char *zSymbol))(void);
static void ccvfsDlClose(sqlite3_vfs*, void*);
static int ccvfsRandomness(sqlite3_vfs*, int nByte, char *zOut);
static int ccvfsSleep(sqlite3_vfs*, int microseconds);
static int ccvfsCurrentTime(sqlite3_vfs*, double*);
static int ccvfsGetLastError(sqlite3_vfs*, int, char *);
static int ccvfsCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64*);
static int ccvfsSetSystemCall(sqlite3_vfs*, const char *zName, sqlite3_syscall_ptr);
static sqlite3_syscall_ptr ccvfsGetSystemCall(sqlite3_vfs*, const char *zName);
static const char *ccvfsNextSystemCall(sqlite3_vfs*, const char *zName);

/*
 * 简单的RLE压缩实现
 */
static int rle_compress(const unsigned char *input, int input_len, unsigned char *output, int output_len) {
    CCVFS_DEBUG("Compressing data: input length=%d, output buffer size=%d", input_len, output_len);
    
    int i, j = 0;
    int count;
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small: %d < %d", output_len, input_len);
        return -1; // 输出缓冲区太小
    }
    
    for (i = 0; i < input_len; i++) {
        count = 1;
        while (i + count < input_len && input[i] == input[i + count] && count < 255) {
            count++;
        }
        
        if (count > 1) {
            if (j + 2 > output_len) {
                CCVFS_ERROR("Output buffer too small: j=%d, needed=%d, available=%d", j, j+2, output_len);
                return -1; // 输出缓冲区太小
            }
            output[j++] = input[i];
            output[j++] = (unsigned char)count;
            i += count - 1;
            CCVFS_VERBOSE("Compress repeated character: character=%u, count=%d", input[i], count);
        } else {
            if (j + 1 > output_len) {
                CCVFS_ERROR("Output buffer too small: j=%d, needed=%d, available=%d", j, j+1, output_len);
                return -1; // 输出缓冲区太小
            }
            output[j++] = input[i];
            CCVFS_VERBOSE("Copy single character: character=%u", input[i]);
        }
    }
    
    CCVFS_DEBUG("Compression completed: original length=%d, compressed length=%d", input_len, j);
    return j; // 返回压缩后的长度
}

/*
 * 简单的RLE解压缩实现
 */
static int rle_decompress(const unsigned char *input, int input_len, unsigned char *output, int output_len) {
    CCVFS_DEBUG("Decompressing data: input length=%d, output buffer size=%d", input_len, output_len);
    
    int i, j = 0;
    
    for (i = 0; i < input_len; i++) {
        unsigned char byte = input[i];
        
        if (i + 1 < input_len && j + input[i+1] <= output_len) {
            // 检查是否是压缩数据（后面跟着计数）
            int count = input[i+1];
            if (count > 1) {
                // 解压重复数据
                int k;
                for (k = 0; k < count && j < output_len; k++) {
                    output[j++] = byte;
                }
                i++; // 跳过计数字段
                CCVFS_VERBOSE("Decompress repeated character: character=%u, count=%d", byte, count);
                continue;
            }
        }
        
        // 单字节数据
        if (j >= output_len) {
            CCVFS_ERROR("Output buffer too small: j=%d, needed=%d", j, j+1);
            return -1; // 输出缓冲区太小
        }
        output[j++] = byte;
        CCVFS_VERBOSE("Copy single character: character=%u", byte);
    }
    
    CCVFS_DEBUG("Decompression completed: compressed length=%d, decompressed length=%d", input_len, j);
    return j; // 返回解压后的长度
}

/*
 * 简单的XOR加密实现
 */
static int xor_encrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    CCVFS_DEBUG("XOR encrypting data: key length=%d, input length=%d, output buffer size=%d", key_len, input_len, output_len);
    
    int i;
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small: %d < %d", output_len, input_len);
        return -1; // 输出缓冲区太小
    }
    
    for (i = 0; i < input_len; i++) {
        output[i] = input[i] ^ key[i % key_len];
        CCVFS_VERBOSE("Encrypt byte: original=%u, key=%u, encrypted=%u", input[i], key[i % key_len], output[i]);
    }
    
    CCVFS_DEBUG("XOR encryption completed: bytes processed=%d", input_len);
    return input_len;
}

/*
 * 简单的XOR解密实现（与加密相同）
 */
static int xor_decrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    CCVFS_DEBUG("XOR decrypting data: key length=%d, input length=%d, output buffer size=%d", key_len, input_len, output_len);
    
    // XOR解密与加密是相同的操作
    int result = xor_encrypt(key, key_len, input, input_len, output, output_len);
    
    if (result >= 0) {
        CCVFS_DEBUG("XOR decryption completed: bytes processed=%d", result);
    } else {
        CCVFS_ERROR("XOR decryption failed");
    }
    
    return result;
}

/*
 * IO方法实现
 */
static int ccvfsIoClose(sqlite3_file *pFile) {
    CCVFS_DEBUG("Closing file");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xClose(p->pReal);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("File closed successfully");
    } else {
        CCVFS_ERROR("Failed to close file: %d", result);
    }
    
    return result;
}

static int ccvfsIoRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFS_DEBUG("Reading file: length=%d, offset=%lld", iAmt, iOfst);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    CCVFS *pVfs = p->pOwner;
    
    // If no compression or encryption algorithm is set, read directly
    if (!pVfs->pCompressAlg && !pVfs->pEncryptAlg) {
        CCVFS_DEBUG("No compression or encryption algorithm, reading directly");
        int result = p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
        if (result == SQLITE_OK) {
            CCVFS_DEBUG("Direct read successful");
        } else {
            CCVFS_ERROR("Direct read failed: %d", result);
        }
        return result;
    }
    
    // For now, we still call the underlying VFS's read method directly
    // The real decompression and decryption functionality will be implemented later
    int result = p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Read successful");
    } else if (result == SQLITE_IOERR_SHORT_READ) {
        CCVFS_DEBUG("Partial read completed");
    } else {
        CCVFS_ERROR("Read failed: %d", result);
    }
    
    return result;
}

static int ccvfsIoWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFS_DEBUG("Writing file: length=%d, offset=%lld", iAmt, iOfst);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    CCVFS *pVfs = p->pOwner;
    
    // If no compression or encryption algorithm is set, write directly
    if (!pVfs->pCompressAlg && !pVfs->pEncryptAlg) {
        CCVFS_DEBUG("No compression or encryption algorithm, writing directly");
        int result = p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
        if (result == SQLITE_OK) {
            CCVFS_DEBUG("Direct write successful");
        } else {
            CCVFS_ERROR("Direct write failed: %d", result);
        }
        return result;
    }
    
    // For now, we still call the underlying VFS's write method directly
    // The real compression and encryption functionality will be implemented later
    int result = p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Write successful");
    } else {
        CCVFS_ERROR("Write failed: %d", result);
    }
    
    return result;
}

static int ccvfsIoTruncate(sqlite3_file *pFile, sqlite3_int64 size) {
    CCVFS_DEBUG("Truncating file: size=%lld", size);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xTruncate(p->pReal, size);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("File truncated successfully");
    } else {
        CCVFS_ERROR("Failed to truncate file: %d", result);
    }
    
    return result;
}

static int ccvfsIoSync(sqlite3_file *pFile, int flags) {
    CCVFS_DEBUG("Synchronizing file: flags=%d", flags);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xSync(p->pReal, flags);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("File synchronized successfully");
    } else {
        CCVFS_ERROR("Failed to synchronize file: %d", result);
    }
    
    return result;
}

static int ccvfsIoFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize) {
    CCVFS_DEBUG("Getting file size");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xFileSize(p->pReal, pSize);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("File size obtained successfully: %lld", *pSize);
    } else {
        CCVFS_ERROR("Failed to get file size: %d", result);
    }
    
    return result;
}

static int ccvfsIoLock(sqlite3_file *pFile, int eLock) {
    CCVFS_DEBUG("Locking file: lock type=%d", eLock);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xLock(p->pReal, eLock);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("File locked successfully");
    } else {
        CCVFS_ERROR("Failed to lock file: %d", result);
    }
    
    return result;
}

static int ccvfsIoUnlock(sqlite3_file *pFile, int eLock) {
    CCVFS_DEBUG("Unlocking file: lock type=%d", eLock);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xUnlock(p->pReal, eLock);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("File unlocked successfully");
    } else {
        CCVFS_ERROR("Failed to unlock file: %d", result);
    }
    
    return result;
}

static int ccvfsIoCheckReservedLock(sqlite3_file *pFile, int *pResOut) {
    CCVFS_DEBUG("Checking reserved lock");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xCheckReservedLock(p->pReal, pResOut);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Reserved lock check successful: %s", *pResOut ? "locked" : "unlocked");
    } else {
        CCVFS_ERROR("Reserved lock check failed: %d", result);
    }
    
    return result;
}

static int ccvfsIoFileControl(sqlite3_file *pFile, int op, void *pArg) {
    CCVFS_DEBUG("File control operation: opcode=%d", op);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xFileControl(p->pReal, op, pArg);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("File control operation successful");
    } else {
        CCVFS_ERROR("File control operation failed: %d", result);
    }
    
    return result;
}

static int ccvfsIoSectorSize(sqlite3_file *pFile) {
    CCVFS_DEBUG("Getting sector size");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xSectorSize(p->pReal);
    
    CCVFS_DEBUG("Sector size: %d", result);
    return result;
}

static int ccvfsIoDeviceCharacteristics(sqlite3_file *pFile) {
    CCVFS_DEBUG("Getting device characteristics");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xDeviceCharacteristics(p->pReal);
    
    CCVFS_DEBUG("Device characteristics: %d", result);
    return result;
}

static int ccvfsIoShmMap(sqlite3_file *pFile, int iPg, int pgsz, int b, void volatile** p) {
    CCVFS_DEBUG("Shared memory mapping: page=%d, page size=%d, map=%d", iPg, pgsz, b);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xShmMap(pCFile->pReal, iPg, pgsz, b, p);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Shared memory mapping successful");
    } else {
        CCVFS_ERROR("Shared memory mapping failed: %d", result);
    }
    
    return result;
}

static int ccvfsIoShmLock(sqlite3_file *pFile, int offset, int n, int flags) {
    CCVFS_DEBUG("Shared memory locking: offset=%d, count=%d, flags=%d", offset, n, flags);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xShmLock(pCFile->pReal, offset, n, flags);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Shared memory locking successful");
    } else {
        CCVFS_ERROR("Shared memory locking failed: %d", result);
    }
    
    return result;
}

static void ccvfsIoShmBarrier(sqlite3_file *pFile) {
    CCVFS_DEBUG("Shared memory barrier");
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    pCFile->pReal->pMethods->xShmBarrier(pCFile->pReal);
    
    CCVFS_DEBUG("Shared memory barrier completed");
}

static int ccvfsIoShmUnmap(sqlite3_file *pFile, int deleteFlag) {
    CCVFS_DEBUG("Shared memory unmapping: delete flag=%d", deleteFlag);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xShmUnmap(pCFile->pReal, deleteFlag);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Shared memory unmapping successful");
    } else {
        CCVFS_ERROR("Shared memory unmapping failed: %d", result);
    }
    
    return result;
}

static int ccvfsIoFetch(sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp) {
    CCVFS_DEBUG("Fetching data: offset=%lld, length=%d", iOfst, iAmt);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xFetch(pCFile->pReal, iOfst, iAmt, pp);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Data fetch successful");
    } else {
        CCVFS_ERROR("Data fetch failed: %d", result);
    }
    
    return result;
}

static int ccvfsIoUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *p) {
    CCVFS_DEBUG("Releasing data: offset=%lld", iOfst);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xUnfetch(pCFile->pReal, iOfst, p);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Data release successful");
    } else {
        CCVFS_ERROR("Data release failed: %d", result);
    }
    
    return result;
}

/*
 * VFS方法实现
 */
static int ccvfsOpen(sqlite3_vfs *pVfs, sqlite3_filename zName, sqlite3_file *pFile, int flags, int *pOutFlags) {
    CCVFS_DEBUG("Opening file: name=%s, flags=%d", zName ? zName : "(null)", flags);
    
    CCVFS *p = (CCVFS *)pVfs;
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int rc;
    
    // 设置IO方法
    pFile->pMethods = &ccvfsIoMethods;
    
    // 分配实际文件结构内存
    pCFile->pReal = (sqlite3_file *)&pCFile[1];
    pCFile->pOwner = p;
    
    CCVFS_DEBUG("Calling underlying VFS to open file");
    // 调用底层VFS的open方法
    rc = p->pRootVfs->xOpen(p->pRootVfs, zName, pCFile->pReal, flags, pOutFlags);
    if (rc != SQLITE_OK) {
        pFile->pMethods = 0;
        CCVFS_ERROR("Underlying VFS failed to open file: %d", rc);
    } else {
        CCVFS_DEBUG("File opened successfully");
    }
    
    return rc;
}

static int ccvfsDelete(sqlite3_vfs *pVfs, const char *zName, int syncDir) {
    CCVFS_DEBUG("Deleting file: name=%s, sync directory=%d", zName, syncDir);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xDelete(p->pRootVfs, zName, syncDir);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("File deleted successfully");
    } else {
        CCVFS_ERROR("Failed to delete file: %d", result);
    }
    
    return result;
}

static int ccvfsAccess(sqlite3_vfs *pVfs, const char *zName, int flags, int *pResOut) {
    CCVFS_DEBUG("Access check: name=%s, flags=%d", zName, flags);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xAccess(p->pRootVfs, zName, flags, pResOut);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Access check successful: %s", *pResOut ? "exists" : "does not exist");
    } else {
        CCVFS_ERROR("Access check failed: %d", result);
    }
    
    return result;
}

static int ccvfsFullPathname(sqlite3_vfs *pVfs, const char *zName, int nOut, char *zOut) {
    CCVFS_DEBUG("Getting full pathname: name=%s, output buffer size=%d", zName, nOut);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xFullPathname(p->pRootVfs, zName, nOut, zOut);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Full pathname obtained successfully: %s", zOut);
    } else {
        CCVFS_ERROR("Failed to get full pathname: %d", result);
    }
    
    return result;
}

static void *ccvfsDlOpen(sqlite3_vfs *pVfs, const char *zFilename) {
    CCVFS_DEBUG("Opening dynamic library: filename=%s", zFilename);
    
    CCVFS *p = (CCVFS *)pVfs;
    void *result = p->pRootVfs->xDlOpen(p->pRootVfs, zFilename);
    
    if (result) {
        CCVFS_DEBUG("Dynamic library opened successfully");
    } else {
        CCVFS_ERROR("Failed to open dynamic library");
    }
    
    return result;
}

static void ccvfsDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg) {
    CCVFS_DEBUG("Getting dynamic library error: buffer size=%d", nByte);
    
    CCVFS *p = (CCVFS *)pVfs;
    p->pRootVfs->xDlError(p->pRootVfs, nByte, zErrMsg);
    
    CCVFS_DEBUG("Dynamic library error message: %s", zErrMsg);
}

static void *(*ccvfsDlSym(sqlite3_vfs *pVfs, void *pH, const char *zSymbol))(void) {
    CCVFS_DEBUG("Getting dynamic library symbol: symbol=%s", zSymbol);
    
    CCVFS *p = (CCVFS *)pVfs;
    void *(*result)(void) = p->pRootVfs->xDlSym(p->pRootVfs, pH, zSymbol);
    
    if (result) {
        CCVFS_DEBUG("Dynamic library symbol obtained successfully");
    } else {
        CCVFS_ERROR("Failed to get dynamic library symbol");
    }
    
    return result;
}

static void ccvfsDlClose(sqlite3_vfs *pVfs, void *pHandle) {
    CCVFS_DEBUG("Closing dynamic library");
    
    CCVFS *p = (CCVFS *)pVfs;
    p->pRootVfs->xDlClose(p->pRootVfs, pHandle);
    
    CCVFS_DEBUG("Dynamic library closed successfully");
}

static int ccvfsRandomness(sqlite3_vfs *pVfs, int nByte, char *zOut) {
    CCVFS_DEBUG("Getting randomness: byte count=%d", nByte);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xRandomness(p->pRootVfs, nByte, zOut);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Randomness obtained successfully");
    } else {
        CCVFS_ERROR("Failed to get randomness: %d", result);
    }
    
    return result;
}

static int ccvfsSleep(sqlite3_vfs *pVfs, int microseconds) {
    CCVFS_DEBUG("Sleeping: microseconds=%d", microseconds);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xSleep(p->pRootVfs, microseconds);
    
    CCVFS_DEBUG("Sleep completed");
    return result;
}

static int ccvfsCurrentTime(sqlite3_vfs *pVfs, double *pTime) {
    CCVFS_DEBUG("Getting current time");
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xCurrentTime(p->pRootVfs, pTime);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Current time obtained successfully: %f", *pTime);
    } else {
        CCVFS_ERROR("Failed to get current time: %d", result);
    }
    
    return result;
}

static int ccvfsGetLastError(sqlite3_vfs *pVfs, int nErr, char *zErr) {
    CCVFS_DEBUG("Getting last error: buffer size=%d", nErr);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xGetLastError(p->pRootVfs, nErr, zErr);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Last error obtained successfully: %s", zErr);
    } else {
        CCVFS_ERROR("Failed to get last error: %d", result);
    }
    
    return result;
}

static int ccvfsCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTime) {
    CCVFS_DEBUG("Getting current time (64-bit)");
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xCurrentTimeInt64(p->pRootVfs, pTime);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("Current time obtained successfully: %lld", *pTime);
    } else {
        CCVFS_ERROR("Failed to get current time: %d", result);
    }
    
    return result;
}

static int ccvfsSetSystemCall(sqlite3_vfs *pVfs, const char *zName, sqlite3_syscall_ptr pFunc) {
    CCVFS_DEBUG("Setting system call: name=%s", zName);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xSetSystemCall(p->pRootVfs, zName, pFunc);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("System call set successfully");
    } else {
        CCVFS_ERROR("Failed to set system call: %d", result);
    }
    
    return result;
}

static sqlite3_syscall_ptr ccvfsGetSystemCall(sqlite3_vfs *pVfs, const char *zName) {
    CCVFS_DEBUG("Getting system call: name=%s", zName);
    
    CCVFS *p = (CCVFS *)pVfs;
    sqlite3_syscall_ptr result = p->pRootVfs->xGetSystemCall(p->pRootVfs, zName);
    
    if (result) {
        CCVFS_DEBUG("System call obtained successfully");
    } else {
        CCVFS_ERROR("Failed to get system call");
    }
    
    return result;
}

static const char *ccvfsNextSystemCall(sqlite3_vfs *pVfs, const char *zName) {
    CCVFS_DEBUG("Getting next system call: current name=%s", zName ? zName : "(null)");
    
    CCVFS *p = (CCVFS *)pVfs;
    const char *result = p->pRootVfs->xNextSystemCall(p->pRootVfs, zName);
    
    if (result) {
        CCVFS_DEBUG("Next system call obtained successfully: %s", result);
    } else {
        CCVFS_DEBUG("No more system calls");
    }
    
    return result;
}

/*
 * 创建CCVFS
 */
int sqlite3_ccvfs_create(
    const char *zVfsName,
    sqlite3_vfs *pRootVfs,
    const char *zCompressType,
    const char *zEncryptType
) {
    CCVFS_DEBUG("Creating CCVFS: name=%s, compression algorithm=%s, encryption algorithm=%s", 
                zVfsName, zCompressType ? zCompressType : "(none)", zEncryptType ? zEncryptType : "(none)");
    
    CCVFS *pNew;
    sqlite3_vfs *pExist;
    
    // 检查VFS是否已存在
    pExist = sqlite3_vfs_find(zVfsName);
    if (pExist) {
        CCVFS_ERROR("VFS already exists: %s", zVfsName);
        return SQLITE_ERROR;
    }
    
    // 如果未指定底层VFS，则使用默认VFS
    if (!pRootVfs) {
        pRootVfs = sqlite3_vfs_find(0);
        CCVFS_DEBUG("Using default VFS as underlying VFS");
    }
    
    // 分配内存
    pNew = (CCVFS *)sqlite3_malloc(sizeof(CCVFS));
    if (!pNew) {
        CCVFS_ERROR("Memory allocation failed: %d bytes", sizeof(CCVFS));
        return SQLITE_NOMEM;
    }
    memset(pNew, 0, sizeof(CCVFS));
    CCVFS_DEBUG("Allocated CCVFS structure memory: %d bytes", sizeof(CCVFS));
    
    // 分配压缩类型字符串内存
    if (zCompressType) {
        pNew->zCompressType = sqlite3_mprintf("%s", zCompressType);
        if (!pNew->zCompressType) {
            sqlite3_free(pNew);
            CCVFS_ERROR("Failed to allocate memory for compression type string");
            return SQLITE_NOMEM;
        }
        CCVFS_DEBUG("Set compression type: %s", zCompressType);
    }
    
    // 分配加密类型字符串内存
    if (zEncryptType) {
        pNew->zEncryptType = sqlite3_mprintf("%s", zEncryptType);
        if (!pNew->zEncryptType) {
            sqlite3_free(pNew->zCompressType);
            sqlite3_free(pNew);
            CCVFS_ERROR("Failed to allocate memory for encryption type string");
            return SQLITE_NOMEM;
        }
        CCVFS_DEBUG("Set encryption type: %s", zEncryptType);
    }
    
    // 初始化基础VFS结构
    pNew->base.iVersion = pRootVfs->iVersion > 3 ? 3 : pRootVfs->iVersion;
    pNew->base.szOsFile = sizeof(CCVFSFile) + pRootVfs->szOsFile;
    pNew->base.mxPathname = pRootVfs->mxPathname;
    pNew->base.zName = zVfsName;
    pNew->base.pAppData = 0;
    
    CCVFS_DEBUG("Initializing VFS structure: version=%d, file size=%d, max pathname length=%d", 
                pNew->base.iVersion, pNew->base.szOsFile, pNew->base.mxPathname);
    
    // 设置VFS方法
    pNew->base.xOpen = ccvfsOpen;
    pNew->base.xDelete = ccvfsDelete;
    pNew->base.xAccess = ccvfsAccess;
    pNew->base.xFullPathname = ccvfsFullPathname;
    pNew->base.xDlOpen = ccvfsDlOpen;
    pNew->base.xDlError = ccvfsDlError;
    pNew->base.xDlSym = (void (*(*)(sqlite3_vfs*,void*, const char *zSymbol))(void))ccvfsDlSym;
    pNew->base.xDlClose = ccvfsDlClose;
    pNew->base.xRandomness = ccvfsRandomness;
    pNew->base.xSleep = ccvfsSleep;
    pNew->base.xCurrentTime = ccvfsCurrentTime;
    pNew->base.xGetLastError = ccvfsGetLastError;
    
    // 版本2的方法
    if (pNew->base.iVersion >= 2) {
        pNew->base.xCurrentTimeInt64 = ccvfsCurrentTimeInt64;
        CCVFS_DEBUG("Supporting VFS version 2 features");
    }
    
    // 版本3的方法
    if (pNew->base.iVersion >= 3) {
        pNew->base.xSetSystemCall = ccvfsSetSystemCall;
        pNew->base.xGetSystemCall = ccvfsGetSystemCall;
        pNew->base.xNextSystemCall = ccvfsNextSystemCall;
        CCVFS_DEBUG("Supporting VFS version 3 features");
    }
    
    // 保存底层VFS引用
    pNew->pRootVfs = pRootVfs;
    
    CCVFS_DEBUG("Registering VFS");
    // 注册VFS
    int result = sqlite3_vfs_register(&pNew->base, 0);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("CCVFS created successfully");
    } else {
        sqlite3_free(pNew->zCompressType);
        sqlite3_free(pNew->zEncryptType);
        sqlite3_free(pNew);
        CCVFS_ERROR("Failed to register VFS: %d", result);
    }
    
    return result;
}

/*
 * 销毁CCVFS
 */
int sqlite3_ccvfs_destroy(const char *zVfsName) {
    CCVFS_DEBUG("Destroying CCVFS: name=%s", zVfsName);
    
    sqlite3_vfs *pVfs = sqlite3_vfs_find(zVfsName);
    CCVFS *p;
    
    if (!pVfs) {
        CCVFS_ERROR("VFS not found: %s", zVfsName);
        return SQLITE_ERROR;
    }
    
    p = (CCVFS *)pVfs;
    
    CCVFS_DEBUG("Unregistering VFS");
    sqlite3_vfs_unregister(pVfs);
    
    if (p->zCompressType) {
        sqlite3_free(p->zCompressType);
        CCVFS_DEBUG("Freed compression type string memory");
    }
    if (p->zEncryptType) {
        sqlite3_free(p->zEncryptType);
        CCVFS_DEBUG("Freed encryption type string memory");
    }
    sqlite3_free(p);
    CCVFS_DEBUG("Freed CCVFS structure memory");
    
    CCVFS_DEBUG("CCVFS destroyed successfully");
    return SQLITE_OK;
}

/*
 * 注册自定义压缩算法
 */
int sqlite3_ccvfs_register_compress_algorithm(CompressAlgorithm *algorithm) {
    CCVFS_DEBUG("Registering compression algorithm: name=%s", algorithm ? algorithm->name : "(null)");
    
    // TODO: 实现算法注册逻辑
    if (!algorithm) {
        CCVFS_ERROR("Algorithm pointer is null");
        return SQLITE_ERROR;
    }
    
    if (!algorithm->name) {
        CCVFS_ERROR("Algorithm name is null");
        return SQLITE_ERROR;
    }
    
    if (!algorithm->compress || !algorithm->decompress) {
        CCVFS_ERROR("Algorithm function pointers are null");
        return SQLITE_ERROR;
    }
    
    CCVFS_DEBUG("Compression algorithm registered successfully");
    return SQLITE_OK;
}

/*
 * 注册自定义加密算法
 */
int sqlite3_ccvfs_register_encrypt_algorithm(EncryptAlgorithm *algorithm) {
    CCVFS_DEBUG("Registering encryption algorithm: name=%s", algorithm ? algorithm->name : "(null)");
    
    // TODO: 实现算法注册逻辑
    if (!algorithm) {
        CCVFS_ERROR("Algorithm pointer is null");
        return SQLITE_ERROR;
    }
    
    if (!algorithm->name) {
        CCVFS_ERROR("Algorithm name is null");
        return SQLITE_ERROR;
    }
    
    if (!algorithm->encrypt || !algorithm->decrypt) {
        CCVFS_ERROR("Algorithm function pointers are null");
        return SQLITE_ERROR;
    }
    
    CCVFS_DEBUG("Encryption algorithm registered successfully");
    return SQLITE_OK;
}

/*
 * 激活压缩加密VFS，类似于sqlite3_activate_cerod
 */
int sqlite3_activate_ccvfs(const char *zCompressType, const char *zEncryptType) {
    static int isActivated = 0;
    
    // 防止重复激活
    if (isActivated) {
        return SQLITE_OK;
    }
    
    // 创建并注册VFS
    int rc = sqlite3_ccvfs_create("ccvfs", NULL, zCompressType, zEncryptType);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to activate CCVFS: %d", rc);
        return rc;
    }
    
    // 将ccvfs设置为默认VFS
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

/*
 * 实现sqlite3_activate_cerod函数，用于通过PRAGMA激活CCVFS
 */
int sqlite3_activate_cerod(const char *zParms) {
    // 解析参数，格式为"compress_type:encrypt_type"或仅"compress_type"
    char *zCompressType = NULL;
    char *zEncryptType = NULL;
    char *zCopy = NULL;
    int rc = SQLITE_OK;
    
    if (zParms) {
        zCopy = sqlite3_mprintf("%s", zParms);
        if (!zCopy) {
            return SQLITE_NOMEM;
        }
        
        // 查找分隔符 ':'
        char *colon = strchr(zCopy, ':');
        if (colon) {
            *colon = '\0';
            zCompressType = zCopy;
            zEncryptType = colon + 1;
        } else {
            zCompressType = zCopy;
            zEncryptType = NULL;
        }
    }
    
    // 激活CCVFS
    rc = sqlite3_activate_ccvfs(zCompressType, zEncryptType);
    
    if (zCopy) {
        sqlite3_free(zCopy);
    }
    
    return rc;
}