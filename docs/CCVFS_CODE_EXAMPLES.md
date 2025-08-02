# CCVFS 压缩数据库实现代码示例

## 核心函数实现示例

### 1. 数据写入实现

```c
// CCVFS 写入操作的核心实现
static int ccvfsIoWrite(sqlite3_file *pFile, const void *zBuf, 
                       int iAmt, sqlite3_int64 iOfst) {
    CCVFSFile *pCcvfsFile = (CCVFSFile*)pFile;
    const char *data = (const char*)zBuf;
    int bytes_remaining = iAmt;
    sqlite3_int64 current_offset = iOfst;
    
    // 处理跨多个块的写入
    while (bytes_remaining > 0) {
        // 1. 计算目标块编号和块内偏移
        uint32_t block_number = (uint32_t)(current_offset / pCcvfsFile->header.block_size);
        uint32_t block_offset = (uint32_t)(current_offset % pCcvfsFile->header.block_size);
        uint32_t bytes_in_block = pCcvfsFile->header.block_size - block_offset;
        
        if (bytes_in_block > bytes_remaining) {
            bytes_in_block = bytes_remaining;
        }
        
        // 2. 获取或创建块缓冲区
        char *block_buffer = getOrCreateBlockBuffer(pCcvfsFile, block_number);
        if (!block_buffer) {
            return SQLITE_NOMEM;
        }
        
        // 3. 如果是部分块写入，先读取现有数据
        if (block_offset > 0 || bytes_in_block < pCcvfsFile->header.block_size) {
            int rc = readBlock(pCcvfsFile, block_number, block_buffer);
            if (rc != SQLITE_OK && rc != SQLITE_IOERR_SHORT_READ) {
                return rc;
            }
        }
        
        // 4. 将新数据写入块缓冲区
        memcpy(block_buffer + block_offset, data, bytes_in_block);
        
        // 5. 标记块为脏块，需要回写
        markBlockDirty(pCcvfsFile, block_number);
        
        // 6. 更新循环变量
        data += bytes_in_block;
        current_offset += bytes_in_block;
        bytes_remaining -= bytes_in_block;
    }
    
    return SQLITE_OK;
}

// 块回写到磁盘的实现
static int writeBlock(CCVFSFile *pCcvfsFile, uint32_t block_number, 
                     const char *block_data) {
    CCVFS *pVfs = pCcvfsFile->pVfs;
    uint32_t block_size = pCcvfsFile->header.block_size;
    
    // 1. 分配压缩缓冲区
    char *compress_buffer = malloc(block_size + 1024); // 额外空间防止压缩膨胀
    char *final_buffer = compress_buffer;
    uint32_t final_size = block_size + 1024;
    
    // 2. 压缩数据
    int rc = pVfs->pCompressAlg->compress(
        block_data, block_size,
        compress_buffer, &final_size
    );
    
    if (rc != SQLITE_OK) {
        free(compress_buffer);
        return rc;
    }
    
    CCVFS_DEBUG("Block %u compressed from %u to %u bytes", 
                block_number, block_size, final_size);
    
    // 3. 加密数据（如果启用）
    if (pVfs->pEncryptAlg) {
        char *encrypt_buffer = malloc(final_size + 256); // 加密可能增加大小
        uint32_t encrypt_size = final_size + 256;
        
        rc = pVfs->pEncryptAlg->encrypt(
            final_buffer, final_size,
            encrypt_buffer, &encrypt_size
        );
        
        if (rc == SQLITE_OK) {
            if (final_buffer != compress_buffer) free(final_buffer);
            final_buffer = encrypt_buffer;
            final_size = encrypt_size;
            CCVFS_DEBUG("Block %u encrypted, size %u", block_number, final_size);
        } else {
            free(encrypt_buffer);
            if (final_buffer != compress_buffer) free(final_buffer);
            free(compress_buffer);
            return rc;
        }
    }
    
    // 4. 创建块头
    CCVFSBlockHeader header = {
        .magic = CCVFS_BLOCK_MAGIC,
        .sequence = block_number,
        .original_size = block_size,
        .compressed_size = final_size,
        .checksum = calculate_crc32(final_buffer, final_size),
        .flags = 0
    };
    
    // 5. 计算物理写入位置
    sqlite3_int64 physical_offset = calculatePhysicalOffset(pCcvfsFile, block_number);
    
    // 6. 写入块头和数据
    rc = pCcvfsFile->pReal->pMethods->xWrite(
        pCcvfsFile->pReal, &header, sizeof(header), physical_offset
    );
    
    if (rc == SQLITE_OK) {
        rc = pCcvfsFile->pReal->pMethods->xWrite(
            pCcvfsFile->pReal, final_buffer, final_size, 
            physical_offset + sizeof(header)
        );
    }
    
    // 7. 更新块索引
    if (rc == SQLITE_OK) {
        updateBlockIndex(pCcvfsFile, block_number, physical_offset, 
                        sizeof(header) + final_size);
    }
    
    // 8. 清理内存
    if (final_buffer != compress_buffer) free(final_buffer);
    free(compress_buffer);
    
    return rc;
}
```

### 2. 数据读取实现

```c
// CCVFS 读取操作的核心实现
static int ccvfsIoRead(sqlite3_file *pFile, void *zBuf, 
                      int iAmt, sqlite3_int64 iOfst) {
    CCVFSFile *pCcvfsFile = (CCVFSFile*)pFile;
    char *buffer = (char*)zBuf;
    int bytes_remaining = iAmt;
    sqlite3_int64 current_offset = iOfst;
    
    // 初始化输出缓冲区
    memset(zBuf, 0, iAmt);
    
    // 处理跨多个块的读取
    while (bytes_remaining > 0) {
        // 1. 计算源块编号和块内偏移
        uint32_t block_number = (uint32_t)(current_offset / pCcvfsFile->header.block_size);
        uint32_t block_offset = (uint32_t)(current_offset % pCcvfsFile->header.block_size);
        uint32_t bytes_in_block = pCcvfsFile->header.block_size - block_offset;
        
        if (bytes_in_block > bytes_remaining) {
            bytes_in_block = bytes_remaining;
        }
        
        // 2. 检查块是否超出文件范围
        if (block_number >= pCcvfsFile->header.block_count) {
            // 读取超出文件末尾，返回部分数据
            return SQLITE_IOERR_SHORT_READ;
        }
        
        // 3. 尝试从缓存获取块数据
        char *block_data = getBlockFromCache(pCcvfsFile, block_number);
        
        if (!block_data) {
            // 4. 缓存未命中，从磁盘读取
            block_data = malloc(pCcvfsFile->header.block_size);
            if (!block_data) {
                return SQLITE_NOMEM;
            }
            
            int rc = readBlock(pCcvfsFile, block_number, block_data);
            if (rc != SQLITE_OK) {
                free(block_data);
                if (rc == SQLITE_IOERR_SHORT_READ && current_offset >= 0) {
                    return SQLITE_OK; // 部分读取成功
                }
                return rc;
            }
            
            // 5. 将块加入缓存
            addBlockToCache(pCcvfsFile, block_number, block_data);
        }
        
        // 6. 从块中复制请求的数据
        memcpy(buffer, block_data + block_offset, bytes_in_block);
        
        // 7. 更新循环变量
        buffer += bytes_in_block;
        current_offset += bytes_in_block;
        bytes_remaining -= bytes_in_block;
    }
    
    return SQLITE_OK;
}

// 从磁盘读取并解压缩块的实现
static int readBlock(CCVFSFile *pCcvfsFile, uint32_t block_number, 
                    char *output_buffer) {
    CCVFS *pVfs = pCcvfsFile->pVfs;
    
    // 1. 获取块的物理位置
    sqlite3_int64 physical_offset;
    uint32_t physical_size;
    
    int rc = getBlockLocation(pCcvfsFile, block_number, 
                             &physical_offset, &physical_size);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    // 2. 读取块头
    CCVFSBlockHeader header;
    rc = pCcvfsFile->pReal->pMethods->xRead(
        pCcvfsFile->pReal, &header, sizeof(header), physical_offset
    );
    
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    // 3. 验证块头
    if (header.magic != CCVFS_BLOCK_MAGIC || 
        header.sequence != block_number) {
        CCVFS_ERROR("Block %u header validation failed", block_number);
        return SQLITE_CORRUPT;
    }
    
    // 4. 读取压缩数据
    char *compressed_data = malloc(header.compressed_size);
    if (!compressed_data) {
        return SQLITE_NOMEM;
    }
    
    rc = pCcvfsFile->pReal->pMethods->xRead(
        pCcvfsFile->pReal, compressed_data, header.compressed_size,
        physical_offset + sizeof(header)
    );
    
    if (rc != SQLITE_OK) {
        free(compressed_data);
        return rc;
    }
    
    // 5. 验证校验和
    uint32_t calculated_checksum = calculate_crc32(compressed_data, header.compressed_size);
    if (calculated_checksum != header.checksum) {
        CCVFS_ERROR("Block %u checksum validation failed", block_number);
        free(compressed_data);
        return SQLITE_CORRUPT;
    }
    
    // 6. 解密数据（如果需要）
    char *decrypted_data = compressed_data;
    uint32_t decrypted_size = header.compressed_size;
    
    if (pVfs->pEncryptAlg) {
        decrypted_data = malloc(header.compressed_size);
        if (!decrypted_data) {
            free(compressed_data);
            return SQLITE_NOMEM;
        }
        
        rc = pVfs->pEncryptAlg->decrypt(
            compressed_data, header.compressed_size,
            decrypted_data, &decrypted_size
        );
        
        if (rc != SQLITE_OK) {
            free(compressed_data);
            free(decrypted_data);
            return rc;
        }
        
        CCVFS_DEBUG("Block %u decrypted", block_number);
    }
    
    // 7. 解压缩数据
    uint32_t decompressed_size = header.original_size;
    rc = pVfs->pCompressAlg->decompress(
        decrypted_data, decrypted_size,
        output_buffer, &decompressed_size
    );
    
    // 8. 清理内存
    free(compressed_data);
    if (decrypted_data != compressed_data) {
        free(decrypted_data);
    }
    
    if (rc == SQLITE_OK) {
        CCVFS_DEBUG("Block %u decompressed from %u to %u bytes", 
                    block_number, decrypted_size, decompressed_size);
    }
    
    return rc;
}
```

### 3. 块缓存管理

```c
// LRU 块缓存实现
typedef struct BlockCacheEntry {
    uint32_t block_number;
    char *data;
    int dirty;                    // 是否被修改
    struct BlockCacheEntry *prev;
    struct BlockCacheEntry *next;
} BlockCacheEntry;

typedef struct BlockCache {
    BlockCacheEntry *head;
    BlockCacheEntry *tail;
    BlockCacheEntry **hash_table;
    int capacity;
    int size;
    uint32_t block_size;
} BlockCache;

// 获取缓存的块数据
static char* getBlockFromCache(CCVFSFile *pCcvfsFile, uint32_t block_number) {
    BlockCache *cache = pCcvfsFile->block_cache;
    if (!cache) return NULL;
    
    // 哈希查找
    uint32_t hash = block_number % cache->capacity;
    BlockCacheEntry *entry = cache->hash_table[hash];
    
    while (entry) {
        if (entry->block_number == block_number) {
            // 命中，移动到链表头部 (LRU)
            moveToHead(cache, entry);
            return entry->data;
        }
        entry = entry->next;
    }
    
    return NULL; // 缓存未命中
}

// 添加块到缓存
static int addBlockToCache(CCVFSFile *pCcvfsFile, uint32_t block_number, 
                          const char *block_data) {
    BlockCache *cache = pCcvfsFile->block_cache;
    if (!cache) return SQLITE_OK;
    
    // 检查是否需要淘汰旧块
    if (cache->size >= cache->capacity) {
        evictLRUBlock(cache);
    }
    
    // 创建新的缓存条目
    BlockCacheEntry *entry = malloc(sizeof(BlockCacheEntry));
    if (!entry) return SQLITE_NOMEM;
    
    entry->data = malloc(cache->block_size);
    if (!entry->data) {
        free(entry);
        return SQLITE_NOMEM;
    }
    
    entry->block_number = block_number;
    memcpy(entry->data, block_data, cache->block_size);
    entry->dirty = 0;
    
    // 添加到哈希表和LRU链表
    uint32_t hash = block_number % cache->capacity;
    entry->next = cache->hash_table[hash];
    cache->hash_table[hash] = entry;
    
    addToHead(cache, entry);
    cache->size++;
    
    return SQLITE_OK;
}

// 刷新脏块到磁盘
static int flushDirtyBlocks(CCVFSFile *pCcvfsFile) {
    BlockCache *cache = pCcvfsFile->block_cache;
    if (!cache) return SQLITE_OK;
    
    BlockCacheEntry *entry = cache->head;
    while (entry) {
        if (entry->dirty) {
            int rc = writeBlock(pCcvfsFile, entry->block_number, entry->data);
            if (rc != SQLITE_OK) {
                return rc;
            }
            entry->dirty = 0;
        }
        entry = entry->next;
    }
    
    return SQLITE_OK;
}
```

### 4. 事务处理和同步

```c
// 文件同步实现
static int ccvfsIoSync(sqlite3_file *pFile, int flags) {
    CCVFSFile *pCcvfsFile = (CCVFSFile*)pFile;
    
    // 1. 刷新所有脏块到磁盘
    int rc = flushDirtyBlocks(pCcvfsFile);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    // 2. 保存块索引
    rc = saveBlockIndex(pCcvfsFile);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    // 3. 同步底层文件
    rc = pCcvfsFile->pReal->pMethods->xSync(pCcvfsFile->pReal, flags);
    if (rc == SQLITE_OK) {
        CCVFS_DEBUG("File synced successfully");
    }
    
    return rc;
}

// 块索引保存实现
static int saveBlockIndex(CCVFSFile *pCcvfsFile) {
    if (!pCcvfsFile->index_dirty) {
        return SQLITE_OK; // 索引未修改，无需保存
    }
    
    // 1. 序列化块索引
    uint32_t index_size = pCcvfsFile->header.block_count * sizeof(CCVFSBlockIndexEntry);
    char *index_buffer = malloc(index_size);
    if (!index_buffer) {
        return SQLITE_NOMEM;
    }
    
    for (uint32_t i = 0; i < pCcvfsFile->header.block_count; i++) {
        CCVFSBlockIndexEntry *entry = (CCVFSBlockIndexEntry*)(index_buffer + i * sizeof(CCVFSBlockIndexEntry));
        entry->offset = pCcvfsFile->block_index[i].offset;
        entry->size = pCcvfsFile->block_index[i].size;
        entry->checksum = pCcvfsFile->block_index[i].checksum;
        entry->flags = pCcvfsFile->block_index[i].flags;
    }
    
    // 2. 写入索引到固定位置
    int rc = pCcvfsFile->pReal->pMethods->xWrite(
        pCcvfsFile->pReal, index_buffer, index_size,
        CCVFS_HEADER_SIZE  // 索引紧跟在文件头后面
    );
    
    if (rc == SQLITE_OK) {
        pCcvfsFile->index_dirty = 0;
        CCVFS_DEBUG("Block index saved: %u blocks", pCcvfsFile->header.block_count);
    }
    
    free(index_buffer);
    return rc;
}
```

### 5. 错误处理和恢复

```c
// 数据完整性检查
static int verifyFileIntegrity(CCVFSFile *pCcvfsFile) {
    CCVFS_INFO("Verifying file integrity...");
    
    // 1. 检查文件头
    if (memcmp(pCcvfsFile->header.magic, CCVFS_MAGIC, 8) != 0) {
        CCVFS_ERROR("Invalid file magic");
        return SQLITE_CORRUPT;
    }
    
    // 2. 验证每个块的完整性
    for (uint32_t i = 0; i < pCcvfsFile->header.block_count; i++) {
        CCVFSBlockHeader block_header;
        sqlite3_int64 offset = pCcvfsFile->block_index[i].offset;
        
        int rc = pCcvfsFile->pReal->pMethods->xRead(
            pCcvfsFile->pReal, &block_header, sizeof(block_header), offset
        );
        
        if (rc != SQLITE_OK) {
            CCVFS_ERROR("Cannot read block %u header", i);
            return rc;
        }
        
        // 验证块头
        if (block_header.magic != CCVFS_BLOCK_MAGIC || 
            block_header.sequence != i) {
            CCVFS_ERROR("Block %u header corruption", i);
            return SQLITE_CORRUPT;
        }
        
        // 验证块数据校验和
        char *block_data = malloc(block_header.compressed_size);
        if (!block_data) return SQLITE_NOMEM;
        
        rc = pCcvfsFile->pReal->pMethods->xRead(
            pCcvfsFile->pReal, block_data, block_header.compressed_size,
            offset + sizeof(block_header)
        );
        
        if (rc == SQLITE_OK) {
            uint32_t checksum = calculate_crc32(block_data, block_header.compressed_size);
            if (checksum != block_header.checksum) {
                CCVFS_ERROR("Block %u data corruption", i);
                rc = SQLITE_CORRUPT;
            }
        }
        
        free(block_data);
        if (rc != SQLITE_OK) return rc;
    }
    
    CCVFS_INFO("File integrity check passed");
    return SQLITE_OK;
}

// 损坏数据的恢复尝试
static int attemptBlockRecovery(CCVFSFile *pCcvfsFile, uint32_t block_number) {
    CCVFS_INFO("Attempting to recover block %u", block_number);
    
    // 这里可以实现各种恢复策略：
    // 1. 从备份文件恢复
    // 2. 使用冗余数据重建
    // 3. 标记为损坏并跳过
    
    // 简单实现：用零填充损坏的块
    char *zero_block = calloc(1, pCcvfsFile->header.block_size);
    if (!zero_block) return SQLITE_NOMEM;
    
    int rc = writeBlock(pCcvfsFile, block_number, zero_block);
    free(zero_block);
    
    if (rc == SQLITE_OK) {
        CCVFS_INFO("Block %u recovered with zero data", block_number);
    } else {
        CCVFS_ERROR("Block %u recovery failed", block_number);
    }
    
    return rc;
}
```

## 使用示例

### 创建和使用压缩数据库

```c
#include "compress_vfs.h"

int main() {
    sqlite3 *db;
    int rc;
    
    // 1. 初始化 CCVFS
    rc = sqlite3_ccvfs_create("ccvfs", NULL, "zlib", NULL, 64*1024, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create CCVFS: %d\n", rc);
        return 1;
    }
    
    // 2. 打开压缩数据库
    rc = sqlite3_open_v2("compressed.db", &db, 
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                        "ccvfs");
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    // 3. 正常的 SQLite 操作
    rc = sqlite3_exec(db, 
        "CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)",
        NULL, NULL, NULL);
    
    if (rc == SQLITE_OK) {
        // 插入大量测试数据
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        
        for (int i = 0; i < 10000; i++) {
            char sql[1024];
            snprintf(sql, sizeof(sql), 
                "INSERT INTO test (data) VALUES ('Test data for record %d - %s')",
                i, "This is some sample text that will be compressed");
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        }
        
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
        printf("Inserted 10000 records\n");
    }
    
    // 4. 查询数据
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            printf("Total records: %d\n", count);
        }
        sqlite3_finalize(stmt);
    }
    
    // 5. 关闭数据库
    sqlite3_close(db);
    
    // 6. 清理 CCVFS
    sqlite3_ccvfs_destroy("ccvfs");
    
    return 0;
}
```

这些代码示例展示了 CCVFS 压缩数据库系统的核心实现细节，包括数据的透明压缩、解压缩、缓存管理、错误处理等关键机制。