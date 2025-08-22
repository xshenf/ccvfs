/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains code to implement a shell that can be used to 
** access SQLite databases with compress_vfs support.
*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../include/ccvfs.h"
#include "../sqlite3/sqlite3.h"

/* 
** 处理 PRAGMA activate_extensions 语句
*/
static void processActivateExtensionsPragma(const char *zRight);

/*
** 在shell初始化时注册我们的处理函数
*/
void shell_cx_init(void);

/*
** 显示当前VFS状态的辅助函数
*/
void sqlite3_ccvfs_show_status(void);

/*
** 激活压缩加密VFS的函数声明
** 实际实现在compress_vfs.c中
*/
int sqlite3_activate_ccvfs(const char *zCompressType, const char *zEncryptType);

/* 
** 处理 PRAGMA activate_extensions 语句
*/
static void processActivateExtensionsPragma(const char *zRight) {
  printf("DEBUG: processActivateExtensionsPragma called with: %s\n", zRight ? zRight : "NULL");
  if (zRight && sqlite3_strnicmp(zRight, "ccvfs-", 6) == 0) {
    printf("DEBUG: Calling sqlite3_activate_cerod with: %s\n", &zRight[6]);
    sqlite3_activate_cerod(&zRight[6]);
  }
}

/*
** 在shell初始化时注册我们的处理函数
** 这个函数会被自动调用来初始化CCVFS支持
*/
void shell_cx_init(void) {
    printf("CCVFS Shell Extension Initialized\n");
    
    /* 默认激活CCVFS，这样用户就可以直接使用 */
    printf("Auto-activating CCVFS with default settings...\n");
    sqlite3_activate_cerod("zlib");
}

/*
** 显示当前VFS状态的辅助函数
*/
void sqlite3_ccvfs_show_status(void) {
    sqlite3_vfs *vfs;
    int count = 0;
    
    printf("\n=== CCVFS Status ===\n");
    
    /* 显示默认VFS */
    vfs = sqlite3_vfs_find(NULL);
    if (vfs) {
        printf("Default VFS: %s\n", vfs->zName);
    } else {
        printf("No default VFS found\n");
    }
    
    /* 列出所有注册的VFS */
    printf("Registered VFS list:\n");
    for (vfs = sqlite3_vfs_find(NULL); vfs; vfs = vfs->pNext) {
        printf("  %d. %s\n", ++count, vfs->zName);
    }
    
    /* 检查CCVFS是否存在 */
    vfs = sqlite3_vfs_find("ccvfs");
    if (vfs) {
        printf("CCVFS Status: Active\n");
    } else {
        printf("CCVFS Status: Not active\n");
    }
    
    printf("==================\n\n");
}

/*
** 将16进制字符串解析为二进制数据的辅助函数
** 输入: hexStr - 16进制字符串 (例如: "48656c6c6f")
** 输出: output - 输出缓冲区
** 返回: 解析的字节数，错误时返回-1
*/
static int hex_string_to_bytes(const char *hexStr, unsigned char *output, int maxBytes) {
    if (!hexStr || !output) {
        return -1;
    }
    
    int hexLen = (int)strlen(hexStr);
    if (hexLen % 2 != 0) {
        printf("CCVFS: Invalid hex string length (must be even)\n");
        return -1;
    }
    
    int byteLen = hexLen / 2;
    if (byteLen > maxBytes) {
        printf("CCVFS: Hex string too long (max %d bytes)\n", maxBytes);
        return -1;
    }
    
    for (int i = 0; i < byteLen; i++) {
        char hex[3] = {hexStr[i*2], hexStr[i*2+1], '\0'};
        char *endptr;
        unsigned long val = strtoul(hex, &endptr, 16);
        
        if (*endptr != '\0' || val > 255) {
            printf("CCVFS: Invalid hex character at position %d\n", i*2);
            return -1;
        }
        
        output[i] = (unsigned char)val;
    }
    
    return byteLen;
}

/*
** 实现sqlite3_activate_cerod函数，用于通过PRAGMA激活CCVFS
** 这是SQLite shell调用的标准激活函数
** zParms: 16进制字符串格式的密码 (例如: "48656c6c6f576f726c64")
*/
void sqlite3_activate_cerod(const char *zParms) {
    char *zCompressType = "zlib";  /* 默认使用zlib压缩 */
    char *zEncryptType = "xor";    /* 默认使用xor加密 */
    int rc = SQLITE_OK;
    static int activation_count = 0;
    unsigned char keyBytes[32];    /* 支持最大32字节密钥 */
    int keyLen;

    activation_count++;

    printf("activate\n");
    
    /* 解析16进制密码参数 */
    if (!zParms || strlen(zParms) == 0) {
        printf("CCVFS: No password provided, using default key\n");
        /* 使用默认密钥 */
        strcpy((char*)keyBytes, "default_key_123");
        keyLen = 16;
    } else {
        printf("CCVFS: Parsing hex password: %s\n", zParms);
        
        /* 将16进制字符串解析为字节数组 */
        keyLen = hex_string_to_bytes(zParms, keyBytes, sizeof(keyBytes));
        if (keyLen < 0) {
            printf("CCVFS: Failed to parse hex password\n");
            return;
        }
        
        if (keyLen < 16) {
            printf("CCVFS: Warning: Key length (%d) is less than recommended 16 bytes\n", keyLen);
        }
        
        printf("CCVFS: Parsed %d bytes from hex string\n", keyLen);
    }
    
    /* 设置全局加密密钥 */
    ccvfs_set_encryption_key(keyBytes, keyLen);
    
    /* 激活CCVFS */
    rc = sqlite3_activate_ccvfs(zCompressType, zEncryptType);
    if (rc == SQLITE_OK) {
        printf("CCVFS: Successfully activated with compression=%s, encryption=%s (attempt #%d)\n", 
               zCompressType ? zCompressType : "none",
               zEncryptType ? zEncryptType : "none",
               activation_count);
    } else {
        printf("CCVFS: Activation failed with error code %d\n", rc);
        /* 提供更详细的错误信息 */
        switch (rc) {
            case SQLITE_NOMEM:
                printf("CCVFS: Out of memory\n");
                break;
            case SQLITE_ERROR:
                printf("CCVFS: Invalid algorithm or configuration\n");
                break;
            default:
                printf("CCVFS: Unknown error\n");
                break;
        }
    }
    
    /* 清除栈上的密钥副本以提高安全性 */
    memset(keyBytes, 0, sizeof(keyBytes));
}

