# DB_TOOL 批量写入功能

## 概述

为 `db_tool` 工具添加了批量写入功能，允许用户测试和管理 CCVFS 的批量写入器。

## 新增功能

### 1. 批量写入测试 (batch-test)

测试批量写入功能的性能和正确性。

**用法：**
```bash
db_tool batch-test [选项] <数据库文件>
```

**选项：**
- `--batch-enable`: 启用批量写入 (使用 CCVFS)
- `--batch-pages <数量>`: 批量写入最大页数 (默认: 100)
- `--batch-memory <MB>`: 批量写入最大内存 (默认: 16MB)
- `--batch-records <数量>`: 批量测试记录数 (默认: 1000)
- `-v, --verbose`: 详细输出

**示例：**
```bash
# 使用标准 SQLite VFS 测试
db_tool batch-test test.db

# 启用批量写入测试
db_tool batch-test --batch-enable --batch-records 5000 test.db

# 自定义批量写入参数
db_tool batch-test --batch-enable --batch-pages 200 --batch-memory 32 --batch-records 10000 test.db
```

### 2. 批量写入统计 (batch-stats)

显示批量写入器的统计信息。

**用法：**
```bash
db_tool batch-stats [选项] <数据库文件>
```

**输出信息：**
- 缓存命中次数
- 刷新次数
- 合并次数
- 总写入页数
- 内存使用情况
- 当前缓冲页数

**示例：**
```bash
db_tool batch-stats test.db
db_tool batch-stats --verbose test.db
```

### 3. 批量写入刷新 (batch-flush)

强制刷新批量写入缓冲区到磁盘。

**用法：**
```bash
db_tool batch-flush [选项] <数据库文件>
```

**示例：**
```bash
db_tool batch-flush test.db
db_tool batch-flush --verbose test.db
```

## 技术实现

### 批量写入配置

批量写入器通过以下参数配置：
- `enabled`: 是否启用批量写入
- `max_pages`: 最大缓冲页数
- `max_memory_mb`: 最大内存使用 (MB)
- `auto_flush_threshold`: 自动刷新阈值 (页数)

### 测试流程

1. **初始化**: 创建 CCVFS 并配置批量写入器
2. **数据库操作**: 创建测试表并插入指定数量的记录
3. **性能测量**: 记录插入时间和成功率
4. **统计输出**: 显示批量写入器统计信息

### 错误处理

- 自动检测文件格式 (CCVFS vs 标准 SQLite)
- 优雅处理配置错误
- 详细的错误信息输出

## 性能对比

### 测试结果示例

**标准 SQLite VFS:**
```
尝试插入记录数: 1000
成功插入记录数: 1000
耗时: 0.01 秒
平均速度: 125000 记录/秒
```

**CCVFS 批量写入:**
```
尝试插入记录数: 1000
成功插入记录数: 1000
耗时: 0.15 秒
平均速度: 6667 记录/秒

批量写入器统计:
缓存命中: 15
刷新次数: 2
合并次数: 8
总写入页数: 25
内存使用: 1.2 MB
缓冲页数: 5
```

## 使用建议

1. **性能测试**: 使用不同的记录数量测试性能
2. **内存调优**: 根据系统内存调整 `--batch-memory` 参数
3. **页数优化**: 根据数据特征调整 `--batch-pages` 参数
4. **监控统计**: 定期检查批量写入器统计信息
5. **手动刷新**: 在关键时刻手动刷新缓冲区

## 注意事项

- 批量写入功能仅在 CCVFS 格式的数据库中有效
- 标准 SQLite 数据库会显示零统计信息
- 批量写入可能会增加内存使用
- 自动刷新阈值影响性能和内存使用的平衡
- **重要**: 批量写入选项 (`--batch-enable`, `--batch-pages`, `--batch-memory`, `--batch-records`) 只能用于 `batch-test` 操作
- 在其他操作中使用批量写入选项会导致错误

## 故障排除

### 常见问题

1. **VFS 未找到错误**: 确保 CCVFS 已正确初始化
2. **插入失败**: 检查数据库文件权限和磁盘空间
3. **统计信息为零**: 确认数据库是 CCVFS 格式
4. **"批量写入选项只能用于 batch-test 操作"错误**: 确保只在 `batch-test` 命令中使用批量写入选项

### 调试选项

使用 `--verbose` 选项获取详细的执行信息，包括：
- CCVFS 初始化过程
- 批量写入器配置
- 详细的统计信息
- 错误诊断信息