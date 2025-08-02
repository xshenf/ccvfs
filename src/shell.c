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
#include "../include/compress_vfs.h"
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
** 实现sqlite3_activate_cerod函数，用于通过PRAGMA激活CCVFS
** 这是SQLite shell调用的标准激活函数
*/
void sqlite3_activate_cerod(const char *zParms) {
    char *zCompressType = NULL;
    char *zEncryptType = NULL;
    char *zCopy = NULL;
    int rc = SQLITE_OK;
    static int activation_count = 0;

    activation_count++;

    printf("activate\n");
    
    /* 如果没有参数，使用默认配置 */
    if (!zParms || strlen(zParms) == 0) {
        zCompressType = "zlib";
        zEncryptType = NULL;
        printf("CCVFS: Using default compression (zlib), no encryption\n");
    } else {
        zCopy = sqlite3_mprintf("%s", zParms);
        if (!zCopy) {
            printf("CCVFS: Memory allocation failed\n");
            return;
        }
        
        /* 解析参数，支持多种格式：
         * - "compress_type" (仅压缩)
         * - "compress_type:encrypt_type" (压缩+加密)
         * - ":encrypt_type" (仅加密)
         * - "none" (禁用压缩和加密)
         */
        if (strcmp(zCopy, "none") == 0) {
            zCompressType = NULL;
            zEncryptType = NULL;
            printf("CCVFS: Disabled compression and encryption\n");
        } else {
            char *colon = strchr(zCopy, ':');
            if (colon) {
                *colon = '\0';
                if (strlen(zCopy) > 0) {
                    zCompressType = zCopy;
                }
                if (strlen(colon + 1) > 0) {
                    zEncryptType = colon + 1;
                }
            } else {
                zCompressType = zCopy;
                zEncryptType = NULL;
            }
        }
        
        printf("CCVFS: Activating with compression=%s, encryption=%s\n", 
               zCompressType ? zCompressType : "none",
               zEncryptType ? zEncryptType : "none");
    }
    
    /* 激活CCVFS */
    rc = sqlite3_activate_ccvfs(zCompressType, zEncryptType);
    if (rc == SQLITE_OK) {
        printf("CCVFS: Successfully activated (attempt #%d)\n", activation_count);
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
    
    if (zCopy) {
        sqlite3_free(zCopy);
    }
}

