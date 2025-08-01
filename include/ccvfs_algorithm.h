#ifndef CCVFS_ALGORITHM_H
#define CCVFS_ALGORITHM_H

#include "ccvfs_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Algorithm management functions
 */
CompressAlgorithm* ccvfs_find_compress_algorithm(const char *name);
EncryptAlgorithm* ccvfs_find_encrypt_algorithm(const char *name);
void ccvfs_init_builtin_algorithms(void);

/*
 * Algorithm listing functions
 */
int ccvfs_list_compress_algorithms(char *buffer, int buffer_size);
int ccvfs_list_encrypt_algorithms(char *buffer, int buffer_size);

/*
 * Global algorithm registries (declared here, defined in ccvfs_algorithm.c)
 */
extern CompressAlgorithm *g_compress_algorithms[CCVFS_MAX_ALGORITHMS];
extern EncryptAlgorithm *g_encrypt_algorithms[CCVFS_MAX_ALGORITHMS];
extern int g_compress_algorithm_count;
extern int g_encrypt_algorithm_count;
extern int g_algorithms_initialized;

#ifdef __cplusplus
}
#endif

#endif /* CCVFS_ALGORITHM_H */