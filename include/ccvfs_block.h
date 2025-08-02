#ifndef CCVFS_BLOCK_H
#define CCVFS_BLOCK_H

#include "ccvfs_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Block and header management functions
 */
int ccvfs_load_header(CCVFSFile *pFile);
int ccvfs_save_header(CCVFSFile *pFile);
int ccvfs_load_block_index(CCVFSFile *pFile);
int ccvfs_save_block_index(CCVFSFile *pFile);
int ccvfs_force_save_block_index(CCVFSFile *pFile); /* Force save even if not dirty */
int ccvfs_init_header(CCVFSFile *pFile, CCVFS *pVfs);
int ccvfs_expand_block_index(CCVFSFile *pFile, uint32_t new_block_count);

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_BLOCK_H */