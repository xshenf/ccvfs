/*
 * Large Database Compression Ratio Test
 * Creates large databases to test compression efficiency
 * Preserves both normal and compressed databases for comparison
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "sqlite3.h"
#include "ccvfs.h"
#include "ccvfs_algorithm.h"

// Get file size
long get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

// Format size display
void format_size(long size, char *buffer, size_t buffer_size) {
    if (size >= 1024*1024) {
        snprintf(buffer, buffer_size, "%.2f MB", size / (1024.0*1024.0));
    } else if (size >= 1024) {
        snprintf(buffer, buffer_size, "%.2f KB", size / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%ld bytes", size);
    }
}

typedef struct {
    const char* algorithm;
    const char* db_filename;
    long file_size;
    double write_time;
    int records_count;
    int success;
} CompressionResult;

int create_large_database(const char* filename, const char* vfs_name, int record_count, CompressionResult* result) {
    printf("Creating database: %s (VFS: %s)\n", filename, vfs_name ? vfs_name : "default");
    
    // Remove old database
    remove(filename);
    
    sqlite3 *db = NULL;
    int rc;
    
    if (vfs_name) {
        rc = sqlite3_open_v2(filename, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs_name);
    } else {
        rc = sqlite3_open(filename, &db);
    }
    
    if (rc != SQLITE_OK) {
        printf("❌ Failed to open database %s: %s\n", filename, sqlite3_errmsg(db));
        return 0;
    }
    
    clock_t start_time = clock();
    
    // Create multiple tables with different data types
    const char* create_tables_sql[] = {
        "CREATE TABLE users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT NOT NULL, "
        "email TEXT NOT NULL, "
        "password_hash TEXT NOT NULL, "
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
        "profile_data TEXT"
        ");",
        
        "CREATE TABLE posts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER REFERENCES users(id), "
        "title TEXT NOT NULL, "
        "content TEXT NOT NULL, "
        "tags TEXT, "
        "view_count INTEGER DEFAULT 0, "
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");",
        
        "CREATE TABLE comments ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "post_id INTEGER REFERENCES posts(id), "
        "user_id INTEGER REFERENCES users(id), "
        "comment_text TEXT NOT NULL, "
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");",
        
        "CREATE TABLE logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "level TEXT NOT NULL, "
        "message TEXT NOT NULL, "
        "details TEXT, "
        "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");"
    };
    
    char* err_msg = NULL;
    for (int i = 0; i < 4; i++) {
        rc = sqlite3_exec(db, create_tables_sql[i], 0, 0, &err_msg);
        if (rc != SQLITE_OK) {
            printf("❌ Failed to create table %d: %s\n", i + 1, err_msg);
            sqlite3_free(err_msg);
            sqlite3_close(db);
            return 0;
        }
    }
    
    printf("  Tables created successfully\n");
    
    // Begin transaction for bulk insert
    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, NULL);
    
    // Insert users data
    printf("  Inserting users data...\n");
    sqlite3_stmt* users_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO users (username, email, password_hash, profile_data) VALUES (?, ?, ?, ?);", -1, &users_stmt, NULL);
    
    for (int i = 1; i <= record_count / 4; i++) {
        char username[64], email[128], hash[128], profile[512];
        
        snprintf(username, sizeof(username), "user_%06d", i);
        snprintf(email, sizeof(email), "user_%06d@example.com", i);
        snprintf(hash, sizeof(hash), "sha256_hash_%06d_abcdef123456789", i);
        snprintf(profile, sizeof(profile), 
                "User profile for %s. This user joined our platform and has various interests including technology, science, arts, and literature. "
                "Profile contains repeated information for compression testing. ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", username);
        
        sqlite3_bind_text(users_stmt, 1, username, -1, SQLITE_STATIC);
        sqlite3_bind_text(users_stmt, 2, email, -1, SQLITE_STATIC);
        sqlite3_bind_text(users_stmt, 3, hash, -1, SQLITE_STATIC);
        sqlite3_bind_text(users_stmt, 4, profile, -1, SQLITE_STATIC);
        
        sqlite3_step(users_stmt);
        sqlite3_reset(users_stmt);
    }
    sqlite3_finalize(users_stmt);
    
    // Insert posts data
    printf("  Inserting posts data...\n");
    sqlite3_stmt* posts_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO posts (user_id, title, content, tags, view_count) VALUES (?, ?, ?, ?, ?);", -1, &posts_stmt, NULL);
    
    for (int i = 1; i <= record_count / 2; i++) {
        char title[128], content[1024], tags[128];
        int user_id = (i % (record_count / 4)) + 1;
        int view_count = i * 7 % 1000;
        
        snprintf(title, sizeof(title), "Post Title %06d: Interesting Topic About Technology", i);
        snprintf(content, sizeof(content), 
                "This is post content %06d. It contains detailed information about various topics. "
                "The content is designed to have good compression potential with repeated phrases and common words. "
                "Technology is advancing rapidly in areas like artificial intelligence, machine learning, cloud computing, "
                "mobile development, web development, data science, cybersecurity, and blockchain technology. "
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 - repeated content for compression testing.", i);
        snprintf(tags, sizeof(tags), "technology,programming,development,tutorial,example");
        
        sqlite3_bind_int(posts_stmt, 1, user_id);
        sqlite3_bind_text(posts_stmt, 2, title, -1, SQLITE_STATIC);
        sqlite3_bind_text(posts_stmt, 3, content, -1, SQLITE_STATIC);
        sqlite3_bind_text(posts_stmt, 4, tags, -1, SQLITE_STATIC);
        sqlite3_bind_int(posts_stmt, 5, view_count);
        
        sqlite3_step(posts_stmt);
        sqlite3_reset(posts_stmt);
    }
    sqlite3_finalize(posts_stmt);
    
    // Insert comments data
    printf("  Inserting comments data...\n");
    sqlite3_stmt* comments_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO comments (post_id, user_id, comment_text) VALUES (?, ?, ?);", -1, &comments_stmt, NULL);
    
    for (int i = 1; i <= record_count; i++) {
        char comment[512];
        int post_id = (i % (record_count / 2)) + 1;
        int user_id = (i % (record_count / 4)) + 1;
        
        snprintf(comment, sizeof(comment), 
                "Comment %06d: This is a user comment with repeated content for compression testing. "
                "Great post! Very informative and helpful. Thanks for sharing this valuable information. "
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", i);
        
        sqlite3_bind_int(comments_stmt, 1, post_id);
        sqlite3_bind_int(comments_stmt, 2, user_id);
        sqlite3_bind_text(comments_stmt, 3, comment, -1, SQLITE_STATIC);
        
        sqlite3_step(comments_stmt);
        sqlite3_reset(comments_stmt);
    }
    sqlite3_finalize(comments_stmt);
    
    // Insert logs data
    printf("  Inserting logs data...\n");
    sqlite3_stmt* logs_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO logs (level, message, details) VALUES (?, ?, ?);", -1, &logs_stmt, NULL);
    
    const char* log_levels[] = {"INFO", "WARNING", "ERROR", "DEBUG"};
    for (int i = 1; i <= record_count / 2; i++) {
        char message[256], details[512];
        const char* level = log_levels[i % 4];
        
        snprintf(message, sizeof(message), "Log message %06d: System operation completed", i);
        snprintf(details, sizeof(details), 
                "Detailed log information %06d. System performed various operations including database queries, "
                "file operations, network requests, and user authentication. All operations completed successfully. "
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", i);
        
        sqlite3_bind_text(logs_stmt, 1, level, -1, SQLITE_STATIC);
        sqlite3_bind_text(logs_stmt, 2, message, -1, SQLITE_STATIC);
        sqlite3_bind_text(logs_stmt, 3, details, -1, SQLITE_STATIC);
        
        sqlite3_step(logs_stmt);
        sqlite3_reset(logs_stmt);
    }
    sqlite3_finalize(logs_stmt);
    
    // Commit transaction
    sqlite3_exec(db, "COMMIT;", 0, 0, NULL);
    
    clock_t end_time = clock();
    
    // Verify record counts
    sqlite3_stmt* count_stmt;
    int total_records = 0;
    
    const char* count_queries[] = {
        "SELECT COUNT(*) FROM users",
        "SELECT COUNT(*) FROM posts", 
        "SELECT COUNT(*) FROM comments",
        "SELECT COUNT(*) FROM logs"
    };
    
    for (int i = 0; i < 4; i++) {
        sqlite3_prepare_v2(db, count_queries[i], -1, &count_stmt, NULL);
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            total_records += sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }
    
    sqlite3_close(db);
    
    // Fill result structure
    result->db_filename = filename;
    result->file_size = get_file_size(filename);
    result->write_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    result->records_count = total_records;
    result->success = 1;
    
    char size_str[64];
    format_size(result->file_size, size_str, sizeof(size_str));
    
    printf("  ✅ Database created successfully\n");
    printf("  Records inserted: %d\n", total_records);
    printf("  File size: %ld bytes (%s)\n", result->file_size, size_str);
    printf("  Write time: %.2f seconds\n", result->write_time);
    
    return 1;
}

int main() {
    printf("=== Large Database Compression Ratio Test ===\n");
    printf("SQLite Version: %s\n", sqlite3_libversion());
    
    const int RECORD_COUNT = 10000;  // This will create ~25,000 total records across all tables
    
    // Initialize algorithms
    ccvfs_init_builtin_algorithms();
    
    char available_algorithms[256];
    ccvfs_list_compress_algorithms(available_algorithms, sizeof(available_algorithms));
    printf("Available compression algorithms: %s\n", available_algorithms);
    printf("Target record count: ~%d total records across all tables\n\n", (int)(RECORD_COUNT * 1.75));
    
    // Create normal database
    printf("=== Creating Normal Database ===\n");
    CompressionResult normal_result = {0};
    normal_result.algorithm = "none";
    
    if (!create_large_database("large_normal.db", NULL, RECORD_COUNT, &normal_result)) {
        printf("❌ Failed to create normal database\n");
        return 1;
    }
    
    // Test compression algorithms
    const char* test_algorithms[] = {"zlib", "lz4", "lzma"};
    const int num_algorithms = sizeof(test_algorithms) / sizeof(test_algorithms[0]);
    
    CompressionResult compression_results[10];
    int successful_compressions = 0;
    
    for (int i = 0; i < num_algorithms; i++) {
        const char* algo = test_algorithms[i];
        
        // Check if algorithm is available
        CompressAlgorithm* compress_algo = ccvfs_find_compress_algorithm(algo);
        if (!compress_algo) {
            printf("⚠️  %s: Not available, skipping\n", algo);
            continue;
        }
        
        printf("\n=== Creating %s Compressed Database ===\n", algo);
        
        char vfs_name[64], db_name[64];
        snprintf(vfs_name, sizeof(vfs_name), "%s_vfs", algo);
        snprintf(db_name, sizeof(db_name), "large_%s.db", algo);
        
        // Create VFS
        int rc = sqlite3_ccvfs_create(vfs_name, NULL, algo, NULL, 0, CCVFS_CREATE_REALTIME);
        if (rc != SQLITE_OK) {
            printf("❌ Failed to create %s VFS: %d\n", algo, rc);
            continue;
        }
        
        CompressionResult* result = &compression_results[successful_compressions];
        result->algorithm = algo;
        
        if (create_large_database(db_name, vfs_name, RECORD_COUNT, result)) {
            successful_compressions++;
        }
        
        sqlite3_ccvfs_destroy(vfs_name);
    }
    
    // Display comprehensive comparison results
    printf("\n" "=" "=" "=" "=" "=" " COMPRESSION RESULTS COMPARISON " "=" "=" "=" "=" "=" "\n");
    printf("| %-10s | %-15s | %-12s | %-10s | %-12s | %-8s |\n", 
           "Algorithm", "File Size", "Ratio", "Records", "Time (sec)", "Speed");
    printf("|------------|-----------------|--------------|------------|--------------|----------|\n");
    
    // Normal database
    char normal_size_str[64];
    format_size(normal_result.file_size, normal_size_str, sizeof(normal_size_str));
    printf("| %-10s | %-15s | %-12s | %-10d | %-12.2f | %-8.0f |\n", 
           "Normal", normal_size_str, "100.0%", normal_result.records_count, 
           normal_result.write_time, normal_result.records_count / normal_result.write_time);
    
    // Compressed databases
    for (int i = 0; i < successful_compressions; i++) {
        CompressionResult* result = &compression_results[i];
        
        char size_str[64];
        format_size(result->file_size, size_str, sizeof(size_str));
        
        double ratio = normal_result.file_size > 0 ? 
                      (double)result->file_size / normal_result.file_size * 100.0 : 0.0;
        
        printf("| %-10s | %-15s | %-11.1f%% | %-10d | %-12.2f | %-8.0f |\n", 
               result->algorithm, size_str, ratio, result->records_count,
               result->write_time, result->records_count / result->write_time);
    }
    
    // Space savings analysis
    printf("\n=== SPACE SAVINGS ANALYSIS ===\n");
    for (int i = 0; i < successful_compressions; i++) {
        CompressionResult* result = &compression_results[i];
        
        if (normal_result.file_size > 0) {
            long space_saved = normal_result.file_size - result->file_size;
            double saved_percent = (double)space_saved / normal_result.file_size * 100.0;
            
            char saved_str[64];
            format_size(space_saved, saved_str, sizeof(saved_str));
            
            printf("%s compression:\n", result->algorithm);
            printf("  Space saved: %s (%.1f%%)\n", saved_str, saved_percent);
            printf("  Compression ratio: %.1f:1\n", (double)normal_result.file_size / result->file_size);
            printf("  Performance impact: %.1fx slower\n", result->write_time / normal_result.write_time);
            printf("\n");
        }
    }
    
    // Database files preserved for manual inspection
    printf("=== PRESERVED DATABASE FILES ===\n");
    printf("Normal database: large_normal.db (%.2f MB)\n", 
           normal_result.file_size / (1024.0 * 1024.0));
    
    for (int i = 0; i < successful_compressions; i++) {
        printf("%s compressed: large_%s.db (%.2f MB)\n", 
               compression_results[i].algorithm, 
               compression_results[i].algorithm,
               compression_results[i].file_size / (1024.0 * 1024.0));
    }
    
    printf("\n=== FINAL RESULTS ===\n");
    printf("✅ Large database compression test completed!\n");
    printf("Normal database: %d records\n", normal_result.records_count);
    printf("Successful compressions: %d\n", successful_compressions);
    printf("All database files preserved for inspection\n");
    
    return 0;
}