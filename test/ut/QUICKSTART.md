# CCVFS 单元测试快速开始

## 1. 构建测试

```bash
# 在项目根目录
mkdir build && cd build
cmake ..
cmake --build . --target unit_tests
```

## 2. 运行基本测试（推荐先运行）

```bash
# 运行基本功能测试
ctest -R BasicTests

# 详细输出
ctest -R BasicTests -V
```

## 3. 运行所有测试

```bash
# 使用 CTest（推荐）
ctest

# 或直接运行
./test/ut/unit_tests
```

## 4. 运行特定测试套件

```bash
# 基本功能测试（推荐先运行）
ctest -R BasicTests

# 核心功能测试
ctest -R CoreTests

# 压缩功能测试
ctest -R CompressionTests

# 批量写入器测试
ctest -R BatchWriterTests

# 集成测试
ctest -R IntegrationTests
```

## 4. 查看详细输出

```bash
# 详细输出
ctest -V

# 失败时显示输出
ctest --output-on-failure

# 超详细输出
ctest -VV
```

## 5. 并行运行测试

```bash
# 使用4个并行作业
ctest -j4
```

## 6. 列出所有可用测试

```bash
# 列出测试但不运行
ctest -N

# 查看测试套件
./test/ut/unit_tests --list
```

## 7. 调试失败的测试

```bash
# 运行特定测试并显示详细输出
ctest -R CoreTests -V

# 直接运行测试可执行文件
./test/ut/unit_tests CCVFS_Core

# 使用调试器
gdb ./test/ut/unit_tests
(gdb) run CCVFS_Core
```

## 测试套件说明

- **BasicTests**: 基本功能验证，SQLite 和 VFS 基础测试（推荐先运行）
- **CoreTests**: VFS 核心功能，创建/销毁，基本操作
- **CompressionTests**: 压缩算法，页面大小，统计信息
- **BatchWriterTests**: 批量写入器配置，性能，统计
- **IntegrationTests**: 端到端测试，性能测试，错误恢复

## 常见问题

**Q: 测试失败怎么办？**
A: 使用 `ctest --output-on-failure` 查看失败详情

**Q: 如何只运行快速测试？**
A: 使用 `ctest -L basic` 运行基本测试，或 `ctest -R BasicTests`

**Q: 如何查看测试性能？**
A: 运行 `ctest -R IntegrationTests -V` 查看性能指标

**Q: 测试文件在哪里清理？**
A: 测试框架会自动清理临时文件