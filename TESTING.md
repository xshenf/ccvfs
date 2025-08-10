# CCVFS 测试指南

本文档描述了 CCVFS (Compressed and Cached Virtual File System) 项目的测试策略和使用方法。

## 测试架构

### 测试类型

1. **单元测试** (`test/ut/`) - 全新的结构化单元测试套件
2. **集成测试** (`test/`) - 现有的集成和功能测试
3. **性能测试** - 包含在单元测试和集成测试中

### 单元测试套件

位于 `test/ut/` 目录下的完整单元测试框架，包含：

#### 测试模块
- **CCVFS_Core**: VFS 核心功能测试
- **Compression**: 压缩和解压缩功能测试  
- **Batch_Writer**: 批量写入器功能测试
- **Integration**: 端到端集成测试

#### 测试框架特性
- 彩色输出和详细报告
- 自动测试文件管理
- 性能基准测试
- 错误处理验证
- 内存泄漏检测支持

## 快速开始

### 1. 验证测试框架

```bash
cd test/ut
./verify_framework.sh
```

### 2. 构建和运行所有测试

```bash
# Linux/macOS
cd test/ut
./run_tests.sh -b

# Windows
cd test\ut
run_tests.bat -b
```

### 3. 运行特定测试套件

```bash
# 运行核心功能测试
./run_tests.sh CCVFS_Core

# 运行压缩测试
./run_tests.sh Compression

# 运行批量写入器测试
./run_tests.sh Batch_Writer

# 运行集成测试
./run_tests.sh Integration
```

## 详细使用方法

### 构建系统集成

项目使用 CMake 构建系统，测试已完全集成：

```bash
# 在项目根目录
mkdir build && cd build
cmake ..

# 构建单元测试
make unit_tests

# 使用 CTest 运行测试
ctest                        # 运行所有测试
ctest -L unit               # 运行单元测试
ctest -R CoreTests          # 运行核心测试
ctest -R CompressionTests   # 运行压缩测试
ctest -R BatchWriterTests   # 运行批量写入器测试
ctest -R IntegrationTests   # 运行集成测试
ctest -V                    # 详细输出
ctest --output-on-failure   # 失败时显示输出
ctest -j4                   # 并行运行测试
```

### CTest 常用选项

```bash
# 基本运行
ctest                        # 运行所有测试
ctest -N                     # 列出所有测试但不运行

# 选择性运行
ctest -R <regex>             # 运行匹配正则表达式的测试
ctest -L <label>             # 运行带有指定标签的测试
ctest -E <regex>             # 排除匹配正则表达式的测试

# 输出控制
ctest -V                     # 详细输出
ctest -VV                    # 超详细输出
ctest --output-on-failure    # 仅在失败时显示输出
ctest -Q                     # 安静模式

# 并行和性能
ctest -j<N>                  # 使用N个并行作业
ctest --timeout <seconds>    # 设置测试超时

# 重复和调试
ctest --repeat until-fail:N  # 重复运行直到失败，最多N次
ctest --repeat until-pass:N  # 重复运行直到通过，最多N次
```

### 测试输出示例

```
🚀 Running All Test Suites
===========================

📦 Running Test Suite: CCVFS_Core
----------------------------------------
🧪 Running: VFS Creation and Destruction
✅ PASS: Default VFS found
✅ PASS: CCVFS created successfully
✅ PASS: CCVFS found after creation
✅ PASS: CCVFS destroyed successfully
✅ PASS: CCVFS not found after destruction
   Test completed

📊 Final Test Summary
=====================
Total Tests:   25
Passed:        25
Failed:        0
Skipped:       0
Total Time:    2.345 seconds
Success Rate:  100.0%

🎉 All tests passed!
```

## 开发工作流

### 添加新测试

1. 在相应的测试文件中添加测试函数
2. 使用测试框架宏进行断言
3. 注册测试用例

```c
int test_new_feature(void) {
    TEST_START("New Feature Test");
    
    // 测试代码
    TEST_ASSERT(condition, "Feature works correctly");
    TEST_ASSERT_EQ(expected, actual, "Values match");
    
    TEST_END();
    return 1;
}

// 在注册函数中添加
REGISTER_TEST_CASE("CCVFS_Core", "New Feature", test_new_feature);
```

### 调试测试

```bash
# 使用 GDB 调试
gdb ./test/ut/unit_tests
(gdb) run CCVFS_Core

# 使用 Valgrind 检查内存
valgrind --leak-check=full ./test/ut/unit_tests CCVFS_Core

# CTest 详细输出
ctest -R CoreTests -V
ctest -R CoreTests -VV

# 直接运行测试获得详细输出
./test/ut/unit_tests CCVFS_Core -v
```

### 性能测试

集成测试包含性能基准：

```bash
# 运行性能测试
ctest -R IntegrationTests -V

# 查看性能指标
ctest -R IntegrationTests -V | grep "seconds\|bytes\|ops/sec"

# 直接运行集成测试
./test/ut/unit_tests Integration
```

## 持续集成

### GitHub Actions 示例

```yaml
name: Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    - name: Install dependencies
      run: sudo apt-get install -y zlib1g-dev liblz4-dev liblzma-dev
    - name: Build and test
      run: |
        mkdir build && cd build
        cmake ..
        make unit_tests
        ctest --output-on-failure
    - name: Run specific test suites
      run: |
        cd build
        ctest -R CoreTests -V
        ctest -R CompressionTests -V
```

### 覆盖率报告

```bash
# 生成覆盖率报告（需要 lcov）
./run_tests.sh --coverage

# 查看报告
open build/coverage_report/index.html
```

## 测试最佳实践

### 1. 测试独立性
- 每个测试应该独立运行
- 使用 setup/teardown 函数清理状态
- 避免测试间的依赖关系

### 2. 错误处理
- 测试正常路径和错误路径
- 验证错误代码和消息
- 测试边界条件

### 3. 性能考虑
- 包含性能基准测试
- 监控内存使用
- 测试大数据集场景

### 4. 可维护性
- 使用描述性的测试名称
- 添加适当的注释
- 保持测试代码简洁

## 故障排除

### 常见问题

1. **编译错误**
   - 检查依赖库是否安装
   - 确认 CMake 版本兼容性

2. **测试失败**
   - 检查文件权限
   - 确认磁盘空间充足
   - 查看详细错误信息

3. **性能测试不稳定**
   - 在专用测试环境运行
   - 多次运行取平均值
   - 检查系统负载

### 获取帮助

```bash
# CTest 帮助
ctest --help

# 列出所有测试
ctest -N

# 查看测试可执行文件帮助
./test/ut/unit_tests --help

# 列出可用测试套件
./test/ut/unit_tests --list
```

## 贡献指南

### 提交测试

1. 确保所有测试通过
2. 添加适当的测试覆盖
3. 更新相关文档
4. 遵循现有代码风格

### 代码审查

- 检查测试覆盖率
- 验证错误处理
- 确认性能影响
- 审查测试质量

---

更多详细信息请参考：
- [单元测试 README](test/ut/README.md)
- [项目 README](README.md)
- [API 文档](docs/)