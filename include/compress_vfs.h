#ifndef COMPRESS_VFS_H
#define COMPRESS_VFS_H

#include "sqlite3.h"
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constants
#define CCVFS_MAGIC "CCVFSDB\0"
#define CCVFS_BLOCK_MAGIC 0x42434356  // "BCCV"
#define CCVFS_VERSION_MAJOR 1
#define CCVFS_VERSION_MINOR 0
#define CCVFS_HEADER_SIZE 128
#define CCVFS_DEFAULT_BLOCK_SIZE (64 * 1024)  // 64KB blocks
#define CCVFS_MAX_ALGORITHM_NAME 12

// File layout constants
#define CCVFS_MAX_BLOCKS 65536  // Maximum blocks supported (2^16)
#define CCVFS_INDEX_TABLE_SIZE (CCVFS_MAX_BLOCKS * 24)  // 24 bytes per block index
#define CCVFS_INDEX_TABLE_OFFSET CCVFS_HEADER_SIZE  // Fixed position after header
#define CCVFS_DATA_BLOCKS_OFFSET (CCVFS_INDEX_TABLE_OFFSET + CCVFS_INDEX_TABLE_SIZE)  // Start of data blocks

// Block flags
#define CCVFS_BLOCK_COMPRESSED   (1 << 0)
#define CCVFS_BLOCK_ENCRYPTED    (1 << 1) 
#define CCVFS_BLOCK_SPARSE       (1 << 2)
#define CCVFS_COMPRESSION_LEVEL_MASK (0xFF << 8)
#define CCVFS_COMPRESSION_LEVEL_SHIFT 8

// Creation flags
#define CCVFS_CREATE_REALTIME    (1 << 0)
#define CCVFS_CREATE_OFFLINE     (1 << 1)
#define CCVFS_CREATE_HYBRID      (1 << 2)

/*
 * File header structure (128 bytes)
 */
typedef struct {
    // Basic identification (16 bytes)
    char magic[8];              // "CCVFSDB\0"
    uint16_t major_version;     // Major version (current: 1)
    uint16_t minor_version;     // Minor version (current: 0)
    uint32_t header_size;       // Header size (128)
    
    // SQLite compatibility info (16 bytes)
    uint32_t original_page_size;    // Original SQLite page size
    uint32_t sqlite_version;        // Original SQLite version
    uint32_t database_size_pages;   // Total database pages
    uint32_t reserved1;
    
    // Compression configuration (24 bytes)
    char compress_algorithm[CCVFS_MAX_ALGORITHM_NAME];    // Compression algorithm name
    char encrypt_algorithm[CCVFS_MAX_ALGORITHM_NAME];     // Encryption algorithm name
    
    // Block configuration (16 bytes)
    uint32_t block_size;           // Logical block size
    uint32_t total_blocks;         // Total number of blocks
    uint64_t index_table_offset;   // Block index table offset
    
    // Statistics (24 bytes)
    uint64_t original_file_size;   // Original file size
    uint64_t compressed_file_size; // Compressed file size
    uint32_t compression_ratio;    // Compression ratio (percentage)
    uint32_t creation_flags;       // Creation flags
    
    // Checksum and security (16 bytes)
    uint32_t header_checksum;      // File header checksum
    uint32_t master_key_hash;      // Master key hash (optional)
    uint64_t timestamp;            // Creation timestamp
    
    // Reserved fields (16 bytes)
    uint8_t reserved[16];          // Reserved for extension
} CCVFSFileHeader;

/*
 * Block index entry
 */
typedef struct {
    uint64_t physical_offset;      // Physical file offset
    uint32_t compressed_size;      // Compressed size
    uint32_t original_size;        // Original size
    uint32_t checksum;             // Block checksum
    uint32_t flags;                // Block flags
} CCVFSBlockIndex;

/*
 * Data block structure
 */
typedef struct {
    // Block header (32 bytes)
    uint32_t magic;                // Block magic: 0x42434356 ("BCCV")
    uint32_t block_number;         // Logical block number
    uint32_t original_size;        // Original data size
    uint32_t compressed_size;      // Compressed data size
    uint32_t checksum;             // Data checksum (CRC32)
    uint32_t flags;                // Block flags
    uint64_t timestamp;            // Block update timestamp
    uint64_t sequence_number;      // Sequence number for transaction consistency
    
    // Variable length data follows
    // uint8_t data[];             // Compressed data
} CCVFSDataBlock;

/*
 * Compression algorithm interface
 */
typedef struct {
    const char *name;
    int (*compress)(const unsigned char *input, int input_len, 
                   unsigned char *output, int output_len, int level);
    int (*decompress)(const unsigned char *input, int input_len,
                     unsigned char *output, int output_len);
    int (*get_max_compressed_size)(int input_len);
} CompressAlgorithm;

/*
 * Encryption algorithm interface
 */
typedef struct {
    const char *name;
    int (*encrypt)(const unsigned char *key, int key_len,
                  const unsigned char *input, int input_len,
                  unsigned char *output, int output_len);
    int (*decrypt)(const unsigned char *key, int key_len,
                  const unsigned char *input, int input_len,
                  unsigned char *output, int output_len);
    int key_size;               // Required key size
} EncryptAlgorithm;

/*
 * Register compression and encryption VFS module
 * Parameters:
 *   zVfsName - Name of the new VFS
 *   pRootVfs - Underlying VFS (usually the default VFS)
 *   zCompressType - Compression algorithm type
 *   zEncryptType - Encryption algorithm type
 *   flags - Creation flags (CCVFS_CREATE_*)
 * Return value:
 *   SQLITE_OK - Success
 *   Other values - Error code
 */
int sqlite3_ccvfs_create(
    const char *zVfsName,
    sqlite3_vfs *pRootVfs,
    const char *zCompressType,
    const char *zEncryptType,
    uint32_t flags
);

/*
 * Destroy compression and encryption VFS module
 */
int sqlite3_ccvfs_destroy(const char *zVfsName);

/*
 * Register custom compression algorithm
 */
int sqlite3_ccvfs_register_compress_algorithm(CompressAlgorithm *algorithm);

/*
 * Register custom encryption algorithm
 */
int sqlite3_ccvfs_register_encrypt_algorithm(EncryptAlgorithm *algorithm);

/*
 * Activate compression and encryption VFS, similar to sqlite3_activate_cerod
 */
int sqlite3_activate_ccvfs(const char *zCompressType, const char *zEncryptType);

/*
 * Compress an existing SQLite database (offline compression)
 */
int sqlite3_ccvfs_compress_database(
    const char *source_db,
    const char *compressed_db,
    const char *compress_algorithm,
    const char *encrypt_algorithm,
    int compression_level
);

/*
 * Decompress a compressed database to standard SQLite format
 */
int sqlite3_ccvfs_decompress_database(
    const char *compressed_db,
    const char *output_db
);

/*
 * Get compression statistics for a compressed database
 */
typedef struct {
    uint64_t original_size;
    uint64_t compressed_size;
    uint32_t compression_ratio;
    uint32_t total_blocks;
    char compress_algorithm[CCVFS_MAX_ALGORITHM_NAME];
    char encrypt_algorithm[CCVFS_MAX_ALGORITHM_NAME];
} CCVFSStats;

int sqlite3_ccvfs_get_stats(const char *compressed_db, CCVFSStats *stats);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* COMPRESS_VFS_H */