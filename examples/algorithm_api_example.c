/*
 * CCVFS Algorithm API Usage Examples
 * 
 * 展示如何使用新的基于算法结构体的API，以及它相比字符串API的优势
 */

#include "ccvfs.h"
#include <stdio.h>
#include <string.h>

// 自定义压缩算法示例
static int custom_compress(const unsigned char *input, int input_len,
                          unsigned char *output, int output_len, int level) {
    // 简单的运行长度编码示例
    if (output_len < input_len * 2) return -1; // 最坏情况
    
    int out_pos = 0;
    int i = 0;
    
    while (i < input_len) {
        unsigned char current = input[i];
        int count = 1;
        
        // 计算连续相同字符的数量
        while (i + count < input_len && input[i + count] == current && count < 255) {
            count++;
        }
        
        if (out_pos + 2 >= output_len) return -1;
        output[out_pos++] = (unsigned char)count;
        output[out_pos++] = current;
        
        i += count;
    }
    
    return out_pos;
}

static int custom_decompress(const unsigned char *input, int input_len,
                            unsigned char *output, int output_len) {
    int in_pos = 0;
    int out_pos = 0;
    
    while (in_pos + 1 < input_len) {
        unsigned char count = input[in_pos++];
        unsigned char value = input[in_pos++];
        
        if (out_pos + count > output_len) return -1;
        
        for (int i = 0; i < count; i++) {
            output[out_pos++] = value;
        }
    }
    
    return out_pos;
}

static int custom_get_max_compressed_size(int input_len) {
    return input_len * 2; // 最坏情况下每个字节变成两个字节
}

// 自定义算法结构体
static CompressAlgorithm custom_rle_algorithm = {
    "custom_rle",
    custom_compress,
    custom_decompress,
    custom_get_max_compressed_size
};

void example_builtin_algorithms() {
    printf("=== 使用内置算法示例 ===\n");
    
    // 使用新的统一API
    printf("使用统一的算法结构体API:\n");
#ifdef HAVE_ZLIB
    int rc = sqlite3_ccvfs_create("vfs_algorithm", NULL, 
                                  CCVFS_COMPRESS_ZLIB, 
                                  NULL,  // 无加密
                                  0, CCVFS_CREATE_REALTIME);
    if (rc == SQLITE_OK) {
        printf("   使用ZLIB压缩算法创建VFS成功\n");
        sqlite3_ccvfs_destroy("vfs_algorithm");
    }
#else
    printf("   ZLIB未编译，使用无压缩算法\n");
    int rc = sqlite3_ccvfs_create("vfs_algorithm", NULL, 
                                  NULL,  // 无压缩
                                  NULL,  // 无加密
                                  0, CCVFS_CREATE_REALTIME);
    if (rc == SQLITE_OK) {
        printf("   使用无压缩算法创建VFS成功\n");
        sqlite3_ccvfs_destroy("vfs_algorithm");
    }
#endif
}

void example_custom_algorithm() {
    printf("\n=== 使用自定义算法示例 ===\n");
    
    printf("直接传递算法结构体:\n");
    int rc = sqlite3_ccvfs_create("vfs_custom_direct", NULL, 
                                  &custom_rle_algorithm, 
                                  NULL,  // 无加密
                                  0, CCVFS_CREATE_REALTIME);
    if (rc == SQLITE_OK) {
        printf("   创建VFS成功\n");
        sqlite3_ccvfs_destroy("vfs_custom_direct");
    }
}

void example_performance_comparison() {
    printf("\n=== 算法结构体API性能测试 ===\n");
    
    clock_t start, end;
    const int iterations = 10000;
    
    // 算法结构体API性能测试
    printf("算法结构体API性能测试 (%d次创建/销毁):\n", iterations);
    start = clock();
    for (int i = 0; i < iterations; i++) {
        char vfs_name[64];
        snprintf(vfs_name, sizeof(vfs_name), "test_vfs_%d", i);
#ifdef HAVE_ZLIB
        sqlite3_ccvfs_create(vfs_name, NULL, CCVFS_COMPRESS_ZLIB, 
                            NULL, 0, CCVFS_CREATE_REALTIME);
#else
        sqlite3_ccvfs_create(vfs_name, NULL, NULL, 
                            NULL, 0, CCVFS_CREATE_REALTIME);
#endif
        sqlite3_ccvfs_destroy(vfs_name);
    }
    end = clock();
    double algorithm_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("   算法结构体API耗时: %.3f秒\n", algorithm_time);
    
    printf("   性能优化: 直接指针访问，无字符串查找开销\n");
}

int main() {
    printf("CCVFS 算法API使用示例\n");
    printf("======================\n");
    
    example_builtin_algorithms();
    example_custom_algorithm();
    example_performance_comparison();
    
    printf("\n=== 新API的优势总结 ===\n");
    printf("1. 类型安全: 编译时检查，避免运行时错误\n");
    printf("2. 性能优化: 无需字符串查找，直接指针访问\n");
    printf("3. 内存节省: 不需要存储算法名称字符串\n");
    printf("4. 易于扩展: 用户可直接传递自定义算法结构体\n");
    printf("5. 设计简洁: 无需全局注册表，直接使用\n");
    
    return 0;
}