# 改进的批量写入逻辑设计

## 设计原则

1. **数据一致性优先**：确保校验和、压缩数据、索引的完全一致
2. **简化逻辑**：减少状态管理的复杂性
3. **性能优化**：减少重复计算和内存操作
4. **错误恢复**：提供清晰的错误处理和回滚机制

## 核心设计改进

### 1. 预处理缓冲区（Pre-processed Buffer）

**当前问题**：缓冲区存储原始数据，刷新时才处理
**改进方案**：缓冲区存储已处理的数据（压缩+加密+校验和）

```c
typedef struct CCVFSProcessedPage {
    uint32_t page_number;
    uint32_t original_size;
    uint32_t processed_size;
    uint32_t checksum;
    uint32_t flags;
    unsigned char *processed_data;  // 已压缩+加密的数据
    unsigned char *original_data;   // 原始数据（用于读取命中）
    int is_dirty;
    struct CCVFSProcessedPage *next;
} CCVFSProcessedPage;

typedef struct CCVFSBatchWriter {
    int enabled;
    uint32_t max_pages;
    uint32_t max_memory_mb;
    uint32_t auto_flush_threshold;
    
    CCVFSProcessedPage *pages;
    uint32_t page_count;
    uint32_t total_memory_used;
    
    // 统计信息
    uint32_t hits;
    uint32_t flushes;
    uint32_t merges;
    uint32_t total_writes;
} CCVFSBatchWriter;
```

### 2. 写入时立即处理（Write-Time Processing）

```c
int ccvfs_batch_write_page(CCVFSFile *pFile, uint32_t pageNum, 
                          const unsigned char *data, uint32_t dataSize) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    
    if (!pWriter->enabled) {
        return SQLITE_NOTFOUND;  // 直接写入
    }
    
    // 1. 立即处理数据（压缩+加密+校验和）
    CCVFSProcessedPage *pPage = ccvfs_process_page_data(pFile, pageNum, data, dataSize);
    if (!pPage) {
        return SQLITE_NOMEM;
    }
    
    // 2. 检查是否需要合并现有页面
    CCVFSProcessedPage *existing = ccvfs_find_processed_page(pWriter, pageNum);
    if (existing) {
        ccvfs_replace_processed_page(pWriter, existing, pPage);
        pWriter->merges++;
    } else {
        ccvfs_add_processed_page(pWriter, pPage);
    }
    
    pWriter->total_writes++;
    
    // 3. 检查是否需要自动刷新
    if (ccvfs_should_auto_flush(pWriter)) {
        return ccvfs_flush_batch_writer(pFile);
    }
    
    return SQLITE_OK;
}
```

### 3. 简化的刷新逻辑（Simplified Flush）

```c
int ccvfs_flush_batch_writer(CCVFSFile *pFile) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    CCVFSProcessedPage *pPage;
    int rc = SQLITE_OK;
    
    if (!pWriter->enabled || pWriter->page_count == 0) {
        return SQLITE_OK;
    }
    
    // 1. 分配连续的磁盘空间（批量分配）
    sqlite3_int64 batchOffset;
    uint32_t totalSize = ccvfs_calculate_batch_size(pWriter);
    
    rc = ccvfs_allocate_batch_space(pFile, totalSize, &batchOffset);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    // 2. 批量写入所有页面（一次I/O操作）
    rc = ccvfs_write_batch_pages(pFile, pWriter, batchOffset);
    if (rc != SQLITE_OK) {
        // 回滚空间分配
        ccvfs_rollback_batch_space(pFile, batchOffset, totalSize);
        return rc;
    }
    
    // 3. 批量更新页面索引（原子操作）
    rc = ccvfs_update_batch_index(pFile, pWriter, batchOffset);
    if (rc != SQLITE_OK) {
        // 这里需要更复杂的回滚逻辑
        return rc;
    }
    
    // 4. 清理批量写入器
    ccvfs_clear_batch_writer(pWriter);
    pWriter->flushes++;
    
    return SQLITE_OK;
}
```

### 4. 优化的读取逻辑（Optimized Read）

```c
int ccvfs_batch_read_page(CCVFSFile *pFile, uint32_t pageNum, 
                         unsigned char *buffer, uint32_t bufferSize) {
    CCVFSBatchWriter *pWriter = &pFile->batch_writer;
    
    if (!pWriter->enabled) {
        return SQLITE_NOTFOUND;
    }
    
    // 1. 查找批量写入器中的页面
    CCVFSProcessedPage *pPage = ccvfs_find_processed_page(pWriter, pageNum);
    if (!pPage) {
        return SQLITE_NOTFOUND;  // 从磁盘读取
    }
    
    // 2. 直接返回原始数据（无需解压缩）
    if (pPage->original_data && pPage->original_size <= bufferSize) {
        memcpy(buffer, pPage->original_data, pPage->original_size);
        if (pPage->original_size < bufferSize) {
            memset(buffer + pPage->original_size, 0, bufferSize - pPage->original_size);
        }
        pWriter->hits++;
        return SQLITE_OK;
    }
    
    return SQLITE_NOTFOUND;
}
```

## 关键优势

### 1. **数据一致性保证**
- 写入时立即计算校验和，避免后续不一致
- 批量刷新时数据已经处理完毕，减少错误
- 原子性的索引更新

### 2. **性能优化**
- 减少重复的压缩/解压缩操作
- 批量I/O操作，减少系统调用
- 连续空间分配，提高磁盘性能
- 读取命中时直接返回原始数据

### 3. **简化的错误处理**
- 清晰的回滚机制
- 减少中间状态
- 更好的错误隔离

### 4. **内存管理优化**
- 基于内存使用量的自动刷新
- 可配置的内存限制
- 更好的内存局部性

## 实现细节

### 空间分配策略
```c
// 批量空间分配，减少碎片
int ccvfs_allocate_batch_space(CCVFSFile *pFile, uint32_t totalSize, 
                              sqlite3_int64 *pOffset) {
    // 1. 尝试从空洞中分配连续空间
    sqlite3_int64 holeOffset = ccvfs_find_large_hole(pFile, totalSize);
    if (holeOffset > 0) {
        *pOffset = holeOffset;
        return SQLITE_OK;
    }
    
    // 2. 在文件末尾分配连续空间
    sqlite3_int64 fileSize;
    int rc = pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
    if (rc != SQLITE_OK) return rc;
    
    *pOffset = fileSize;
    return SQLITE_OK;
}
```

### 批量I/O操作
```c
int ccvfs_write_batch_pages(CCVFSFile *pFile, CCVFSBatchWriter *pWriter, 
                           sqlite3_int64 baseOffset) {
    // 构建连续的数据块
    unsigned char *batchData = ccvfs_build_batch_data(pWriter);
    if (!batchData) return SQLITE_NOMEM;
    
    // 一次性写入所有数据
    uint32_t totalSize = ccvfs_calculate_batch_size(pWriter);
    int rc = pFile->pReal->pMethods->xWrite(pFile->pReal, batchData, 
                                           totalSize, baseOffset);
    
    sqlite3_free(batchData);
    return rc;
}
```

### 配置接口
```c
int sqlite3_ccvfs_configure_batch_writer(const char *zVfsName,
                                         int enabled,
                                         uint32_t max_pages,
                                         uint32_t max_memory_mb,
                                         uint32_t auto_flush_threshold);
```

## 迁移策略

1. **保持向后兼容**：新的批量写入器与现有缓冲区并存
2. **渐进式迁移**：先实现核心功能，再逐步替换
3. **配置选择**：允许用户选择使用哪种批量写入策略
4. **性能测试**：全面测试新旧系统的性能差异

## 预期效果

- **数据一致性**：消除校验和不匹配问题
- **性能提升**：批量I/O和减少重复计算带来20-40%性能提升
- **内存效率**：更好的内存管理和局部性
- **代码简化**：减少复杂的状态管理逻辑
- **错误处理**：更清晰的错误恢复机制