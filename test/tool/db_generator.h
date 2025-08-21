#ifndef DB_GENERATOR_H
#define DB_GENERATOR_H

#include "ccvfs.h"
#include <stdint.h>

/*
 * Database generator interface - Create databases of arbitrary size
 * Supports both compressed and uncompressed database generation
 */

#ifdef __cplusplus
extern "C" {
#endif

// Data generation modes
typedef enum {
    DATA_MODE_RANDOM,      // Random text data
    DATA_MODE_SEQUENTIAL,  // Sequential numbers and text
    DATA_MODE_LOREM,       // Lorem ipsum text
    DATA_MODE_BINARY,      // Binary blob data
    DATA_MODE_MIXED        // Mixed data types
} DataMode;

// Configuration structure
typedef struct {
    const char *output_file;
    double target_size;           // Target file size in bytes
    int use_compression;        // Use CCVFS compression
    const char *compress_algorithm;   // Compression algorithm
    const char *encrypt_algorithm;    // Encryption algorithm
    uint32_t page_size;        // Page size for compression
    int compression_level;      // Compression level (1-9)
    DataMode data_mode;         // Data generation mode
    int record_size;            // Average record size
    int table_count;            // Number of tables to create
    int verbose;                // Verbose output
    int batch_size;             // Records per transaction
    int use_wal_mode;           // Use WAL journal mode (default: true)
} GeneratorConfig;

// Main function to generate a database
int sqlite3_ccvfs_generate_database(const GeneratorConfig *config);

// Helper function to parse size strings (e.g., "10MB", "500KB", "2GB")
double sqlite3_ccvfs_parse_size_string(const char *size_str);

// Helper function to parse page size strings
uint32_t sqlite3_ccvfs_parse_page_size(const char *size_str);

// Initialize configuration with default values
void sqlite3_ccvfs_init_generator_config(GeneratorConfig *config);

#ifdef __cplusplus
}
#endif

#endif /* DB_GENERATOR_H */