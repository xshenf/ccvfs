# SQLite 压缩加密 VFS 扩展

## 简介

这是一个 SQLite 的虚拟文件系统 (VFS) 扩展，支持对数据库文件进行透明的压缩和加密。通过这个扩展，SQLite 可以直接读写经过压缩和加密的数据库文件，而无需在应用层进行额外处理。

## 功能特性

- 透明压缩：自动压缩数据库文件，节省磁盘空间
- 透明加密：自动加密数据库文件，保护数据安全
- 可插拔架构：支持自定义压缩和加密算法
- 兼容性强：与标准 SQLite 完全兼容
- 高性能：优化的数据处理流程，降低性能损耗

## 设计架构

### VFS 层次结构

```
+---------------------+
|   SQLite 引擎       |
+----------+----------+
           |
+----------v----------+
|   CCVFS             | <- 本项目实现的VFS
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

### 核心组件

1. **VFS 层**：拦截 SQLite 的文件操作请求
2. **IO 方法层**：处理实际的文件读写操作
3. **编解码层**：执行压缩和解密/加密操作
4. **算法接口层**：支持自定义算法插件

## 使用方法

### 基本用法

```c
#include "compress_vfs.h"

int main() {
    // 注册压缩加密VFS
    sqlite3_ccvfs_create("ccvfs", NULL, "zlib", "aes-256");
    
    // 使用压缩加密VFS打开数据库
    sqlite3 *db;
    sqlite3_open_v2("test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "ccvfs");
    
    // 正常使用SQLite API
    // ...
    
    // 关闭数据库
    sqlite3_close(db);
    
    // 注销VFS
    sqlite3_ccvfs_destroy("ccvfs");
    
    return 0;
}
```

### 自定义算法

#### 自定义压缩算法

```c
// 定义压缩算法接口
typedef struct {
    const char *name;
    int (*compress)(const unsigned char *input, int input_len, 
                   unsigned char *output, int output_len);
    int (*decompress)(const unsigned char *input, int input_len,
                     unsigned char *output, int output_len);
} CompressAlgorithm;

// 实现自定义算法
static int my_compress(const unsigned char *input, int input_len, 
                      unsigned char *output, int output_len) {
    // 实现压缩逻辑
    return compressed_size;
}

static int my_decompress(const unsigned char *input, int input_len,
                        unsigned char *output, int output_len) {
    // 实现解压缩逻辑
    return decompressed_size;
}

// 注册自定义算法
static CompressAlgorithm my_algorithm = {
    "my_algorithm",
    my_compress,
    my_decompress
};

// 在创建VFS时使用自定义算法
sqlite3_ccvfs_create("ccvfs", NULL, "my_algorithm", "aes-256");
```

#### 自定义加密算法

```c
// 定义加密算法接口
typedef struct {
    const char *name;
    int (*encrypt)(const unsigned char *key, int key_len,
                  const unsigned char *input, int input_len,
                  unsigned char *output, int output_len);
    int (*decrypt)(const unsigned char *key, int key_len,
                  const unsigned char *input, int input_len,
                  unsigned char *output, int output_len);
} EncryptAlgorithm;

// 实现自定义算法
static int my_encrypt(const unsigned char *key, int key_len,
                     const unsigned char *input, int input_len,
                     unsigned char *output, int output_len) {
    // 实现加密逻辑
    return encrypted_size;
}

static int my_decrypt(const unsigned char *key, int key_len,
                     const unsigned char *input, int input_len,
                     unsigned char *output, int output_len) {
    // 实现解密逻辑
    return decrypted_size;
}

// 注册自定义算法
static EncryptAlgorithm my_encrypt_algorithm = {
    "my_encrypt",
    my_encrypt,
    my_decrypt
};
```

## 调试模式

CCVFS支持调试模式，可以在开发和故障排除时提供详细的运行信息。

### 启用调试模式

在构建项目时添加以下CMake选项：

```bash
mkdir build
cd build
cmake -DENABLE_DEBUG=ON -DENABLE_VERBOSE=ON ..
make
```

这将启用以下调试宏：
- `DEBUG`: 启用基本调试信息
- `VERBOSE`: 启用详细调试信息

### 调试信息级别

CCVFS提供了多个调试信息级别：

1. **ERROR**: 错误信息，始终显示
2. **INFO**: 一般信息，始终显示
3. **DEBUG**: 调试信息，仅在启用DEBUG时显示
4. **VERBOSE**: 详细调试信息，仅在启用VERBOSE时显示

## 文件格式

有关压缩后文件的内部结构和读取原理，请参阅[压缩文件格式说明](COMPRESSED_FILE_FORMAT.md)。

## 内置算法支持

### 压缩算法

1. **RLE (Run-Length Encoding)**
   - 最简单的压缩算法
   - 适合有大量重复数据的场景

2. **LZ4**
   - 高性能压缩算法
   - 压缩比适中，速度极快

3. **Zlib**
   - 广泛使用的压缩算法
   - 压缩比较高，速度中等

### 加密算法

1. **AES-128**
   - 128位密钥长度
   - 安全性较高，性能良好

2. **AES-256**
   - 256位密钥长度
   - 更高的安全性

3. **ChaCha20**
   - 现代流加密算法
   - 在移动设备上性能优异

## 性能考虑

1. **压缩率 vs 性能**：高压缩率算法通常需要更多CPU时间
2. **加密开销**：加密会增加CPU开销，但提供数据安全保障
3. **块大小优化**：合理设置数据块大小以平衡内存使用和性能
4. **缓存策略**：实现智能缓存减少重复编解码操作

## 安全性说明

1. **密钥管理**：应用程序负责密钥的安全存储和管理
2. **数据完整性**：内置数据完整性校验机制
3. **防篡改**：检测到数据被篡改时会报错
4. **安全删除**：支持安全删除敏感数据

## 编译和安装

### 使用 CMake 构建

```bash
mkdir build
cd build
cmake ..
make
```

### 依赖项

- SQLite3 开发库
- 可选：zlib、lz4、openssl 等算法库

## API 参考

### 核心函数

```c
// 创建压缩加密VFS
int sqlite3_ccvfs_create(
    const char *zVfsName,      // VFS名称
    sqlite3_vfs *pRootVfs,     // 底层VFS（NULL表示使用默认VFS）
    const char *zCompressType, // 压缩算法类型
    const char *zEncryptType   // 加密算法类型
);

// 销毁压缩加密VFS
int sqlite3_ccvfs_destroy(const char *zVfsName);

// 注册自定义压缩算法
int sqlite3_ccvfs_register_compress_algorithm(CompressAlgorithm *algorithm);

// 注册自定义加密算法
int sqlite3_ccvfs_register_encrypt_algorithm(EncryptAlgorithm *algorithm);
```

## 限制和注意事项

1. 不支持在同一个数据库文件上同时使用不同的VFS
2. 加密数据库不能直接用普通SQLite工具打开
3. 压缩和加密会增加CPU使用率
4. 需要妥善保管加密密钥
5. 某些SQLite特性可能不完全兼容（如在线备份）

## 故障排除

### 常见问题

1. **数据库无法打开**：检查算法库是否正确链接
2. **性能下降**：评估压缩/加密算法是否适合当前数据类型
3. **内存占用高**：调整内部缓冲区大小

### 错误代码

- SQLITE_OK (0): 操作成功
- SQLITE_ERROR (1): 一般错误
- SQLITE_NOMEM (7): 内存不足
- SQLITE_MISUSE (21): API使用错误

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE) 文件。

## 贡献

欢迎提交 Issue 和 Pull Request 来改进这个项目。