#include "ccvfs_utils.h"

/*
 * CRC32 checksum calculation using Ethernet polynomial
 */
uint32_t ccvfs_crc32(const unsigned char *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    int i, j;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CCVFS_CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}

/*
 * Get space utilization statistics
 */
int ccvfs_get_space_stats(sqlite3_file *pFile, CCVFSSpaceStats *pStats) {
    CCVFSFile *p = (CCVFSFile *)pFile;
    
    if (!p || !pStats) {
        return SQLITE_MISUSE;
    }
    
    if (!p->is_ccvfs_file) {
        return SQLITE_NOTFOUND; // Not a CCVFS file
    }
    
    // Copy basic stats
    pStats->total_allocated_space = p->total_allocated_space;
    pStats->total_used_space = p->total_used_space;
    pStats->fragmentation_score = p->fragmentation_score;
    pStats->space_reuse_count = p->space_reuse_count;
    pStats->space_expansion_count = p->space_expansion_count;
    pStats->new_allocation_count = p->new_allocation_count;
    pStats->hole_reclaim_count = p->hole_reclaim_count;
    pStats->best_fit_count = p->best_fit_count;
    pStats->sequential_write_count = p->sequential_write_count;
    
    // Calculate derived metrics
    if (p->total_allocated_space > 0) {
        pStats->space_efficiency_ratio = (double)p->total_used_space / (double)p->total_allocated_space;
    } else {
        pStats->space_efficiency_ratio = 1.0;
    }
    
    uint32_t totalOperations = p->space_reuse_count + p->space_expansion_count + p->new_allocation_count;
    if (totalOperations > 0) {
        pStats->reuse_efficiency_ratio = (double)p->space_reuse_count / (double)totalOperations;
        pStats->hole_reclaim_ratio = (double)p->hole_reclaim_count / (double)totalOperations;
    } else {
        pStats->reuse_efficiency_ratio = 0.0;
        pStats->hole_reclaim_ratio = 0.0;
    }
    
    return SQLITE_OK;
}