@echo off
echo 使用CMake构建项目...
mkdir build 2>nul
cd build
cmake ..
if %errorlevel% neq 0 (
    echo CMake配置失败
    exit /b %errorlevel%
)

cmake --build .
if %errorlevel% neq 0 (
    echo 构建失败
    exit /b %errorlevel%
)

echo 构建完成
echo 运行测试程序: build\bin\test_compressvfs.exe
exit /b 0

:makefile_build
echo 使用传统方式构建...
REM 设置编译器路径（根据实际安装情况调整）
set CC=gcc

REM 创建输出目录
if not exist "lib" mkdir lib
if not exist "bin" mkdir bin

REM 编译源文件
%CC% -c src/compress_vfs.c -I./include -I./sqlite3 -Wall -Wextra -std=c99 -O2 -o lib/compress_vfs.o

REM 创建动态链接库
%CC% -shared -o lib/compressvfs.dll lib/compress_vfs.o -lsqlite3

REM 编译测试程序
%CC% test/main.c -I./include -I./sqlite3 -L./lib -lsqlite3 -lcompressvfs -o bin/test.exe

echo 传统方式构建完成
echo 运行测试程序: bin\test.exe
exit /b 0