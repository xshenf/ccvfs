# SQLite 压缩加密 VFS 设计文档

## 1. 概述

本文档详细描述了 SQLite 压缩加密 VFS 扩展的设计和实现方案。该扩展通过实现 SQLite 的虚拟文件系统 (VFS) 接口，提供对数据库文件的透明压缩和加密功能。

有关压缩后文件的内部结构和读取原理，请参阅[压缩文件格式说明](COMPRESSED_FILE_FORMAT.md)。

## 2. 系统架构

### 2.1 整体架构

```
+---------------------+
|   SQLite 引擎       |
+----------+----------+
           |
+----------v----------+
|   CCVFS             | <- 自定义VFS实现
+----------+----------+
           |
+----------v----------+
|   Codec Layer       | <- 编解码层（压缩/解密，加密/解压）
+----------+----------+
           |
+----------v----------+
|   系统默认 VFS       | <- 底层系统VFS
+---------------------+
```

### 2.2 核心组件

1. **VFS 层**：实现 `sqlite3_vfs` 结构体，拦截所有文件操作
2. **IO 层**：实现 `sqlite3_io_methods` 结构体，处理文件读写操作
3. **编解码层**：执行压缩/解密和加密/解压操作
4. **算法管理层**：管理和调度各种压缩/加密算法

## 3. 数据结构设计

### 3.1 CCVFS 结构体

```c
typedef struct CCVFS {
    sqlite3_vfs base;           /* 基础VFS结构 */
    sqlite3_vfs *pRootVfs;      /* 底层VFS */
    char *zCompressType;        /* 压缩算法类型 */
    char *zEncryptType;         /* 加密算法类型 */
    CompressAlgorithm *pCompressAlg; /* 压缩算法实现 */
    EncryptAlgorithm *pEncryptAlg;   /* 加密算法实现 */
} CCVFS;
```

### 3.2 CCVFSFile 结构体

```c
typedef struct CCVFSFile {
    sqlite3_file base;          /* 基础文件结构 */
    sqlite3_file *pReal;        /* 实际的文件指针 */
    CCVFS *pOwner;              /* 拥有此文件的VFS */
} CCVFSFile;
```

## 4. 核心功能实现

### 4.1 VFS 注册与管理

#### 4.1.1 创建 VFS

`sqlite3_ccvfs_create` 函数负责创建并注册一个新的 VFS：

1. 检查是否已存在同名 VFS
2. 分配并初始化 [CCVFS](file:///E:/workspace/cesqlite/DESIGN.md#L45-L52) 结构体
3. 根据算法名称查找对应的算法实现
4. 设置 VFS 方法指针
5. 调用 `sqlite3_vfs_register` 注册 VFS

#### 4.1.2 销毁 VFS

`sqlite3_ccvfs_destroy` 函数负责注销并销毁 VFS：

1. 查找指定名称的 VFS
2. 调用 `sqlite3_vfs_unregister` 注销 VFS
3. 释放内存资源

### 4.2 算法管理

#### 4.2.1 算法注册

通过 `sqlite3_ccvfs_register_compress_algorithm` 和 `sqlite3_ccvfs_register_encrypt_algorithm` 注册自定义算法：

1. 维护全局算法列表
2. 支持按名称查找算法
3. 允许覆盖同名算法实现

#### 4.2.2 算法查找

在创建 VFS 时根据算法名称查找对应的实现：

1. 遍历算法列表查找匹配项
2. 返回算法接口指针

### 4.3 文件操作拦截

#### 4.3.1 文件打开 (xOpen)

1. 调用底层 VFS 的 `xOpen` 方法打开实际文件
2. 创建 [CCVFSFile](file:///E:/workspace/cesqlite/DESIGN.md#L54-L58) 结构体包装实际文件
3. 设置 IO 方法指针

#### 4.3.2 文件读取 (xRead)

读取操作流程：

1. 根据偏移量和长度计算实际需要读取的数据块
2. 调用底层 VFS 读取压缩加密的数据
3. 对读取的数据进行解密
4. 对解密后的数据进行解压缩
5. 将解压缩后的数据返回给调用者

#### 4.3.3 文件写入 (xWrite)

写入操作流程：

1. 对待写入数据进行压缩
2. 对压缩后的数据进行加密
3. 计算写入位置，调用底层 VFS 写入加密压缩数据
4. 更新文件元数据

### 4.4 编解码处理

#### 4.4.1 数据块管理

为了高效处理压缩和加密，我们将数据组织成固定大小的块：

1. 默认块大小为 8KB（可配置）
2. 每个块独立进行压缩和加密处理
3. 维护块索引以支持随机访问

#### 4.4.2 压缩处理

压缩流程：

1. 将输入数据分块处理
2. 对每个数据块调用压缩算法
3. 存储压缩后的数据长度信息
4. 将压缩数据传递给加密层

#### 4.4.3 加密处理

加密流程：

1. 对压缩后的数据进行加密
2. 添加消息认证码 (MAC) 保证数据完整性
3. 存储加密相关元数据

#### 4.4.4 解密处理

解密流程：

1. 验证数据完整性 (检查 MAC)
2. 对加密数据进行解密
3. 将解密后的数据传递给解压缩层

#### 4.4.5 解压缩处理

解压缩流程：

1. 解析数据块头部获取原始长度信息
2. 对压缩数据进行解压缩
3. 将解压缩后的数据返回给调用者

## 5. 算法接口实现

### 5.1 压缩算法接口

```c
typedef struct {
    const char *name;
    int (*compress)(const unsigned char *input, int input_len, 
                   unsigned char *output, int output_len);
    int (*decompress)(const unsigned char *input, int input_len,
                     unsigned char *output, int output_len);
} CompressAlgorithm;
```

### 5.2 加密算法接口

```c
typedef struct {
    const char *name;
    int (*encrypt)(const unsigned char *key, int key_len,
                  const unsigned char *input, int input_len,
                  unsigned char *output, int output_len);
    int (*decrypt)(const unsigned char *key, int key_len,
                  const unsigned char *input, int input_len,
                  unsigned char *output, int output_len);
} EncryptAlgorithm;
```

## 6. 内置算法实现

### 6.1 RLE 压缩算法

简单实现 Run-Length Encoding 算法：

1. 遍历输入数据，查找连续重复字节
2. 对于重复超过一定次数的字节序列，用"字节+计数"的形式表示
3. 对于非重复数据，直接存储

### 6.2 XOR 简单加密算法

实现简单的 XOR 加密：

1. 使用用户提供的密钥
2. 循环使用密钥对数据进行 XOR 操作
3. 解密过程与加密过程相同

### 6.3 数据块格式

每个数据块包含以下信息：

```
+------------------+------------------+------------------+------------------+
|  块头部 (16B)    |  压缩数据长度(4B)|  原始数据长度(4B)|  压缩加密数据    |
+------------------+------------------+------------------+------------------+
```

块头部包含：
- 魔数标识 (4B)
- 块序列号 (4B)
- 校验和 (4B)
- 标志位 (4B)

## 7. 错误处理与恢复

### 7.1 错误检测

1. 数据完整性校验 (CRC32)
2. 块序列号验证
3. 数据长度验证

### 7.2 错误恢复

1. 损坏块隔离，不影响其他块读取
2. 提供损坏数据报告机制
3. 支持从备份恢复

## 8. 性能优化策略

### 8.1 缓存机制

1. 实现 LRU 缓存，缓存最近访问的数据块
2. 缓存解压缩和解密后的明文数据
3. 缓存常用算法的上下文状态

### 8.2 并发处理

1. 支持多线程并发读取不同数据块
2. 对写入操作进行序列化处理
3. 使用读写锁提高并发性能

### 8.3 内存管理

1. 预分配固定大小的缓冲区
2. 实现内存池减少频繁分配释放
3. 及时释放临时缓冲区

## 9. 安全性设计

### 9.1 密钥管理

1. 支持外部密钥管理系统
2. 实现密钥派生函数 (KDF)
3. 支持密钥轮换

### 9.2 抗篡改设计

1. 实现消息认证码 (MAC)
2. 定期验证数据完整性
3. 检测到篡改时拒绝访问

### 9.3 安全擦除

1. 删除文件时安全擦除数据
2. 内存中的敏感数据及时清除

## 10. 配置与定制

### 10.1 编译时配置

通过宏定义控制功能：

```c
#define ENABLE_RLE_COMPRESSION 1
#define ENABLE_XOR_ENCRYPTION 1
#define DEFAULT_BLOCK_SIZE 8192
```

### 10.2 运行时配置

通过 URI 参数或 PRAGMA 配置：

```sql
PRAGMA cipher_algorithm = 'aes-256';
PRAGMA compression_algorithm = 'zlib';
```

## 11. 测试策略

### 11.1 单元测试

1. 算法正确性测试
2. 边界条件测试
3. 错误处理测试

### 11.2 集成测试

1. 与 SQLite 集成测试
2. 性能基准测试
3. 兼容性测试

### 11.3 压力测试

1. 大数据量读写测试
2. 并发访问测试
3. 长时间运行稳定性测试

## 12. 部署与维护

### 12.1 部署方式

1. 动态库方式加载
2. 静态编译到应用程序
3. SQLite 扩展方式

### 12.2 升级策略

1. 向后兼容性保证
2. 数据格式版本管理
3. 平滑升级机制

## 13. 限制与约束

### 13.1 技术限制

1. 不支持 WAL 模式下的在线备份
2. 对某些 SQLite 特性可能存在兼容性问题
3. 性能开销取决于选择的算法

### 13.2 使用约束

1. 需要妥善保管加密密钥
2. 压缩效果取决于数据特征
3. 不适用于已加密的数据库文件

## 14. 未来扩展

### 14.1 功能扩展

1. 支持更多压缩/加密算法
2. 实现增量备份功能
3. 添加数据压缩统计信息

### 14.2 性能优化

1. 实现异步IO处理
2. 优化多核并行处理
3. 添加智能缓存预取

---