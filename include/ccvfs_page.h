#ifndef CCVFS_PAGE_H
#define CCVFS_PAGE_H

#include "ccvfs_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Page and header management functions - 页面和文件头管理函数
 */
int ccvfs_load_header(CCVFSFile *pFile);
int ccvfs_save_header(CCVFSFile *pFile);
int ccvfs_load_page_index(CCVFSFile *pFile);
int ccvfs_save_page_index(CCVFSFile *pFile);
int ccvfs_force_save_page_index(CCVFSFile *pFile); /* Force save even if not dirty */
int ccvfs_init_header(CCVFSFile *pFile, CCVFS *pVfs);
int ccvfs_expand_page_index(CCVFSFile *pFile, uint32_t new_page_count);

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_PAGE_H */