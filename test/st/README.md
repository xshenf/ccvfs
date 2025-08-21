# System Test Suite - Modular Structure

本目录包含CCVFS系统测试的模块化实现，提供了一个统一的测试入口点，替代了之前分散的多个测试程序。

## 文件结构

### 核心文件
- **`system_test_main.c`** - 统一测试入口点和命令行界面
- **`system_test_common.h`** - 通用数据结构和工具函数声明
- **`system_test_common.c`** - 通用工具函数实现
- **`system_test_functions.h`** - 所有测试函数的声明

### 测试模块
- **`test_basic.c`** - 基础测试
  - VFS连接测试
  - 简单数据库操作测试
  - 大型数据库压力测试
  - 简单大型数据测试

- **`test_storage.c`** - 存储和空洞检测测试
  - 综合空洞检测测试
  - 简单空洞管理测试

- **`test_buffer.c`** - 缓冲区管理测试
  - 批量写入缓冲区测试
  - 简单缓冲区操作测试

- **`test_tools.c`** - 工具集成测试
  - 数据库压缩/解压缩工具测试

### 构建配置
- **`CMakeLists.txt`** - CMake构建配置，包含CTest集成

## 使用方法

### 构建测试
```bash
cd cmake-build-debug
ninja system_tests
```

### 运行测试

#### 显示帮助信息
```bash
./test/st/system_tests.exe --help
```

#### 列出所有可用测试
```bash
./test/st/system_tests.exe --list
```

#### 运行单个测试
```bash
./test/st/system_tests.exe vfs_connection
./test/st/system_tests.exe hole_detection
./test/st/system_tests.exe batch_write_buffer
```

#### 运行所有测试
```bash
./test/st/system_tests.exe --all
```

#### 使用CTest运行
```bash
# 运行所有系统测试
ctest -R SystemTest

# 运行特定类别的测试
ctest -L Basic
ctest -L Storage
ctest -L Buffer
ctest -L Tools

# 运行特定测试
ctest -R SystemTest_VFS_Connection
```

## 测试分类

### Basic (基础测试)
- **SystemTest_VFS_Connection** - VFS连接和基本操作
- **SystemTest_Simple_DB** - 简单数据库操作和压缩

### Performance (性能测试)
- **SystemTest_Large_DB_Stress** - 大型数据库压力测试
- **SystemTest_Simple_Large** - 简单大型数据操作

### Storage (存储测试)
- **SystemTest_Hole_Detection** - 空洞检测功能
- **SystemTest_Simple_Hole** - 简单空洞管理

### Buffer (缓冲区测试)
- **SystemTest_Batch_Write_Buffer** - 批量写入缓冲区功能
- **SystemTest_Simple_Buffer** - 简单缓冲区操作

### Tools (工具测试)
- **SystemTest_DB_Tools** - 数据库工具集成

### Integration (集成测试)
- **SystemTest_All** - 运行所有测试的综合测试

## 测试结果

每个测试返回以下信息：
- **测试名称和描述**
- **通过/总计的步骤数** (例如: 6/6 passed)
- **详细消息** (记录数量、错误信息等)
- **最终状态** (PASS/FAIL)

## 优势

### 模块化设计
- **易于维护**: 每个功能域有独立的源文件
- **清晰结构**: 通用功能和特定测试分离
- **易于扩展**: 添加新测试只需修改相应模块

### 统一入口
- **单一可执行文件**: 替代多个分散的测试程序
- **命令行界面**: 灵活的测试执行选项
- **CTest集成**: 与标准测试框架完全兼容

### 分类管理
- **逻辑分组**: 按功能域组织测试
- **选择性执行**: 可以运行特定类别或单个测试
- **并行支持**: CTest支持并行测试执行

这种模块化结构使得系统测试更加易于维护、扩展和使用，同时保持了所有原有的测试功能。
