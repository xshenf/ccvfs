#ifndef COMPRESS_VFS_H
#define COMPRESS_VFS_H

#include "sqlite3.h"
#include <stdio.h>

// 数据块大小定义
#define CCVFS_DEFAULT_BLOCK_SIZE 8192
#define CCVFS_HEADER_SIZE 16
#define CCVFS_LENGTH_FIELD_SIZE 4

// 调试宏定义
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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 压缩算法接口
 */
typedef struct {
    const char *name;
    int (*compress)(const unsigned char *input, int input_len, 
                   unsigned char *output, int output_len);
    int (*decompress)(const unsigned char *input, int input_len,
                     unsigned char *output, int output_len);
} CompressAlgorithm;

/*
 * 加密算法接口
 */
typedef struct {
    const char *name;
    int (*encrypt)(const unsigned char *key, int key_len,
                  const unsigned char *input, int input_len,
                  unsigned char *output, int output_len);
    int (*decrypt)(const unsigned char *key, int key_len,
                  const unsigned char *input, int input_len,
                  unsigned char *output, int output_len);
} EncryptAlgorithm;

/*
 * 注册压缩加密VFS模块
 * 参数:
 *   zVfsName - 新VFS的名称
 *   pRootVfs - 底层VFS（通常是默认VFS）
 *   zCompressType - 压缩算法类型
 *   zEncryptType - 加密算法类型
 * 返回值:
 *   SQLITE_OK - 成功
 *   其他值 - 错误代码
 */
int sqlite3_ccvfs_create(
    const char *zVfsName,
    sqlite3_vfs *pRootVfs,
    const char *zCompressType,
    const char *zEncryptType
);

/*
 * 销毁压缩加密VFS模块
 */
int sqlite3_ccvfs_destroy(const char *zVfsName);

/*
 * 注册自定义压缩算法
 */
int sqlite3_ccvfs_register_compress_algorithm(CompressAlgorithm *algorithm);

/*
 * 注册自定义加密算法
 */
int sqlite3_ccvfs_register_encrypt_algorithm(EncryptAlgorithm *algorithm);

/*
 * 激活压缩加密VFS，类似于sqlite3_activate_cerod
 */
int sqlite3_activate_ccvfs(const char *zCompressType, const char *zEncryptType);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* COMPRESS_VFS_H */