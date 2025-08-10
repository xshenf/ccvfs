# CCVFS Unit Test Suite

这是 CCVFS (Compressed and Cached Virtual File System) 项目的完整单元测试套件。

## 概述

单元测试套件包含以下测试模块：

### 1. 核心功能测试 (CCVFS_Core)
- VFS 创建和销毁
- 参数验证
- 基本数据库操作
- 错误处理
- 多 VFS 实例管理

### 2. 压缩功能测试 (Compression)
- 基本压缩和解压缩
- 不同压缩算法 (zlib, lz4, rle)
- 各种页面大小
- 压缩级别测试
- 统计信息和错误处理

### 3. 批量写入器测试 (Batch_Writer)
- 配置和基本操作
- 统计信息和刷新操作
- 不同配置组合
- 性能特性
- 错误处理

### 4. 集成测试 (Integration)
- 端到端工作流测试
- 实时压缩与批量写入器
- 混合读写操作
- 错误恢复和一致性
- 负载下的性能测试

## 构建和运行

### 构建测试

```bash
# 在项目根目录
mkdir build && cd build
cmake ..
make unit_tests
```

### 使用 CTest 运行测试

```bash
# 运行所有测试
ctest

# 运行特定测试套件
ctest -R CoreTests           # 核心功能测试
ctest -R CompressionTests    # 压缩功能测试
ctest -R BatchWriterTests    # 批量写入器测试
ctest -R IntegrationTests    # 集成测试
ctest -R AllUnitTests        # 所有单元测试

# 运行带标签的测试
ctest -L unit                # 所有单元测试
ctest -L core                # 核心测试
ctest -L compression         # 压缩测试
ctest -L batch               # 批量写入器测试
ctest -L integration         # 集成测试

# 详细输出
ctest -V                     # 详细输出
ctest --output-on-failure    # 失败时显示输出
ctest -VV                    # 超详细输出

# 并行运行测试
ctest -j4                    # 使用4个并行作业

# 运行特定测试并显示输出
ctest -R CoreTests -V
```

### 直接运行测试可执行文件

```bash
# 运行所有测试套件
./test/ut/unit_tests

# 运行特定测试套件
./test/ut/unit_tests CCVFS_Core
./test/ut/unit_tests Compression
./test/ut/unit_tests Batch_Writer
./test/ut/unit_tests Integration

# 查看可用测试套件
./test/ut/unit_tests --list

# 显示帮助信息
./test/ut/unit_tests --help
```

## 测试框架特性

### 断言宏
- `TEST_ASSERT(condition, message)` - 基本断言
- `TEST_ASSERT_EQ(expected, actual, message)` - 相等断言
- `TEST_ASSERT_STR_EQ(expected, actual, message)` - 字符串相等断言
- `TEST_ASSERT_NULL(ptr, message)` - 空指针断言
- `TEST_ASSERT_NOT_NULL(ptr, message)` - 非空指针断言

### 测试控制宏
- `TEST_START(test_name)` - 开始测试
- `TEST_END()` - 结束测试
- `TEST_SKIP(message)` - 跳过测试

### 颜色输出
测试框架支持彩色输出，使测试结果更易读：
- 🧪 蓝色：测试开始
- ✅ 绿色：测试通过
- ❌ 红色：测试失败
- ⏭️ 黄色：测试跳过
- 🎉 绿色：所有测试通过
- 💥 红色：有测试失败

## 测试文件结构

```
test/ut/
├── test_framework.h          # 测试框架头文件
├── test_framework.c          # 测试框架实现
├── test_ccvfs_core.c         # 核心功能测试
├── test_compression.c        # 压缩功能测试
├── test_batch_writer.c       # 批量写入器测试
├── test_integration.c        # 集成测试
├── main.c                    # 主程序入口
├── CMakeLists.txt           # CMake 配置
└── README.md                # 本文档
```

## 测试数据管理

测试框架自动管理测试文件：
- 每个测试套件有独立的 setup/teardown 函数
- 自动清理临时测试文件
- 避免测试间的相互影响

## 性能测试

集成测试包含性能基准测试：
- 压缩/解压缩性能
- 批量写入性能
- 混合操作性能
- 负载测试

## 错误处理测试

全面的错误处理测试：
- 无效参数处理
- 文件系统错误
- 内存不足情况
- 数据损坏恢复

## 扩展测试

要添加新的测试：

1. 在相应的测试文件中添加测试函数
2. 使用 `REGISTER_TEST_CASE` 注册测试
3. 重新构建并运行测试

示例：
```c
int test_new_feature(void) {
    TEST_START("New Feature Test");
    
    // 测试代码
    TEST_ASSERT(condition, "Feature works correctly");
    
    TEST_END();
    return 1;
}

// 在注册函数中添加
REGISTER_TEST_CASE("CCVFS_Core", "New Feature Test", test_new_feature);
```

## 持续集成

测试套件设计用于持续集成环境：
- 返回适当的退出代码
- 支持超时设置
- 生成详细的测试报告
- 支持并行测试执行

## 故障排除

### 常见问题

1. **编译错误**：确保所有依赖库已安装
2. **测试失败**：检查文件权限和磁盘空间
3. **性能测试不稳定**：在专用测试环境中运行

### 调试测试

```bash
# 使用 gdb 调试
gdb ./test/ut/unit_tests
(gdb) run CCVFS_Core

# 使用 valgrind 检查内存泄漏
valgrind --leak-check=full ./test/ut/unit_tests
```

## 贡献

欢迎贡献新的测试用例和改进：
1. 遵循现有的测试模式
2. 添加适当的文档
3. 确保测试的独立性
4. 包含错误处理测试