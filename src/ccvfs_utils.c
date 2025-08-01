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