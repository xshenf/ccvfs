#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/compress_vfs.h"

// 数据块大小定义
#define CCVFS_DEFAULT_BLOCK_SIZE 8192
#define CCVFS_HEADER_SIZE 16
#define CCVFS_LENGTH_FIELD_SIZE 4

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
    CCVFS_DEBUG("压缩数据: 输入长度=%d, 输出缓冲区大小=%d", input_len, output_len);
    
    int i, j = 0;
    int count;
    
    if (output_len < input_len) {
        CCVFS_ERROR("输出缓冲区太小: %d < %d", output_len, input_len);
        return -1; // 输出缓冲区太小
    }
    
    for (i = 0; i < input_len; i++) {
        count = 1;
        while (i + count < input_len && input[i] == input[i + count] && count < 255) {
            count++;
        }
        
        if (count > 1) {
            if (j + 2 > output_len) {
                CCVFS_ERROR("输出缓冲区太小: j=%d, 需要=%d, 可用=%d", j, j+2, output_len);
                return -1; // 输出缓冲区太小
            }
            output[j++] = input[i];
            output[j++] = (unsigned char)count;
            i += count - 1;
            CCVFS_VERBOSE("压缩重复字符: 字符=%u, 次数=%d", input[i], count);
        } else {
            if (j + 1 > output_len) {
                CCVFS_ERROR("输出缓冲区太小: j=%d, 需要=%d, 可用=%d", j, j+1, output_len);
                return -1; // 输出缓冲区太小
            }
            output[j++] = input[i];
            CCVFS_VERBOSE("复制单个字符: 字符=%u", input[i]);
        }
    }
    
    CCVFS_DEBUG("压缩完成: 原始长度=%d, 压缩后长度=%d", input_len, j);
    return j; // 返回压缩后的长度
}

/*
 * 简单的RLE解压缩实现
 */
static int rle_decompress(const unsigned char *input, int input_len, unsigned char *output, int output_len) {
    CCVFS_DEBUG("解压缩数据: 输入长度=%d, 输出缓冲区大小=%d", input_len, output_len);
    
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
                CCVFS_VERBOSE("解压重复字符: 字符=%u, 次数=%d", byte, count);
                continue;
            }
        }
        
        // 单字节数据
        if (j >= output_len) {
            CCVFS_ERROR("输出缓冲区太小: j=%d, 需要=%d", j, j+1);
            return -1; // 输出缓冲区太小
        }
        output[j++] = byte;
        CCVFS_VERBOSE("复制单个字符: 字符=%u", byte);
    }
    
    CCVFS_DEBUG("解压缩完成: 压缩长度=%d, 解压后长度=%d", input_len, j);
    return j; // 返回解压后的长度
}

/*
 * 简单的XOR加密实现
 */
static int xor_encrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    CCVFS_DEBUG("XOR加密数据: 密钥长度=%d, 输入长度=%d, 输出缓冲区大小=%d", key_len, input_len, output_len);
    
    int i;
    
    if (output_len < input_len) {
        CCVFS_ERROR("输出缓冲区太小: %d < %d", output_len, input_len);
        return -1; // 输出缓冲区太小
    }
    
    for (i = 0; i < input_len; i++) {
        output[i] = input[i] ^ key[i % key_len];
        CCVFS_VERBOSE("加密字节: 原始=%u, 密钥=%u, 加密后=%u", input[i], key[i % key_len], output[i]);
    }
    
    CCVFS_DEBUG("XOR加密完成: 处理字节数=%d", input_len);
    return input_len;
}

/*
 * 简单的XOR解密实现（与加密相同）
 */
static int xor_decrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    CCVFS_DEBUG("XOR解密数据: 密钥长度=%d, 输入长度=%d, 输出缓冲区大小=%d", key_len, input_len, output_len);
    
    // XOR解密与加密是相同的操作
    int result = xor_encrypt(key, key_len, input, input_len, output, output_len);
    
    if (result >= 0) {
        CCVFS_DEBUG("XOR解密完成: 处理字节数=%d", result);
    } else {
        CCVFS_ERROR("XOR解密失败");
    }
    
    return result;
}

/*
 * IO方法实现
 */
static int ccvfsIoClose(sqlite3_file *pFile) {
    CCVFS_DEBUG("关闭文件");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xClose(p->pReal);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("文件关闭成功");
    } else {
        CCVFS_ERROR("文件关闭失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFS_DEBUG("读取文件: 长度=%d, 偏移量=%lld", iAmt, iOfst);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    CCVFS *pVfs = p->pOwner;
    
    // 如果没有设置压缩或加密算法，则直接读取
    if (!pVfs->pCompressAlg && !pVfs->pEncryptAlg) {
        CCVFS_DEBUG("无压缩或加密算法，直接读取");
        int result = p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
        if (result == SQLITE_OK) {
            CCVFS_DEBUG("直接读取成功");
        } else {
            CCVFS_ERROR("直接读取失败: %d", result);
        }
        return result;
    }
    
    // 对于现在，我们仍然直接调用底层VFS的读取方法
    // 真正的解压缩和解密功能将在后续实现
    int result = p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("读取成功");
    } else if (result == SQLITE_IOERR_SHORT_READ) {
        CCVFS_DEBUG("部分读取完成");
    } else {
        CCVFS_ERROR("读取失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    CCVFS_DEBUG("写入文件: 长度=%d, 偏移量=%lld", iAmt, iOfst);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    CCVFS *pVfs = p->pOwner;
    
    // 如果没有设置压缩或加密算法，则直接写入
    if (!pVfs->pCompressAlg && !pVfs->pEncryptAlg) {
        CCVFS_DEBUG("无压缩或加密算法，直接写入");
        int result = p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
        if (result == SQLITE_OK) {
            CCVFS_DEBUG("直接写入成功");
        } else {
            CCVFS_ERROR("直接写入失败: %d", result);
        }
        return result;
    }
    
    // 对于现在，我们仍然直接调用底层VFS的写入方法
    // 真正的压缩和加密功能将在后续实现
    int result = p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("写入成功");
    } else {
        CCVFS_ERROR("写入失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoTruncate(sqlite3_file *pFile, sqlite3_int64 size) {
    CCVFS_DEBUG("截断文件: 大小=%lld", size);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xTruncate(p->pReal, size);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("文件截断成功");
    } else {
        CCVFS_ERROR("文件截断失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoSync(sqlite3_file *pFile, int flags) {
    CCVFS_DEBUG("同步文件: 标志=%d", flags);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xSync(p->pReal, flags);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("文件同步成功");
    } else {
        CCVFS_ERROR("文件同步失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize) {
    CCVFS_DEBUG("获取文件大小");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xFileSize(p->pReal, pSize);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("获取文件大小成功: %lld", *pSize);
    } else {
        CCVFS_ERROR("获取文件大小失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoLock(sqlite3_file *pFile, int eLock) {
    CCVFS_DEBUG("锁定文件: 锁类型=%d", eLock);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xLock(p->pReal, eLock);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("文件锁定成功");
    } else {
        CCVFS_ERROR("文件锁定失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoUnlock(sqlite3_file *pFile, int eLock) {
    CCVFS_DEBUG("解锁文件: 锁类型=%d", eLock);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xUnlock(p->pReal, eLock);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("文件解锁成功");
    } else {
        CCVFS_ERROR("文件解锁失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoCheckReservedLock(sqlite3_file *pFile, int *pResOut) {
    CCVFS_DEBUG("检查保留锁");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xCheckReservedLock(p->pReal, pResOut);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("检查保留锁成功: %s", *pResOut ? "已锁定" : "未锁定");
    } else {
        CCVFS_ERROR("检查保留锁失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoFileControl(sqlite3_file *pFile, int op, void *pArg) {
    CCVFS_DEBUG("文件控制操作: 操作码=%d", op);
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xFileControl(p->pReal, op, pArg);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("文件控制操作成功");
    } else {
        CCVFS_ERROR("文件控制操作失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoSectorSize(sqlite3_file *pFile) {
    CCVFS_DEBUG("获取扇区大小");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xSectorSize(p->pReal);
    
    CCVFS_DEBUG("扇区大小: %d", result);
    return result;
}

static int ccvfsIoDeviceCharacteristics(sqlite3_file *pFile) {
    CCVFS_DEBUG("获取设备特性");
    
    CCVFSFile *p = (CCVFSFile *)pFile;
    int result = p->pReal->pMethods->xDeviceCharacteristics(p->pReal);
    
    CCVFS_DEBUG("设备特性: %d", result);
    return result;
}

static int ccvfsIoShmMap(sqlite3_file *pFile, int iPg, int pgsz, int b, void volatile** p) {
    CCVFS_DEBUG("共享内存映射: 页面=%d, 页面大小=%d, 映射=%d", iPg, pgsz, b);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xShmMap(pCFile->pReal, iPg, pgsz, b, p);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("共享内存映射成功");
    } else {
        CCVFS_ERROR("共享内存映射失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoShmLock(sqlite3_file *pFile, int offset, int n, int flags) {
    CCVFS_DEBUG("共享内存锁定: 偏移量=%d, 数量=%d, 标志=%d", offset, n, flags);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xShmLock(pCFile->pReal, offset, n, flags);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("共享内存锁定成功");
    } else {
        CCVFS_ERROR("共享内存锁定失败: %d", result);
    }
    
    return result;
}

static void ccvfsIoShmBarrier(sqlite3_file *pFile) {
    CCVFS_DEBUG("共享内存屏障");
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    pCFile->pReal->pMethods->xShmBarrier(pCFile->pReal);
    
    CCVFS_DEBUG("共享内存屏障完成");
}

static int ccvfsIoShmUnmap(sqlite3_file *pFile, int deleteFlag) {
    CCVFS_DEBUG("共享内存取消映射: 删除标志=%d", deleteFlag);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xShmUnmap(pCFile->pReal, deleteFlag);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("共享内存取消映射成功");
    } else {
        CCVFS_ERROR("共享内存取消映射失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoFetch(sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp) {
    CCVFS_DEBUG("获取数据: 偏移量=%lld, 长度=%d", iOfst, iAmt);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xFetch(pCFile->pReal, iOfst, iAmt, pp);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("获取数据成功");
    } else {
        CCVFS_ERROR("获取数据失败: %d", result);
    }
    
    return result;
}

static int ccvfsIoUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *p) {
    CCVFS_DEBUG("释放数据: 偏移量=%lld", iOfst);
    
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int result = pCFile->pReal->pMethods->xUnfetch(pCFile->pReal, iOfst, p);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("释放数据成功");
    } else {
        CCVFS_ERROR("释放数据失败: %d", result);
    }
    
    return result;
}

/*
 * VFS方法实现
 */
static int ccvfsOpen(sqlite3_vfs *pVfs, sqlite3_filename zName, sqlite3_file *pFile, int flags, int *pOutFlags) {
    CCVFS_DEBUG("打开文件: 名称=%s, 标志=%d", zName ? zName : "(空)", flags);
    
    CCVFS *p = (CCVFS *)pVfs;
    CCVFSFile *pCFile = (CCVFSFile *)pFile;
    int rc;
    
    // 设置IO方法
    pFile->pMethods = &ccvfsIoMethods;
    
    // 分配实际文件结构内存
    pCFile->pReal = (sqlite3_file *)&pCFile[1];
    pCFile->pOwner = p;
    
    CCVFS_DEBUG("调用底层VFS打开文件");
    // 调用底层VFS的open方法
    rc = p->pRootVfs->xOpen(p->pRootVfs, zName, pCFile->pReal, flags, pOutFlags);
    if (rc != SQLITE_OK) {
        pFile->pMethods = 0;
        CCVFS_ERROR("底层VFS打开文件失败: %d", rc);
    } else {
        CCVFS_DEBUG("文件打开成功");
    }
    
    return rc;
}

static int ccvfsDelete(sqlite3_vfs *pVfs, const char *zName, int syncDir) {
    CCVFS_DEBUG("删除文件: 名称=%s, 同步目录=%d", zName, syncDir);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xDelete(p->pRootVfs, zName, syncDir);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("文件删除成功");
    } else {
        CCVFS_ERROR("文件删除失败: %d", result);
    }
    
    return result;
}

static int ccvfsAccess(sqlite3_vfs *pVfs, const char *zName, int flags, int *pResOut) {
    CCVFS_DEBUG("访问检查: 名称=%s, 标志=%d", zName, flags);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xAccess(p->pRootVfs, zName, flags, pResOut);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("访问检查成功: %s", *pResOut ? "存在" : "不存在");
    } else {
        CCVFS_ERROR("访问检查失败: %d", result);
    }
    
    return result;
}

static int ccvfsFullPathname(sqlite3_vfs *pVfs, const char *zName, int nOut, char *zOut) {
    CCVFS_DEBUG("获取完整路径名: 名称=%s, 输出缓冲区大小=%d", zName, nOut);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xFullPathname(p->pRootVfs, zName, nOut, zOut);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("获取完整路径名成功: %s", zOut);
    } else {
        CCVFS_ERROR("获取完整路径名失败: %d", result);
    }
    
    return result;
}

static void *ccvfsDlOpen(sqlite3_vfs *pVfs, const char *zFilename) {
    CCVFS_DEBUG("动态库打开: 文件名=%s", zFilename);
    
    CCVFS *p = (CCVFS *)pVfs;
    void *result = p->pRootVfs->xDlOpen(p->pRootVfs, zFilename);
    
    if (result) {
        CCVFS_DEBUG("动态库打开成功");
    } else {
        CCVFS_ERROR("动态库打开失败");
    }
    
    return result;
}

static void ccvfsDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg) {
    CCVFS_DEBUG("获取动态库错误: 缓冲区大小=%d", nByte);
    
    CCVFS *p = (CCVFS *)pVfs;
    p->pRootVfs->xDlError(p->pRootVfs, nByte, zErrMsg);
    
    CCVFS_DEBUG("动态库错误信息: %s", zErrMsg);
}

static void *(*ccvfsDlSym(sqlite3_vfs *pVfs, void *pH, const char *zSymbol))(void) {
    CCVFS_DEBUG("获取动态库符号: 符号=%s", zSymbol);
    
    CCVFS *p = (CCVFS *)pVfs;
    void *(*result)(void) = p->pRootVfs->xDlSym(p->pRootVfs, pH, zSymbol);
    
    if (result) {
        CCVFS_DEBUG("获取动态库符号成功");
    } else {
        CCVFS_ERROR("获取动态库符号失败");
    }
    
    return result;
}

static void ccvfsDlClose(sqlite3_vfs *pVfs, void *pHandle) {
    CCVFS_DEBUG("关闭动态库");
    
    CCVFS *p = (CCVFS *)pVfs;
    p->pRootVfs->xDlClose(p->pRootVfs, pHandle);
    
    CCVFS_DEBUG("动态库关闭成功");
}

static int ccvfsRandomness(sqlite3_vfs *pVfs, int nByte, char *zOut) {
    CCVFS_DEBUG("获取随机数: 字节数=%d", nByte);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xRandomness(p->pRootVfs, nByte, zOut);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("获取随机数成功");
    } else {
        CCVFS_ERROR("获取随机数失败: %d", result);
    }
    
    return result;
}

static int ccvfsSleep(sqlite3_vfs *pVfs, int microseconds) {
    CCVFS_DEBUG("睡眠: 微秒=%d", microseconds);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xSleep(p->pRootVfs, microseconds);
    
    CCVFS_DEBUG("睡眠完成");
    return result;
}

static int ccvfsCurrentTime(sqlite3_vfs *pVfs, double *pTime) {
    CCVFS_DEBUG("获取当前时间");
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xCurrentTime(p->pRootVfs, pTime);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("获取当前时间成功: %f", *pTime);
    } else {
        CCVFS_ERROR("获取当前时间失败: %d", result);
    }
    
    return result;
}

static int ccvfsGetLastError(sqlite3_vfs *pVfs, int nErr, char *zErr) {
    CCVFS_DEBUG("获取最后错误: 缓冲区大小=%d", nErr);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xGetLastError(p->pRootVfs, nErr, zErr);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("获取最后错误成功: %s", zErr);
    } else {
        CCVFS_ERROR("获取最后错误失败: %d", result);
    }
    
    return result;
}

static int ccvfsCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTime) {
    CCVFS_DEBUG("获取当前时间(64位)");
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xCurrentTimeInt64(p->pRootVfs, pTime);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("获取当前时间成功: %lld", *pTime);
    } else {
        CCVFS_ERROR("获取当前时间失败: %d", result);
    }
    
    return result;
}

static int ccvfsSetSystemCall(sqlite3_vfs *pVfs, const char *zName, sqlite3_syscall_ptr pFunc) {
    CCVFS_DEBUG("设置系统调用: 名称=%s", zName);
    
    CCVFS *p = (CCVFS *)pVfs;
    int result = p->pRootVfs->xSetSystemCall(p->pRootVfs, zName, pFunc);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("设置系统调用成功");
    } else {
        CCVFS_ERROR("设置系统调用失败: %d", result);
    }
    
    return result;
}

static sqlite3_syscall_ptr ccvfsGetSystemCall(sqlite3_vfs *pVfs, const char *zName) {
    CCVFS_DEBUG("获取系统调用: 名称=%s", zName);
    
    CCVFS *p = (CCVFS *)pVfs;
    sqlite3_syscall_ptr result = p->pRootVfs->xGetSystemCall(p->pRootVfs, zName);
    
    if (result) {
        CCVFS_DEBUG("获取系统调用成功");
    } else {
        CCVFS_ERROR("获取系统调用失败");
    }
    
    return result;
}

static const char *ccvfsNextSystemCall(sqlite3_vfs *pVfs, const char *zName) {
    CCVFS_DEBUG("获取下一个系统调用: 当前名称=%s", zName ? zName : "(空)");
    
    CCVFS *p = (CCVFS *)pVfs;
    const char *result = p->pRootVfs->xNextSystemCall(p->pRootVfs, zName);
    
    if (result) {
        CCVFS_DEBUG("获取下一个系统调用成功: %s", result);
    } else {
        CCVFS_DEBUG("没有更多系统调用");
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
    CCVFS_DEBUG("创建CCVFS: 名称=%s, 压缩算法=%s, 加密算法=%s", 
                zVfsName, zCompressType ? zCompressType : "(无)", zEncryptType ? zEncryptType : "(无)");
    
    CCVFS *pNew;
    sqlite3_vfs *pExist;
    
    // 检查VFS是否已存在
    pExist = sqlite3_vfs_find(zVfsName);
    if (pExist) {
        CCVFS_ERROR("VFS已存在: %s", zVfsName);
        return SQLITE_ERROR;
    }
    
    // 如果未指定底层VFS，则使用默认VFS
    if (!pRootVfs) {
        pRootVfs = sqlite3_vfs_find(0);
        CCVFS_DEBUG("使用默认VFS作为底层VFS");
    }
    
    // 分配内存
    pNew = (CCVFS *)sqlite3_malloc(sizeof(CCVFS));
    if (!pNew) {
        CCVFS_ERROR("内存分配失败: %d字节", sizeof(CCVFS));
        return SQLITE_NOMEM;
    }
    memset(pNew, 0, sizeof(CCVFS));
    CCVFS_DEBUG("分配CCVFS结构体内存: %d字节", sizeof(CCVFS));
    
    // 分配压缩类型字符串内存
    if (zCompressType) {
        pNew->zCompressType = sqlite3_mprintf("%s", zCompressType);
        if (!pNew->zCompressType) {
            sqlite3_free(pNew);
            CCVFS_ERROR("压缩类型字符串内存分配失败");
            return SQLITE_NOMEM;
        }
        CCVFS_DEBUG("设置压缩类型: %s", zCompressType);
    }
    
    // 分配加密类型字符串内存
    if (zEncryptType) {
        pNew->zEncryptType = sqlite3_mprintf("%s", zEncryptType);
        if (!pNew->zEncryptType) {
            sqlite3_free(pNew->zCompressType);
            sqlite3_free(pNew);
            CCVFS_ERROR("加密类型字符串内存分配失败");
            return SQLITE_NOMEM;
        }
        CCVFS_DEBUG("设置加密类型: %s", zEncryptType);
    }
    
    // 初始化基础VFS结构
    pNew->base.iVersion = pRootVfs->iVersion > 3 ? 3 : pRootVfs->iVersion;
    pNew->base.szOsFile = sizeof(CCVFSFile) + pRootVfs->szOsFile;
    pNew->base.mxPathname = pRootVfs->mxPathname;
    pNew->base.zName = zVfsName;
    pNew->base.pAppData = 0;
    
    CCVFS_DEBUG("初始化VFS结构: 版本=%d, 文件大小=%d, 最大路径名长度=%d", 
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
        CCVFS_DEBUG("支持VFS版本2特性");
    }
    
    // 版本3的方法
    if (pNew->base.iVersion >= 3) {
        pNew->base.xSetSystemCall = ccvfsSetSystemCall;
        pNew->base.xGetSystemCall = ccvfsGetSystemCall;
        pNew->base.xNextSystemCall = ccvfsNextSystemCall;
        CCVFS_DEBUG("支持VFS版本3特性");
    }
    
    // 保存底层VFS引用
    pNew->pRootVfs = pRootVfs;
    
    CCVFS_DEBUG("注册VFS");
    // 注册VFS
    int result = sqlite3_vfs_register(&pNew->base, 0);
    
    if (result == SQLITE_OK) {
        CCVFS_DEBUG("CCVFS创建成功");
    } else {
        sqlite3_free(pNew->zCompressType);
        sqlite3_free(pNew->zEncryptType);
        sqlite3_free(pNew);
        CCVFS_ERROR("VFS注册失败: %d", result);
    }
    
    return result;
}

/*
 * 销毁CCVFS
 */
int sqlite3_ccvfs_destroy(const char *zVfsName) {
    CCVFS_DEBUG("销毁CCVFS: 名称=%s", zVfsName);
    
    sqlite3_vfs *pVfs = sqlite3_vfs_find(zVfsName);
    CCVFS *p;
    
    if (!pVfs) {
        CCVFS_ERROR("找不到VFS: %s", zVfsName);
        return SQLITE_ERROR;
    }
    
    p = (CCVFS *)pVfs;
    
    CCVFS_DEBUG("注销VFS");
    sqlite3_vfs_unregister(pVfs);
    
    if (p->zCompressType) {
        sqlite3_free(p->zCompressType);
        CCVFS_DEBUG("释放压缩类型字符串内存");
    }
    if (p->zEncryptType) {
        sqlite3_free(p->zEncryptType);
        CCVFS_DEBUG("释放加密类型字符串内存");
    }
    sqlite3_free(p);
    CCVFS_DEBUG("释放CCVFS结构体内存");
    
    CCVFS_DEBUG("CCVFS销毁成功");
    return SQLITE_OK;
}

/*
 * 注册自定义压缩算法
 */
int sqlite3_ccvfs_register_compress_algorithm(CompressAlgorithm *algorithm) {
    CCVFS_DEBUG("注册压缩算法: 名称=%s", algorithm ? algorithm->name : "(空)");
    
    // TODO: 实现算法注册逻辑
    if (!algorithm) {
        CCVFS_ERROR("算法指针为空");
        return SQLITE_ERROR;
    }
    
    if (!algorithm->name) {
        CCVFS_ERROR("算法名称为空");
        return SQLITE_ERROR;
    }
    
    if (!algorithm->compress || !algorithm->decompress) {
        CCVFS_ERROR("算法函数指针为空");
        return SQLITE_ERROR;
    }
    
    CCVFS_DEBUG("压缩算法注册成功");
    return SQLITE_OK;
}

/*
 * 注册自定义加密算法
 */
int sqlite3_ccvfs_register_encrypt_algorithm(EncryptAlgorithm *algorithm) {
    CCVFS_DEBUG("注册加密算法: 名称=%s", algorithm ? algorithm->name : "(空)");
    
    // TODO: 实现算法注册逻辑
    if (!algorithm) {
        CCVFS_ERROR("算法指针为空");
        return SQLITE_ERROR;
    }
    
    if (!algorithm->name) {
        CCVFS_ERROR("算法名称为空");
        return SQLITE_ERROR;
    }
    
    if (!algorithm->encrypt || !algorithm->decrypt) {
        CCVFS_ERROR("算法函数指针为空");
        return SQLITE_ERROR;
    }
    
    CCVFS_DEBUG("加密算法注册成功");
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
        CCVFS_ERROR("激活CCVFS失败: %d", rc);
        return rc;
    }
    
    // 将ccvfs设置为默认VFS
    sqlite3_vfs *ccvfs = sqlite3_vfs_find("ccvfs");
    if (ccvfs) {
        sqlite3_vfs_register(ccvfs, 1);
        isActivated = 1;
        CCVFS_INFO("CCVFS激活成功，已设置为默认VFS");
        return SQLITE_OK;
    } else {
        CCVFS_ERROR("无法找到刚创建的CCVFS");
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
