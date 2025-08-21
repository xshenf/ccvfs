#ifndef DB_COMPARE_H
#define DB_COMPARE_H

#include "ccvfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Comparison result structure
typedef struct {
    int tables_compared;
    int tables_identical;
    int tables_different;
    int records_compared;
    int records_identical;
    int records_different;
    int schema_differences;
} CompareResult;

// Comparison options
typedef struct {
    int ignore_case;          // Ignore case in string comparisons
    int ignore_whitespace;    // Ignore whitespace differences
    int compare_schema_only;  // Only compare schema, not data
    int verbose;              // Verbose output
    const char *ignore_tables; // Comma-separated list of tables to ignore
} CompareOptions;

// Main function to compare two databases
int compare_databases(const char *db1_path, const char *db2_path, 
                      const CompareOptions *options, CompareResult *result);

// Print comparison results
void print_compare_results(const CompareResult *result, const CompareOptions *options);

#ifdef __cplusplus
}
#endif

#endif /* DB_COMPARE_H */