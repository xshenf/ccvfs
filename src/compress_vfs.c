#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "../include/compress_vfs.h"

// Debug macro definitions
#ifdef DEBUG
#define CCVFS_DEBUG(fmt, ...) fprintf(stderr, "[CCVFS DEBUG] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define CCVFS_DEBUG(fmt, ...)
#endif

#ifdef VERBOSE
#define CCVFS_VERBOSE(fmt, ...) fprintf(stderr, "[CCVFS VERBOSE] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define CCVFS_VERBOSE(fmt, ...)
#endif

#define CCVFS_INFO(fmt, ...) fprintf(stdout, "[CCVFS INFO] " fmt "\n", ##__VA_ARGS__)
#define CCVFS_ERROR(fmt, ...) fprintf(stderr, "[CCVFS ERROR] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

// Internal constants
#define CCVFS_MAX_ALGORITHMS 16
#define CCVFS_CRC32_POLYNOMIAL 0xEDB88320

// Algorithm registry
static CompressAlgorithm *g_compress_algorithms[CCVFS_MAX_ALGORITHMS];
static EncryptAlgorithm *g_encrypt_algorithms[CCVFS_MAX_ALGORITHMS];
static int g_compress_algorithm_count = 0;
static int g_encrypt_algorithm_count = 0;
static int g_algorithms_initialized = 0;

/*
 * CCVFS structure
 */
typedef struct CCVFS {
    sqlite3_vfs base;           /* Base VFS structure */
    sqlite3_vfs *pRootVfs;      /* Underlying VFS */
    char *zCompressType;        /* Compression algorithm type */
    char *zEncryptType;         /* Encryption algorithm type */
    CompressAlgorithm *pCompressAlg; /* Compression algorithm implementation */
    EncryptAlgorithm *pEncryptAlg;   /* Encryption algorithm implementation */
    uint32_t creation_flags;    /* Creation flags */
} CCVFS;

/*
 * CCVFS file structure
 */
typedef struct CCVFSFile {
    sqlite3_file base;          /* Base file structure */
    sqlite3_file *pReal;        /* Actual file pointer */
    CCVFS *pOwner;              /* Owner VFS */
    CCVFSFileHeader header;     /* Cached file header */
    CCVFSBlockIndex *pBlockIndex; /* Block index table */
    int header_loaded;          /* Header loaded flag */
} CCVFSFile;

// Forward declarations
static int ccvfsIoClose(sqlite3_file*);
static int ccvfsIoRead(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
static int ccvfsIoWrite(sqlite3_file*, const void*, int iAmt, sqlite3_int64 iOfst);
static int ccvfsIoTruncate(sqlite3_file*, sqlite3_int64 size);
static int ccvfsIoSync(sqlite3_file*, int flags);
static int ccvfsIoFileSize(sqlite3_file*, sqlite3_int64 *pSize);
static int ccvfsIoLock(sqlite3_file*, int);
static int ccvfsIoUnlock(sqlite3_file*, int);
static int ccvfsIoCheckReservedLock(sqlite3_file*, int *pResOut);
static int ccvfsIoFileControl(sqlite3_file*, int op, void *pArg);
static int ccvfsIoSectorSize(sqlite3_file*);
static int ccvfsIoDeviceCharacteristics(sqlite3_file*);
static int ccvfsIoShmMap(sqlite3_file*, int iPg, int pgsz, int, void volatile**);
static int ccvfsIoShmLock(sqlite3_file*, int offset, int n, int flags);
static void ccvfsIoShmBarrier(sqlite3_file*);
static int ccvfsIoShmUnmap(sqlite3_file*, int deleteFlag);
static int ccvfsIoFetch(sqlite3_file*, sqlite3_int64 iOfst, int iAmt, void **pp);
static int ccvfsIoUnfetch(sqlite3_file*, sqlite3_int64 iOfst, void *p);

// VFS method declarations
static int ccvfsOpen(sqlite3_vfs*, sqlite3_filename zName, sqlite3_file*, int flags, int *pOutFlags);
static int ccvfsDelete(sqlite3_vfs*, const char *zName, int syncDir);
static int ccvfsAccess(sqlite3_vfs*, const char *zName, int flags, int *pResOut);
static int ccvfsFullPathname(sqlite3_vfs*, const char *zName, int nOut, char *zOut);
static void *ccvfsDlOpen(sqlite3_vfs*, const char *zFilename);
static void ccvfsDlError(sqlite3_vfs*, int nByte, char *zErrMsg);
static void *(*ccvfsDlSym(sqlite3_vfs*,void*, const char *zSymbol))(void);
static void ccvfsDlClose(sqlite3_vfs*, void*);
static int ccvfsRandomness(sqlite3_vfs*, int nByte, char *zOut);
static int ccvfsSleep(sqlite3_vfs*, int microseconds);
static int ccvfsCurrentTime(sqlite3_vfs*, double*);
static int ccvfsGetLastError(sqlite3_vfs*, int, char *);
static int ccvfsCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64*);
static int ccvfsSetSystemCall(sqlite3_vfs*, const char *zName, sqlite3_syscall_ptr);
static sqlite3_syscall_ptr ccvfsGetSystemCall(sqlite3_vfs*, const char *zName);
static const char *ccvfsNextSystemCall(sqlite3_vfs*, const char *zName);

// Utility functions
static uint32_t ccvfs_crc32(const unsigned char *data, int len);
static int ccvfs_load_header(CCVFSFile *pFile);
static int ccvfs_save_header(CCVFSFile *pFile);
static int ccvfs_load_block_index(CCVFSFile *pFile);
static CompressAlgorithm* ccvfs_find_compress_algorithm(const char *name);
static EncryptAlgorithm* ccvfs_find_encrypt_algorithm(const char *name);
static void ccvfs_init_builtin_algorithms(void);

/*
 * IO methods table
 */
static sqlite3_io_methods ccvfsIoMethods = {
    3,                          /* iVersion */
    ccvfsIoClose,               /* xClose */
    ccvfsIoRead,                /* xRead */
    ccvfsIoWrite,               /* xWrite */
    ccvfsIoTruncate,            /* xTruncate */
    ccvfsIoSync,                /* xSync */
    ccvfsIoFileSize,            /* xFileSize */
    ccvfsIoLock,                /* xLock */
    ccvfsIoUnlock,              /* xUnlock */
    ccvfsIoCheckReservedLock,   /* xCheckReservedLock */
    ccvfsIoFileControl,         /* xFileControl */
    ccvfsIoSectorSize,          /* xSectorSize */
    ccvfsIoDeviceCharacteristics, /* xDeviceCharacteristics */
    ccvfsIoShmMap,              /* xShmMap */
    ccvfsIoShmLock,             /* xShmLock */
    ccvfsIoShmBarrier,          /* xShmBarrier */
    ccvfsIoShmUnmap,            /* xShmUnmap */
    ccvfsIoFetch,               /* xFetch */
    ccvfsIoUnfetch              /* xUnfetch */
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/*
 * Calculate CRC32 checksum
 */
static uint32_t ccvfs_crc32(const unsigned char *data, int len) {
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
 * Find compression algorithm by name
 */
static CompressAlgorithm* ccvfs_find_compress_algorithm(const char *name) {
    int i;
    
    if (!name) return NULL;
    
    for (i = 0; i < g_compress_algorithm_count; i++) {
        if (g_compress_algorithms[i] && 
            strcmp(g_compress_algorithms[i]->name, name) == 0) {
            return g_compress_algorithms[i];
        }
    }
    
    return NULL;
}

/*
 * Find encryption algorithm by name
 */
static EncryptAlgorithm* ccvfs_find_encrypt_algorithm(const char *name) {
    int i;
    
    if (!name) return NULL;
    
    for (i = 0; i < g_encrypt_algorithm_count; i++) {
        if (g_encrypt_algorithms[i] && 
            strcmp(g_encrypt_algorithms[i]->name, name) == 0) {
            return g_encrypt_algorithms[i];
        }
    }
    
    return NULL;
}

// ============================================================================
// BUILTIN ALGORITHMS
// ============================================================================

/*
 * RLE Compression Algorithm (Improved)
 */
static int rle_compress(const unsigned char *input, int input_len, 
                       unsigned char *output, int output_len, int level) {
    int i = 0, j = 0;
    int count;
    
    CCVFS_DEBUG("RLE compressing %d bytes", input_len);
    
    if (output_len < input_len + input_len/2) {
        CCVFS_ERROR("Output buffer too small for RLE compression");
        return -1;
    }
    
    while (i < input_len) {
        unsigned char byte = input[i];
        count = 1;
        
        // Count consecutive identical bytes
        while (i + count < input_len && input[i + count] == byte && count < 255) {
            count++;
        }
        
        if (count >= 3 || (count == 2 && byte == 0)) {
            // Use RLE encoding for runs of 3+ or runs of 2 zeros
            if (j + 3 > output_len) return -1;
            output[j++] = 0xFF;  // RLE marker
            output[j++] = byte;  // Repeated byte
            output[j++] = count; // Count
        } else {
            // Copy literal bytes
            if (byte == 0xFF) {
                // Escape the RLE marker
                if (j + 2 > output_len) return -1;
                output[j++] = 0xFF;
                output[j++] = 0x00;  // Escaped marker
            } else {
                if (j + 1 > output_len) return -1;
                output[j++] = byte;
            }
            count = 1;
        }
        
        i += count;
    }
    
    CCVFS_DEBUG("RLE compressed %d bytes to %d bytes", input_len, j);
    return j;
}

static int rle_decompress(const unsigned char *input, int input_len,
                         unsigned char *output, int output_len) {
    int i = 0, j = 0;
    
    CCVFS_DEBUG("RLE decompressing %d bytes", input_len);
    
    while (i < input_len && j < output_len) {
        if (input[i] == 0xFF) {
            if (i + 1 >= input_len) break;
            
            if (input[i + 1] == 0x00) {
                // Escaped marker
                output[j++] = 0xFF;
                i += 2;
            } else if (i + 2 < input_len) {
                // RLE sequence
                unsigned char byte = input[i + 1];
                int count = input[i + 2];
                
                if (j + count > output_len) {
                    CCVFS_ERROR("Output buffer overflow in RLE decompression");
                    return -1;
                }
                
                while (count-- > 0 && j < output_len) {
                    output[j++] = byte;
                }
                i += 3;
            } else {
                break;
            }
        } else {
            output[j++] = input[i++];
        }
    }
    
    CCVFS_DEBUG("RLE decompressed %d bytes to %d bytes", input_len, j);
    return j;
}

static int rle_get_max_compressed_size(int input_len) {
    // Worst case: every byte needs to be escaped
    return input_len * 2 + 16;
}

/*
 * XOR Encryption Algorithm (Simple)
 */
static int xor_encrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    int i;
    
    if (output_len < input_len) {
        CCVFS_ERROR("Output buffer too small for XOR encryption");
        return -1;
    }
    
    if (key_len == 0) {
        memcpy(output, input, input_len);
        return input_len;
    }
    
    for (i = 0; i < input_len; i++) {
        output[i] = input[i] ^ key[i % key_len];
    }
    
    return input_len;
}

static int xor_decrypt(const unsigned char *key, int key_len,
                      const unsigned char *input, int input_len,
                      unsigned char *output, int output_len) {
    // XOR encryption is symmetric
    return xor_encrypt(key, key_len, input, input_len, output, output_len);
}

// Builtin algorithm instances
static CompressAlgorithm rle_algorithm = {
    "rle",
    rle_compress,
    rle_decompress,
    rle_get_max_compressed_size
};

static EncryptAlgorithm xor_algorithm = {
    "xor",
    xor_encrypt,
    xor_decrypt,
    16  // Default key size
};

/*
 * Initialize builtin algorithms
 */
static void ccvfs_init_builtin_algorithms(void) {
    if (g_algorithms_initialized) return;
    
    // Register builtin compression algorithms
    sqlite3_ccvfs_register_compress_algorithm(&rle_algorithm);
    
    // Register builtin encryption algorithms
    sqlite3_ccvfs_register_encrypt_algorithm(&xor_algorithm);
    
    g_algorithms_initialized = 1;
    
    CCVFS_INFO("Builtin algorithms initialized");
}

// ============================================================================
// ALGORITHM REGISTRATION API
// ============================================================================

/*
 * Register compression algorithm
 */
int sqlite3_ccvfs_register_compress_algorithm(CompressAlgorithm *algorithm) {
    int i;
    
    if (!algorithm || !algorithm->name || !algorithm->compress || !algorithm->decompress) {
        CCVFS_ERROR("Invalid compression algorithm");
        return SQLITE_MISUSE;
    }
    
    // Check if algorithm already exists
    for (i = 0; i < g_compress_algorithm_count; i++) {
        if (g_compress_algorithms[i] && 
            strcmp(g_compress_algorithms[i]->name, algorithm->name) == 0) {
            // Replace existing algorithm
            g_compress_algorithms[i] = algorithm;
            CCVFS_INFO("Replaced compression algorithm: %s", algorithm->name);
            return SQLITE_OK;
        }
    }
    
    // Add new algorithm
    if (g_compress_algorithm_count >= CCVFS_MAX_ALGORITHMS) {
        CCVFS_ERROR("Too many compression algorithms registered");
        return SQLITE_FULL;
    }
    
    g_compress_algorithms[g_compress_algorithm_count++] = algorithm;
    CCVFS_INFO("Registered compression algorithm: %s", algorithm->name);
    
    return SQLITE_OK;
}

/*
 * Register encryption algorithm
 */
int sqlite3_ccvfs_register_encrypt_algorithm(EncryptAlgorithm *algorithm) {
    int i;
    
    if (!algorithm || !algorithm->name || !algorithm->encrypt || !algorithm->decrypt) {
        CCVFS_ERROR("Invalid encryption algorithm");
        return SQLITE_MISUSE;
    }
    
    // Check if algorithm already exists
    for (i = 0; i < g_encrypt_algorithm_count; i++) {
        if (g_encrypt_algorithms[i] && 
            strcmp(g_encrypt_algorithms[i]->name, algorithm->name) == 0) {
            // Replace existing algorithm
            g_encrypt_algorithms[i] = algorithm;
            CCVFS_INFO("Replaced encryption algorithm: %s", algorithm->name);
            return SQLITE_OK;
        }
    }
    
    // Add new algorithm
    if (g_encrypt_algorithm_count >= CCVFS_MAX_ALGORITHMS) {
        CCVFS_ERROR("Too many encryption algorithms registered");
        return SQLITE_FULL;
    }
    
    g_encrypt_algorithms[g_encrypt_algorithm_count++] = algorithm;
    CCVFS_INFO("Registered encryption algorithm: %s", algorithm->name);
    
    return SQLITE_OK;
}

// ============================================================================
// HEADER AND INDEX MANAGEMENT
// ============================================================================

/*
 * Load file header from disk
 */
static int ccvfs_load_header(CCVFSFile *pFile) {
    int rc;
    sqlite3_int64 fileSize;
    
    if (pFile->header_loaded) {
        return SQLITE_OK;
    }
    
    // Check if file exists and has enough data for header
    rc = pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to get file size");
        return rc;
    }
    
    if (fileSize < CCVFS_HEADER_SIZE) {
        CCVFS_DEBUG("File too small for CCVFS header, treating as new file");
        return SQLITE_IOERR_READ;
    }
    
    // Read header from beginning of file
    rc = pFile->pReal->pMethods->xRead(pFile->pReal, &pFile->header, 
                                       CCVFS_HEADER_SIZE, 0);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read file header");
        return rc;
    }
    
    // Verify magic number
    if (memcmp(pFile->header.magic, CCVFS_MAGIC, 8) != 0) {
        CCVFS_DEBUG("Invalid magic number, not a CCVFS file");
        return SQLITE_IOERR_READ;
    }
    
    // Verify version
    if (pFile->header.major_version != CCVFS_VERSION_MAJOR) {
        CCVFS_ERROR("Unsupported CCVFS version: %d.%d", 
                    pFile->header.major_version, pFile->header.minor_version);
        return SQLITE_IOERR_READ;
    }
    
    // Verify header checksum
    uint32_t calculated_checksum = ccvfs_crc32((const unsigned char*)&pFile->header,
                                               CCVFS_HEADER_SIZE - sizeof(uint32_t));
    if (pFile->header.header_checksum != calculated_checksum) {
        CCVFS_ERROR("Header checksum mismatch");
        return SQLITE_IOERR_READ;
    }
    
    pFile->header_loaded = 1;
    
    CCVFS_DEBUG("Loaded CCVFS header: version %d.%d, %d blocks, compression: %s, encryption: %s",
                pFile->header.major_version, pFile->header.minor_version,
                pFile->header.total_blocks, pFile->header.compress_algorithm,
                pFile->header.encrypt_algorithm);
    
    return SQLITE_OK;
}

/*
 * Save file header to disk
 */
static int ccvfs_save_header(CCVFSFile *pFile) {
    int rc;
    
    // Calculate header checksum
    pFile->header.header_checksum = ccvfs_crc32((const unsigned char*)&pFile->header,
                                                CCVFS_HEADER_SIZE - sizeof(uint32_t));
    
    // Write header to beginning of file
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, &pFile->header,
                                        CCVFS_HEADER_SIZE, 0);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write file header");
        return rc;
    }
    
    pFile->header_loaded = 1;
    
    CCVFS_DEBUG("Saved CCVFS header");
    return SQLITE_OK;
}

/*
 * Load block index table from disk
 */
static int ccvfs_load_block_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    if (!pFile->header_loaded) {
        rc = ccvfs_load_header(pFile);
        if (rc != SQLITE_OK) {
            return rc;
        }
    }
    
    if (pFile->header.total_blocks == 0) {
        CCVFS_DEBUG("No blocks in file");
        return SQLITE_OK;
    }
    
    // Allocate memory for block index
    index_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
    pFile->pBlockIndex = (CCVFSBlockIndex*)sqlite3_malloc(index_size);
    if (!pFile->pBlockIndex) {
        CCVFS_ERROR("Failed to allocate memory for block index");
        return SQLITE_NOMEM;
    }
    
    // Read block index from file
    rc = pFile->pReal->pMethods->xRead(pFile->pReal, pFile->pBlockIndex,
                                       index_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to read block index");
        sqlite3_free(pFile->pBlockIndex);
        pFile->pBlockIndex = NULL;
        return rc;
    }
    
    CCVFS_DEBUG("Loaded block index: %d blocks", pFile->header.total_blocks);
    return SQLITE_OK;
}

/*
 * Save block index table to disk
 */
static int ccvfs_save_block_index(CCVFSFile *pFile) {
    int rc;
    size_t index_size;
    
    if (!pFile->pBlockIndex || pFile->header.total_blocks == 0) {
        return SQLITE_OK;
    }
    
    index_size = pFile->header.total_blocks * sizeof(CCVFSBlockIndex);
    
    // Write block index to file
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, pFile->pBlockIndex,
                                        index_size, pFile->header.index_table_offset);
    if (rc != SQLITE_OK) {
        CCVFS_ERROR("Failed to write block index");
        return rc;
    }
    
    CCVFS_DEBUG("Saved block index: %d blocks", pFile->header.total_blocks);
    return SQLITE_OK;
}

/*
 * Initialize new CCVFS file header
 */
static int ccvfs_init_header(CCVFSFile *pFile, CCVFS *pVfs) {
    memset(&pFile->header, 0, sizeof(CCVFSFileHeader));
    
    // Basic identification
    memcpy(pFile->header.magic, CCVFS_MAGIC, 8);
    pFile->header.major_version = CCVFS_VERSION_MAJOR;
    pFile->header.minor_version = CCVFS_VERSION_MINOR;
    pFile->header.header_size = CCVFS_HEADER_SIZE;
    
    // SQLite compatibility (will be filled when first SQLite page is written)
    pFile->header.original_page_size = 4096;  // Default SQLite page size
    pFile->header.sqlite_version = sqlite3_libversion_number();
    pFile->header.database_size_pages = 0;
    
    // Compression configuration
    if (pVfs->zCompressType) {
        strncpy(pFile->header.compress_algorithm, pVfs->zCompressType, 
                CCVFS_MAX_ALGORITHM_NAME - 1);
    }
    if (pVfs->zEncryptType) {
        strncpy(pFile->header.encrypt_algorithm, pVfs->zEncryptType,
                CCVFS_MAX_ALGORITHM_NAME - 1);
    }
    
    // Block configuration
    pFile->header.block_size = CCVFS_DEFAULT_BLOCK_SIZE;
    pFile->header.total_blocks = 0;
    pFile->header.index_table_offset = CCVFS_HEADER_SIZE;
    
    // Statistics
    pFile->header.original_file_size = 0;
    pFile->header.compressed_file_size = 0;
    pFile->header.compression_ratio = 100;  // No compression initially
    pFile->header.creation_flags = pVfs->creation_flags;
    
    // Security
    pFile->header.master_key_hash = 0;  // TODO: implement key management
    pFile->header.timestamp = (uint64_t)time(NULL);
    
    pFile->header_loaded = 1;
    
    CCVFS_DEBUG("Initialized new CCVFS header");
    return SQLITE_OK;
}

/*
 * Expand block index table
 */
static int ccvfs_expand_block_index(CCVFSFile *pFile, uint32_t new_block_count) {
    CCVFSBlockIndex *new_index;
    size_t new_size;
    
    if (new_block_count <= pFile->header.total_blocks) {
        return SQLITE_OK;  // Already large enough
    }
    
    new_size = new_block_count * sizeof(CCVFSBlockIndex);
    new_index = (CCVFSBlockIndex*)sqlite3_realloc(pFile->pBlockIndex, new_size);
    if (!new_index) {
        CCVFS_ERROR("Failed to expand block index");
        return SQLITE_NOMEM;
    }
    
    // Initialize new entries
    if (pFile->header.total_blocks < new_block_count) {
        memset(new_index + pFile->header.total_blocks, 0, 
               (new_block_count - pFile->header.total_blocks) * sizeof(CCVFSBlockIndex));
    }
    
    pFile->pBlockIndex = new_index;
    pFile->header.total_blocks = new_block_count;
    
    CCVFS_DEBUG("Expanded block index to %d blocks", new_block_count);
    return SQLITE_OK;
}