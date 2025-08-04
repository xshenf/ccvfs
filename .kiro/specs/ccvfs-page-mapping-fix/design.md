# Design Document

## Overview

通过分析代码发现，CCVFS压缩文件变大的主要原因包括：
1. 页索引保存位置不固定，导致重复写入和空间浪费
2. 空间分配策略过于保守，总是分配新空间而不重用现有空间
3. 压缩效果检查不充分，可能使用了比原数据更大的压缩数据
4. 页面映射逻辑存在缺陷，导致物理空间利用率低

## Architecture

### 核心组件关系
```
db_tool -> db_compress_tool -> CCVFS -> ccvfs_io -> ccvfs_page
```

### 问题定位
1. **ccvfs_page.c**: 页索引保存位置计算错误
2. **ccvfs_io.c**: 空间分配策略需要优化
3. **db_compress_tool.c**: 压缩流程需要验证

## Components and Interfaces

### 1. 页索引管理组件 (ccvfs_page.c)

**当前问题:**
- `ccvfs_calculate_index_position()` 动态计算位置导致索引重复写入
- 页索引没有使用固定的 `CCVFS_INDEX_TABLE_OFFSET` 位置

**修复方案:**
```c
// 修复页索引保存位置
int ccvfs_save_page_index(CCVFSFile *pFile) {
    // 使用固定位置而不是动态计算
    sqlite3_int64 index_offset = CCVFS_INDEX_TABLE_OFFSET;
    // 写入到固定位置
    rc = pFile->pReal->pMethods->xWrite(pFile->pReal, pFile->pPageIndex,
                                        index_size, index_offset);
}
```

### 2. 空间分配组件 (ccvfs_io.c)

**当前问题:**
- `ccvfs_find_best_fit_space()` 被禁用，总是返回0
- `writePage()` 中的空间重用逻辑过于复杂且效率低

**修复方案:**
```c
// 简化空间重用策略
static int writePage(CCVFSFile *pFile, uint32_t pageNum, const unsigned char *data, uint32_t dataSize) {
    // 1. 检查现有页面是否可以重用
    if (pIndex->physical_offset != 0 && compressedSize <= pIndex->compressed_size) {
        // 直接重用现有空间
        writeOffset = pIndex->physical_offset;
    } else {
        // 2. 分配新空间到文件末尾
        sqlite3_int64 fileSize;
        pFile->pReal->pMethods->xFileSize(pFile->pReal, &fileSize);
        writeOffset = fileSize;
    }
}
```

### 3. 压缩验证组件

**当前问题:**
- 压缩效果检查不充分
- 可能使用了比原数据更大的压缩结果

**修复方案:**
```c
// 增强压缩效果验证
if (pFile->pOwner->pCompressAlg) {
    int rc = pFile->pOwner->pCompressAlg->compress(data, dataSize, compressedData, maxCompressedSize, 1);
    if (rc > 0 && (uint32_t)rc < dataSize * 0.95) { // 至少5%压缩率
        compressedSize = rc;
        flags |= CCVFS_PAGE_COMPRESSED;
    } else {
        // 压缩无效，使用原始数据
        sqlite3_free(compressedData);
        compressedData = NULL;
        compressedSize = dataSize;
    }
}
```

## Data Models

### 页索引结构优化
```c
typedef struct {
    uint64_t physical_offset;      // 物理文件偏移 - 使用固定索引表位置
    uint32_t compressed_size;      // 压缩后大小 - 确保小于原始大小
    uint32_t original_size;        // 原始大小
    uint32_t checksum;             // 页面校验和
    uint32_t flags;                // 页面标志 - 包含压缩有效性标志
} CCVFSPageIndex;
```

### 文件布局优化
```
[Header: 128B] -> [Index Table: Fixed Position] -> [Data Pages: Variable]
```

## Error Handling

### 1. 页索引保存错误处理
- 验证索引表大小不超过预留空间
- 检查写入操作返回值
- 提供回滚机制

### 2. 空间分配错误处理
- 文件大小获取失败时的处理
- 空间不足时的处理策略
- 写入失败时的清理操作

### 3. 压缩失败处理
- 压缩算法返回错误时使用原始数据
- 内存分配失败时的清理
- 数据完整性验证

## Testing Strategy

### 1. 单元测试
- 页索引保存/加载功能测试
- 空间分配策略测试
- 压缩效果验证测试

### 2. 集成测试
- db_tool压缩完整流程测试
- 不同大小数据库的压缩测试
- 压缩前后文件大小对比测试

### 3. 性能测试
- 大文件压缩性能测试
- 空间利用率测试
- 读写性能对比测试

### 4. 回归测试
- 确保修复不影响现有功能
- 验证解压缩功能正常
- 数据完整性验证