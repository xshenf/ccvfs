# CCVFS 压缩数据库使用指南

## 快速开始

### 1. 基本概念

CCVFS (Compressed and Encrypted Virtual File System) 是一个 SQLite 虚拟文件系统扩展，提供：

- **透明压缩**: 自动压缩数据库文件，节省存储空间
- **可选加密**: 保护敏感数据安全
- **无缝集成**: 使用标准 SQLite API，无需修改应用代码
- **多种算法**: 支持 RLE、LZ4、Zlib 等压缩算法

### 2. 创建压缩数据库

```bash
# 使用工具创建压缩数据库
./db_generator.exe -c -a zlib -b 64K compressed.ccvfs 10MB

# 参数说明:
# -c: 启用压缩
# -a zlib: 使用 zlib 压缩算法
# -b 64K: 设置 64KB 块大小
# compressed.ccvfs: 输出文件名
# 10MB: 目标文件大小
```

### 3. 程序中使用

```c
#include "compress_vfs.h"

// 创建 CCVFS 实例
sqlite3_ccvfs_create("my_ccvfs", NULL, "zlib", NULL, 64*1024, 0);

// 打开压缩数据库
sqlite3 *db;
sqlite3_open_v2("data.ccvfs", &db, SQLITE_OPEN_READWRITE, "my_ccvfs");

// 正常使用 SQLite API
sqlite3_exec(db, "CREATE TABLE users (id INTEGER, name TEXT)", NULL, NULL, NULL);
sqlite3_exec(db, "INSERT INTO users VALUES (1, 'Alice')", NULL, NULL, NULL);

// 关闭数据库
sqlite3_close(db);
sqlite3_ccvfs_destroy("my_ccvfs");
```

## 详细配置指南

### 压缩算法选择

| 算法 | 压缩率 | 速度 | CPU使用 | 适用场景 |
|------|--------|------|---------|----------|
| **RLE** | 低(30%) | 极快 | 极低 | 简单重复数据 |
| **LZ4** | 中(60%) | 快 | 低 | 实时系统,OLTP |
| **Zlib** | 高(80%) | 中等 | 中等 | 存储优化,归档 |

**选择建议:**
```bash
# 实时应用 - 优先性能
./db_generator.exe -c -a lz4 -b 16K fast.ccvfs 1GB

# 存储优化 - 优先空间
./db_generator.exe -c -a zlib -b 256K archive.ccvfs 1GB

# 平衡方案 - 推荐配置
./db_generator.exe -c -a zlib -b 64K balanced.ccvfs 1GB
```

### 块大小配置

| 块大小 | 内存使用 | 压缩效率 | 随机访问 | 适用场景 |
|--------|----------|----------|----------|----------|
| **4KB** | 低 | 较低 | 优秀 | OLTP,频繁随机访问 |
| **64KB** | 中等 | 良好 | 良好 | 通用推荐配置 |
| **256KB** | 中高 | 优秀 | 一般 | OLAP,批量处理 |
| **1MB** | 高 | 最优 | 较差 | 数据仓库,归档 |

**配置示例:**
```bash
# OLTP 系统
./db_generator.exe -c -a lz4 -b 4K oltp.ccvfs 500MB

# OLAP 系统  
./db_generator.exe -c -a zlib -b 256K olap.ccvfs 10GB

# 归档存储
./db_generator.exe -c -a zlib -b 1M archive.ccvfs 100GB
```

### 加密配置

```bash
# 启用 XOR 简单加密 (测试用)
./db_generator.exe -c -e xor secure_test.ccvfs 1GB

# 启用 AES 加密 (生产环境)
./db_generator.exe -c -e aes256 secure_prod.ccvfs 1GB

# 压缩+加密组合
./db_generator.exe -c -a zlib -e aes256 -b 64K secure.ccvfs 1GB
```

## 性能优化建议

### 1. 应用场景优化

**OLTP (在线事务处理):**
```bash
# 配置: 小块 + 快速压缩 + 缓存优化
./db_generator.exe -c -a lz4 -b 16K \
  --batch-size 500 oltp_optimized.ccvfs 1GB
```

**OLAP (在线分析处理):**
```bash
# 配置: 大块 + 高压缩 + 批量处理
./db_generator.exe -c -a zlib -b 256K \
  --batch-size 10000 olap_optimized.ccvfs 10GB
```

**数据归档:**
```bash
# 配置: 最大块 + 最高压缩 + 加密
./db_generator.exe -c -a zlib -e aes256 -b 1M \
  archive_optimized.ccvfs 100GB
```

### 2. 性能监控

```c
// 在应用中监控压缩性能
typedef struct {
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t cache_hits;
    uint64_t cache_misses;
    double avg_compression_ratio;
} CCVFSStats;

// 获取统计信息
CCVFSStats stats;
sqlite3_ccvfs_get_stats("my_ccvfs", &stats);

printf("Cache hit ratio: %.2f%%\n", 
       (double)stats.cache_hits / (stats.cache_hits + stats.cache_misses) * 100);
printf("Compression ratio: %.2f%%\n", stats.avg_compression_ratio * 100);
```

## 常见使用模式

### 1. 开发测试环境

```bash
# 快速创建测试数据
./db_generator.exe -v -t 5 -m random test_dev.db 10MB

# 创建压缩测试数据
./db_generator.exe -c -v -t 5 -m lorem test_compressed.ccvfs 10MB

# 验证数据完整性
./db_tool.exe verify test_compressed.ccvfs
```

### 2. 生产环境部署

```c
// 生产环境配置示例
int setup_production_db() {
    // 1. 创建针对生产优化的 CCVFS
    int rc = sqlite3_ccvfs_create(
        "prod_ccvfs",           // VFS 名称
        NULL,                   // 使用默认底层 VFS
        "zlib",                 // 平衡压缩算法
        "aes256",               // 生产级加密
        64 * 1024,              // 64KB 块大小
        CCVFS_CREATE_OFFLINE    // 离线模式标志
    );
    
    if (rc != SQLITE_OK) {
        log_error("Failed to create production CCVFS: %d", rc);
        return rc;
    }
    
    // 2. 配置性能参数
    sqlite3_ccvfs_set_cache_size("prod_ccvfs", 32); // 32个块缓存
    sqlite3_ccvfs_enable_prefetch("prod_ccvfs", 1); // 启用预取
    
    return SQLITE_OK;
}
```

### 3. 数据迁移

```bash
# 从普通数据库迁移到压缩数据库
./db_tool.exe compress source.db compressed.ccvfs --algorithm zlib

# 从压缩数据库恢复到普通数据库  
./db_tool.exe decompress compressed.ccvfs restored.db

# 批量迁移脚本
for db in *.db; do
    echo "Compressing $db..."
    ./db_tool.exe compress "$db" "${db%.db}.ccvfs" --algorithm zlib
done
```

## 故障排除

### 1. 常见错误

**错误: SQLITE_CORRUPT**
```bash
# 检查文件完整性
./db_tool.exe verify problematic.ccvfs

# 尝试修复
./db_tool.exe repair problematic.ccvfs fixed.ccvfs
```

**错误: 压缩文件比原文件大**
```
原因: 小文件的固定开销(1.5MB索引表)
解决: 使用更小的块大小或者不压缩小文件

# 对小文件使用4KB块
./db_generator.exe -c -b 4K small.ccvfs 1MB
```

**错误: 性能下降明显**
```
检查点:
1. 块大小是否合适当前访问模式
2. 缓存配置是否足够
3. 压缩算法是否适合数据特性

# 性能分析
./db_tool.exe analyze slow.ccvfs --performance
```

### 2. 调试模式

```c
// 启用详细日志
#define ENABLE_DEBUG 1
#define ENABLE_VERBOSE 1

// 编译时启用调试
gcc -DENABLE_DEBUG -DENABLE_VERBOSE compress_vfs.c -o debug_version
```

### 3. 备份和恢复

```bash
# 创建压缩数据库的备份
./db_tool.exe backup source.ccvfs backup.ccvfs

# 验证备份完整性
./db_tool.exe verify backup.ccvfs

# 从备份恢复
./db_tool.exe restore backup.ccvfs recovered.ccvfs
```

## 最佳实践

### 1. 设计原则

**选择合适的压缩策略:**
- 文件大小 < 10MB: 考虑不压缩或使用4KB块
- 文件大小 10MB-100MB: 使用64KB块 + zlib
- 文件大小 > 100MB: 使用256KB-1MB块 + zlib

**数据库设计优化:**
- 使用适当的数据类型减少存储空间
- 合理设计索引减少随机访问
- 批量操作优于频繁小事务

### 2. 监控和维护

```bash
# 定期检查数据库健康状态
./db_tool.exe health_check *.ccvfs

# 分析压缩效果
./db_tool.exe analyze *.ccvfs --compression-stats

# 性能基准测试
./db_tool.exe benchmark test.ccvfs --operations 10000
```

### 3. 升级和兼容性

```c
// 检查 CCVFS 版本兼容性
int check_compatibility(const char *filename) {
    CCVFSFileHeader header;
    // 读取文件头检查版本
    if (header.version > CURRENT_CCVFS_VERSION) {
        return SQLITE_VERSION_MISMATCH;
    }
    return SQLITE_OK;
}
```

## 总结

CCVFS 提供了一个强大而灵活的数据库压缩解决方案，通过合理配置可以实现：

- **空间节省**: 70-90% 的存储空间减少
- **透明操作**: 无需修改现有应用代码  
- **性能平衡**: 合理的读写性能权衡
- **数据安全**: 可选的加密保护

关键是根据具体应用场景选择合适的压缩算法、块大小和缓存策略，并进行充分的测试验证。