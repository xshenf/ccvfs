# Requirements Document

## Introduction

当前使用db_tool压缩db_generator生成的数据库时，压缩后的文件比原文件大很多。通过代码分析发现，问题主要出现在页映射和空间分配策略上。需要修复页索引保存位置、空间重用逻辑和压缩效率问题。

## Requirements

### Requirement 1

**User Story:** 作为开发者，我希望压缩工具能够正确管理页面映射，以便压缩后的文件大小合理且不会比原文件更大

#### Acceptance Criteria

1. WHEN 使用db_tool压缩数据库 THEN 压缩后文件大小应该小于或等于原文件大小
2. WHEN 页面被写入时 THEN 页索引应该正确记录物理偏移和大小信息
3. WHEN 保存页索引时 THEN 索引应该写入到正确的固定位置而不是文件末尾

### Requirement 2

**User Story:** 作为开发者，我希望页面空间分配策略能够有效重用空间，以便减少文件碎片和大小

#### Acceptance Criteria

1. WHEN 页面大小变小时 THEN 系统应该重用现有的物理空间
2. WHEN 页面大小增长时 THEN 系统应该智能决定是扩展现有空间还是分配新空间
3. WHEN 存在空洞时 THEN 系统应该优先使用空洞而不是总是追加到文件末尾

### Requirement 3

**User Story:** 作为开发者，我希望压缩算法能够有效工作，以便真正减少文件大小

#### Acceptance Criteria

1. WHEN 数据可压缩时 THEN 压缩算法应该产生更小的输出
2. WHEN 压缩无效时 THEN 系统应该使用原始数据而不是更大的压缩数据
3. WHEN 页面全为零时 THEN 系统应该将其标记为稀疏页面

### Requirement 4

**User Story:** 作为开发者，我希望页索引管理正确，以便读写操作能够找到正确的页面数据

#### Acceptance Criteria

1. WHEN 页索引被保存时 THEN 应该写入到CCVFS_INDEX_TABLE_OFFSET固定位置
2. WHEN 页索引被加载时 THEN 应该从固定位置读取而不是动态计算位置
3. WHEN 文件关闭时 THEN 页索引和文件头应该被正确保存