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

/*
 * Space utilization statistics
 */
typedef struct {
    uint64_t total_allocated_space;
    uint64_t total_used_space;
    uint32_t fragmentation_score;
    uint32_t space_reuse_count;
    uint32_t space_expansion_count;
    uint32_t new_allocation_count;
    double space_efficiency_ratio;
    double reuse_efficiency_ratio;
} CCVFSSpaceStats;

int ccvfs_get_space_stats(sqlite3_file *pFile, CCVFSSpaceStats *pStats);

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_UTILS_H */