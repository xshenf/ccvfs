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
static void processActivateExtensionsPragma(const char *zRight) {
  if (zRight && sqlite3_strnicmp(zRight, "ccvfs-", 6) == 0) {
    sqlite3_activate_cerod(&zRight[6]);
  }
}

/*
** 在shell初始化时注册我们的处理函数
*/
void shell_cx_init(void) {
  /* 可以在这里添加任何需要的初始化代码 */
}

/*
** 激活压缩加密VFS，类似于sqlite3_activate_cerod
*/
int sqlite3_activate_ccvfs(const char *zCompressType, const char *zEncryptType) {
    static int isActivated = 0;
    
    /* 防止重复激活 */
    if (isActivated) {
        return SQLITE_OK;
    }
    
    /* 创建并注册VFS */
    int rc = sqlite3_ccvfs_create("ccvfs", NULL, zCompressType, zEncryptType, CCVFS_CREATE_REALTIME);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    /* 将ccvfs设置为默认VFS */
    sqlite3_vfs *ccvfs = sqlite3_vfs_find("ccvfs");
    if (ccvfs) {
        sqlite3_vfs_register(ccvfs, 1);
        isActivated = 1;
        return SQLITE_OK;
    } else {
        return SQLITE_ERROR;
    }
}

/*
** 实现sqlite3_activate_cerod函数，用于通过PRAGMA激活CCVFS
*/
void sqlite3_activate_cerod(const char *zParms) {
    /* 解析参数，格式为"compress_type:encrypt_type"或仅"compress_type" */
    char *zCompressType = NULL;
    char *zEncryptType = NULL;
    char *zCopy = NULL;
    int rc = SQLITE_OK;

    printf("sqlite3_activate_cerod: %s\n", zParms);
    
    if (zParms) {
        zCopy = sqlite3_mprintf("%s", zParms);
        if (!zCopy) {
            return;
        }
        
        /* 查找分隔符 ':' */
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
    
    /* 激活CCVFS */
    rc = sqlite3_activate_ccvfs(zCompressType, zEncryptType);
    if (rc != SQLITE_OK) {
        printf("Failed to activate CCVFS: %s\n", sqlite3_errmsg(NULL));
    }
    
    if (zCopy) {
        sqlite3_free(zCopy);
    }
}

