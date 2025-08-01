#ifndef CCVFS_UTILS_H
#define CCVFS_UTILS_H

#include "ccvfs_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Utility functions
 */
uint32_t ccvfs_crc32(const unsigned char *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_UTILS_H */