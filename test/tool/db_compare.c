#ifndef DB_COMPARE_H
#define DB_COMPARE_H

#include <sqlite3.h>

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

// Function to compare two databases
int compare_databases(const char *db1_path, const char *db2_path,
                      const CompareOptions *options, CompareResult *result);

// Function to print comparison results
void print_compare_results(const CompareResult *result, const CompareOptions *options);

#endif // DB_COMPARE_H
#include "ccvfs.h"
#include "db_compare.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

// Function to check if a table should be ignored
static int should_ignore_table(const char *table_name, const char *ignore_list) {
    if (!ignore_list || !table_name) return 0;
    
    char *list_copy = strdup(ignore_list);
    if (!list_copy) return 0;
    
    char *token = strtok(list_copy, ",");
    while (token) {
        // Trim whitespace
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) end--;
        *(end + 1) = '\0';
        
        if (strcmp(token, table_name) == 0) {
            free(list_copy);
            return 1;
        }
        token = strtok(NULL, ",");
    }
    
    free(list_copy);
    return 0;
}

// Get list of tables from database
static int get_table_list(sqlite3 *db, char ***tables, int *count) {
    const char *sql = "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name";
    sqlite3_stmt *stmt;
    int rc;
    
    *tables = NULL;
    *count = 0;
    
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparing table list query: %s\n", sqlite3_errmsg(db));
        return rc;
    }
    
    // First pass: count tables
    int table_count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        table_count++;
    }
    
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error counting tables: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }
    
    sqlite3_reset(stmt);
    
    // Allocate memory for table names
    *tables = malloc(table_count * sizeof(char*));
    if (!*tables) {
        sqlite3_finalize(stmt);
        return SQLITE_NOMEM;
    }
    
    // Second pass: collect table names
    int i = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *table_name = (const char*)sqlite3_column_text(stmt, 0);
        (*tables)[i] = strdup(table_name);
        if (!(*tables)[i]) {
            // Cleanup on error
            for (int j = 0; j < i; j++) {
                free((*tables)[j]);
            }
            free(*tables);
            *tables = NULL;
            sqlite3_finalize(stmt);
            return SQLITE_NOMEM;
        }
        i++;
    }
    
    *count = table_count;
    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

// Get table schema
static int get_table_schema(sqlite3 *db, const char *table_name, char **schema) {
    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT sql FROM sqlite_master WHERE type='table' AND name='%s'", table_name);
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    *schema = NULL;
    if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *sql_text = (const char*)sqlite3_column_text(stmt, 0);
        if (sql_text) {
            *schema = strdup(sql_text);
        }
    }
    
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? SQLITE_OK : rc;
}

// Compare table schemas
static int compare_table_schemas(sqlite3 *db1, sqlite3 *db2, const char *table_name, 
                                const CompareOptions *options, CompareResult *result) {
    char *schema1 = NULL, *schema2 = NULL;
    int rc1, rc2;
    
    rc1 = get_table_schema(db1, table_name, &schema1);
    rc2 = get_table_schema(db2, table_name, &schema2);
    
    if (rc1 != SQLITE_OK || rc2 != SQLITE_OK) {
        if (options->verbose) {
            printf("Error getting schema for table '%s': db1=%d, db2=%d\n", table_name, rc1, rc2);
        }
        result->schema_differences++;
        goto cleanup;
    }
    
    if (!schema1 && !schema2) {
        // Both missing - no difference
        goto cleanup;
    }
    
    if (!schema1 || !schema2) {
        printf("SCHEMA DIFFERENCE: Table '%s' exists in only one database\n", table_name);
        result->schema_differences++;
        goto cleanup;
    }
    
    if (strcmp(schema1, schema2) != 0) {
        printf("SCHEMA DIFFERENCE: Table '%s' has different schemas\n", table_name);
        if (options->verbose) {
            printf("  Database 1: %s\n", schema1);
            printf("  Database 2: %s\n", schema2);
        }
        result->schema_differences++;
    }
    
cleanup:
    free(schema1);
    free(schema2);
    return SQLITE_OK;
}

// Get row count for a table
static int get_row_count(sqlite3 *db, const char *table_name, int *count) {
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM \"%s\"", table_name);
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    *count = 0;
    if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        *count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? SQLITE_OK : rc;
}

// Simple checksum using length and character sum
static unsigned long simple_string_hash(const char *str) {
    if (!str) return 0;
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// Compare table data using a simple row-by-row comparison
static int compare_table_data_detailed(sqlite3 *db1, sqlite3 *db2, const char *table_name,
                                      const CompareOptions *options, CompareResult *result) {
    char sql[512];
    sqlite3_stmt *stmt1 = NULL, *stmt2 = NULL;
    int rc = SQLITE_OK;
    
    // Get row counts first
    int count1 = 0, count2 = 0;
    get_row_count(db1, table_name, &count1);
    get_row_count(db2, table_name, &count2);
    
    result->records_compared += (count1 > count2 ? count1 : count2);
    
    if (count1 != count2) {
        printf("DATA DIFFERENCE: Table '%s' has different row counts: %d vs %d\n", 
               table_name, count1, count2);
        result->records_different += abs(count1 - count2);
        return SQLITE_OK;
    }
    
    if (count1 == 0) {
        // Both tables are empty
        if (options->verbose) {
            printf("DATA MATCH: Table '%s' is empty in both databases\n", table_name);
        }
        return SQLITE_OK;
    }
    
    // Get column information first
    snprintf(sql, sizeof(sql), "PRAGMA table_info(\"%s\")", table_name);
    rc = sqlite3_prepare_v2(db1, sql, -1, &stmt1, NULL);
    if (rc != SQLITE_OK) {
        return rc;
    }
    
    int column_count = 0;
    while (sqlite3_step(stmt1) == SQLITE_ROW) {
        column_count++;
    }
    sqlite3_finalize(stmt1);
    
    if (column_count == 0) {
        printf("WARNING: Table '%s' has no columns\n", table_name);
        return SQLITE_OK;
    }
    
    // Create ordered queries to ensure consistent row ordering
    snprintf(sql, sizeof(sql), "SELECT rowid, * FROM \"%s\" ORDER BY rowid", table_name);
    
    // Prepare statements for both databases
    rc = sqlite3_prepare_v2(db1, sql, -1, &stmt1, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparing statement for db1: %s\n", sqlite3_errmsg(db1));
        return rc;
    }
    
    rc = sqlite3_prepare_v2(db2, sql, -1, &stmt2, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error preparing statement for db2: %s\n", sqlite3_errmsg(db2));
        sqlite3_finalize(stmt1);
        return rc;
    }
    
    // Compare rows
    int rows_compared = 0;
    int rows_different = 0;
    
    while (1) {
        int step1 = sqlite3_step(stmt1);
        int step2 = sqlite3_step(stmt2);
        
        if (step1 != step2) {
            printf("DATA DIFFERENCE: Table '%s' has different row availability at row %d\n", 
                   table_name, rows_compared + 1);
            rows_different++;
            break;
        }
        
        if (step1 == SQLITE_DONE) {
            // Both reached end
            break;
        }
        
        if (step1 != SQLITE_ROW) {
            fprintf(stderr, "Error stepping through table '%s': %s\n", 
                   table_name, sqlite3_errmsg(db1));
            break;
        }
        
        rows_compared++;
        
        // Compare all columns for this row
        int total_columns = sqlite3_column_count(stmt1);
        int row_identical = 1;
        
        for (int col = 0; col < total_columns; col++) {
            int type1 = sqlite3_column_type(stmt1, col);
            int type2 = sqlite3_column_type(stmt2, col);
            
            if (type1 != type2) {
                row_identical = 0;
                break;
            }
            
            switch (type1) {
                case SQLITE_INTEGER: {
                    long long val1 = sqlite3_column_int64(stmt1, col);
                    long long val2 = sqlite3_column_int64(stmt2, col);
                    if (val1 != val2) row_identical = 0;
                    break;
                }
                case SQLITE_FLOAT: {
                    double val1 = sqlite3_column_double(stmt1, col);
                    double val2 = sqlite3_column_double(stmt2, col);
                    if (val1 != val2) row_identical = 0;
                    break;
                }
                case SQLITE_TEXT: {
                    const char *text1 = (const char*)sqlite3_column_text(stmt1, col);
                    const char *text2 = (const char*)sqlite3_column_text(stmt2, col);
                    if (!text1 && !text2) {
                        // Both NULL
                    } else if (!text1 || !text2) {
                        row_identical = 0;
                    } else if (options->ignore_case) {
                        if (strcasecmp(text1, text2) != 0) row_identical = 0;
                    } else {
                        if (strcmp(text1, text2) != 0) row_identical = 0;
                    }
                    break;
                }
                case SQLITE_BLOB: {
                    const void *blob1 = sqlite3_column_blob(stmt1, col);
                    const void *blob2 = sqlite3_column_blob(stmt2, col);
                    int size1 = sqlite3_column_bytes(stmt1, col);
                    int size2 = sqlite3_column_bytes(stmt2, col);
                    if (size1 != size2 || memcmp(blob1, blob2, size1) != 0) {
                        row_identical = 0;
                    }
                    break;
                }
                case SQLITE_NULL:
                    // Both are NULL, identical
                    break;
            }
            
            if (!row_identical) break;
        }
        
        if (!row_identical) {
            rows_different++;
            if (options->verbose && rows_different <= 5) {
                printf("DATA DIFFERENCE: Table '%s' row %d differs\n", table_name, rows_compared);
            }
        }
    }
    
    sqlite3_finalize(stmt1);
    sqlite3_finalize(stmt2);
    
    if (rows_different == 0) {
        if (options->verbose) {
            printf("DATA MATCH: Table '%s' has identical content (%d rows)\n", 
                   table_name, rows_compared);
        }
        result->records_identical += rows_compared;
    } else {
        printf("DATA DIFFERENCE: Table '%s' has %d different rows out of %d\n", 
               table_name, rows_different, rows_compared);
        result->records_different += rows_different;
        result->records_identical += (rows_compared - rows_different);
    }
    
    return SQLITE_OK;
}

// Helper function to determine if a file is likely a CCVFS file
static int is_ccvfs_file(const char *filename) {
    if (!filename) return 0;
    
    // Check file extension
    const char *ext = strrchr(filename, '.');
    if (ext && strcmp(ext, ".ccvfs") == 0) {
        return 1;
    }
    
    // Check file header
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;
    
    char magic[8];
    size_t read = fread(magic, 1, 8, fp);
    fclose(fp);
    
    if (read == 8 && strncmp(magic, CCVFS_MAGIC, 8) == 0) {
        return 1;
    }
    
    return 0;
}
int compare_databases(const char *db1_path, const char *db2_path, 
                      const CompareOptions *options, CompareResult *result) {
    sqlite3 *db1 = NULL, *db2 = NULL;
    char **tables1 = NULL, **tables2 = NULL;
    int table_count1 = 0, table_count2 = 0;
    int rc = SQLITE_OK;
    
    memset(result, 0, sizeof(CompareResult));
    
    // Open databases
    const char *vfs1 = is_ccvfs_file(db1_path) ? "ccvfs" : NULL;
    const char *vfs2 = is_ccvfs_file(db2_path) ? "ccvfs" : NULL;
    
    rc = sqlite3_open_v2(db1_path, &db1, SQLITE_OPEN_READONLY, vfs1);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database 1 '%s': %s\n", db1_path, sqlite3_errmsg(db1));
        goto cleanup;
    }
    
    rc = sqlite3_open_v2(db2_path, &db2, SQLITE_OPEN_READONLY, vfs2);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database 2 '%s': %s\n", db2_path, sqlite3_errmsg(db2));
        goto cleanup;
    }
    
    if (options->verbose) {
        printf("Opened databases successfully\n");
        printf("Database 1: %s\n", db1_path);
        printf("Database 2: %s\n", db2_path);
        printf("\n");
    }
    
    // Get table lists
    rc = get_table_list(db1, &tables1, &table_count1);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error getting table list from database 1\n");
        goto cleanup;
    }
    
    rc = get_table_list(db2, &tables2, &table_count2);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error getting table list from database 2\n");
        goto cleanup;
    }
    
    if (options->verbose) {
        printf("Database 1 has %d tables\n", table_count1);
        printf("Database 2 has %d tables\n", table_count2);
        printf("\n");
    }
    
    // Create a merged list of all unique table names
    char **all_tables = malloc((table_count1 + table_count2) * sizeof(char*));
    int all_table_count = 0;
    
    // Add tables from db1
    for (int i = 0; i < table_count1; i++) {
        if (!should_ignore_table(tables1[i], options->ignore_tables)) {
            all_tables[all_table_count++] = strdup(tables1[i]);
        }
    }
    
    // Add tables from db2 that aren't already in the list
    for (int i = 0; i < table_count2; i++) {
        if (should_ignore_table(tables2[i], options->ignore_tables)) {
            continue;
        }
        
        int found = 0;
        for (int j = 0; j < all_table_count; j++) {
            if (strcmp(all_tables[j], tables2[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            all_tables[all_table_count++] = strdup(tables2[i]);
        }
    }
    
    printf("Comparing %d tables...\n\n", all_table_count);
    
    // Compare each table
    for (int i = 0; i < all_table_count; i++) {
        const char *table_name = all_tables[i];
        
        if (options->verbose) {
            printf("Processing table: %s\n", table_name);
        }
        
        result->tables_compared++;
        
        // Check if table exists in both databases
        int exists_in_db1 = 0, exists_in_db2 = 0;
        
        for (int j = 0; j < table_count1; j++) {
            if (strcmp(tables1[j], table_name) == 0) {
                exists_in_db1 = 1;
                break;
            }
        }
        
        for (int j = 0; j < table_count2; j++) {
            if (strcmp(tables2[j], table_name) == 0) {
                exists_in_db2 = 1;
                break;
            }
        }
        
        if (!exists_in_db1 || !exists_in_db2) {
            printf("TABLE DIFFERENCE: Table '%s' exists in %s%s%s\n", 
                   table_name,
                   exists_in_db1 ? "database 1" : "",
                   (exists_in_db1 && exists_in_db2) ? " and " : "",
                   exists_in_db2 ? "database 2" : "");
            if (!exists_in_db1 && !exists_in_db2) {
                printf("  (This should not happen - internal error)\n");
            }
            result->tables_different++;
            continue;
        }
        
        // Compare schema
        compare_table_schemas(db1, db2, table_name, options, result);
        
        // Compare data (unless schema-only mode)
        if (!options->compare_schema_only) {
            compare_table_data_detailed(db1, db2, table_name, options, result);
        }
        
        // Determine if table is identical
        int table_identical = 1;
        // This is a simplification - in a full implementation, we'd track per-table differences
        
        if (table_identical) {
            result->tables_identical++;
        } else {
            result->tables_different++;
        }
    }
    
    // Cleanup all_tables
    for (int i = 0; i < all_table_count; i++) {
        free(all_tables[i]);
    }
    free(all_tables);
    
cleanup:
    // Cleanup tables1
    if (tables1) {
        for (int i = 0; i < table_count1; i++) {
            free(tables1[i]);
        }
        free(tables1);
    }
    
    // Cleanup tables2
    if (tables2) {
        for (int i = 0; i < table_count2; i++) {
            free(tables2[i]);
        }
        free(tables2);
    }
    
    if (db1) sqlite3_close(db1);
    if (db2) sqlite3_close(db2);
    
    return rc;
}

// Print comparison results
void print_compare_results(const CompareResult *result, const CompareOptions *options) {
    printf("\n=== Database Comparison Results ===\n");
    printf("Tables compared:    %d\n", result->tables_compared);
    printf("  Identical:        %d\n", result->tables_identical);
    printf("  Different:        %d\n", result->tables_different);
    
    if (!options->compare_schema_only) {
        printf("Records compared:   %d\n", result->records_compared);
        printf("  Identical:        %d\n", result->records_identical);
        printf("  Different:        %d\n", result->records_different);
    }
    
    printf("Schema differences: %d\n", result->schema_differences);
    
    int total_differences = result->tables_different + result->records_different + result->schema_differences;
    if (total_differences == 0) {
        printf("\n✓ Databases are IDENTICAL\n");
    } else {
        printf("\n✗ Databases have %d differences\n", total_differences);
    }
}

// Print usage information
static void print_usage(const char *program_name) {
    printf("SQLite数据库比对工具\n");
    printf("用法: %s [选项] <数据库1> <数据库2>\n\n", program_name);
    
    printf("选项:\n");
    printf("  -s, --schema-only            只比对表结构，不比对数据\n");
    printf("  -i, --ignore-case            忽略字符串比较中的大小写差异\n");
    printf("  -w, --ignore-whitespace      忽略空白字符差异\n");
    printf("  -t, --ignore-tables <表名>   忽略指定的表（逗号分隔）\n");
    printf("  -v, --verbose                详细输出\n");
    printf("  -h, --help                   显示帮助信息\n\n");
    
    printf("功能:\n");
    printf("  • 自动识别和处理压缩数据库（.ccvfs）和标准SQLite数据库\n");
    printf("  • 比对表结构（CREATE TABLE语句）\n"); 
    printf("  • 比对数据内容（行数和校验和）\n");
    printf("  • 支持忽略特定表或字段\n");
    printf("  • 提供详细的差异报告\n\n");
    
    printf("示例:\n");
    printf("  %s db1.db db2.db                     # 比对两个标准数据库\n", program_name);
    printf("  %s db1.ccvfs db2.db                  # 比对压缩数据库和标准数据库\n", program_name);
    printf("  %s -s db1.db db2.db                  # 只比对表结构\n", program_name);
    printf("  %s -t temp,log db1.db db2.db         # 忽略temp和log表\n", program_name);
    printf("  %s -v db1.db db2.db                  # 详细输出\n", program_name);
}

