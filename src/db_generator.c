
#include "ccvfs.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <getopt.h>

/*
 * Database generator tool - Create databases of arbitrary size
 * Supports both compressed and uncompressed database generation
 */

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
    char *output_file;
    double target_size;           // Target file size in bytes
    int use_compression;        // Use CCVFS compression
    char *compress_algorithm;   // Compression algorithm
    char *encrypt_algorithm;    // Encryption algorithm
    uint32_t page_size;        // Page size for compression
    int compression_level;      // Compression level (1-9)
    DataMode data_mode;         // Data generation mode
    int record_size;            // Average record size
    int table_count;            // Number of tables to create
    int verbose;                // Verbose output
    int batch_size;             // Records per transaction
    int use_wal_mode;           // Use WAL journal mode (default: true)
} GeneratorConfig;

// Get file size
static long get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// Generate random string - optimized version with pre-generated random data
static void generate_random_string(char *buffer, int length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ";
    int charset_size = sizeof(charset) - 1;
    
    // Generate multiple random values at once to reduce rand() calls
    static unsigned int rand_cache = 0;
    static int rand_bits = 0;
    
    for (int i = 0; i < length - 1; i++) {
        if (rand_bits < 8) {
            rand_cache = rand();
            rand_bits = 32;
        }
        buffer[i] = charset[(rand_cache & 0xFF) % charset_size];
        rand_cache >>= 8;
        rand_bits -= 8;
    }
    buffer[length - 1] = '\0';
}

// Generate Lorem ipsum text
static void generate_lorem_text(char *buffer, int length) {
    const char *lorem_words[] = {
        "Lorem", "ipsum", "dolor", "sit", "amet", "consectetur", "adipiscing", "elit",
        "sed", "do", "eiusmod", "tempor", "incididunt", "ut", "labore", "et", "dolore",
        "magna", "aliqua", "Ut", "enim", "ad", "minim", "veniam", "quis", "nostrud",
        "exercitation", "ullamco", "laboris", "nisi", "ut", "aliquip", "ex", "ea",
        "commodo", "consequat", "Duis", "aute", "irure", "dolor", "in", "reprehenderit",
        "voluptate", "velit", "esse", "cillum", "fugiat", "nulla", "pariatur"
    };
    int word_count = sizeof(lorem_words) / sizeof(lorem_words[0]);
    
    int pos = 0;
    while (pos < length - 20) {  // Leave space for word and null terminator
        const char *word = lorem_words[rand() % word_count];
        int word_len = strlen(word);
        
        if (pos + word_len + 1 < length) {
            strcpy(buffer + pos, word);
            pos += word_len;
            if (pos < length - 1) {
                buffer[pos++] = ' ';
            }
        } else {
            break;
        }
    }
    buffer[pos] = '\0';
}

// Generate data based on mode
static void generate_data(char *buffer, int length, DataMode mode, int record_id) {
    switch (mode) {
        case DATA_MODE_RANDOM:
            generate_random_string(buffer, length);
            break;
            
        case DATA_MODE_SEQUENTIAL:
            snprintf(buffer, length, "Record_%d_Data_%08d", record_id, record_id * 123456);
            break;
            
        case DATA_MODE_LOREM:
            generate_lorem_text(buffer, length);
            break;
            
        case DATA_MODE_BINARY:
            // Optimized binary data generation
            {
                static unsigned int rand_cache = 0;
                static int rand_bits = 0;
                
                for (int i = 0; i < length - 1; i++) {
                    if (rand_bits < 8) {
                        rand_cache = rand();
                        rand_bits = 32;
                    }
                    buffer[i] = (char)(rand_cache & 0xFF);
                    rand_cache >>= 8;
                    rand_bits -= 8;
                }
            }
            buffer[length - 1] = '\0';
            break;
            
        case DATA_MODE_MIXED:
            if (record_id % 4 == 0) {
                generate_random_string(buffer, length);
            } else if (record_id % 4 == 1) {
                generate_lorem_text(buffer, length);
            } else if (record_id % 4 == 2) {
                snprintf(buffer, length, "Mixed_Record_%d_Time_%ld", record_id, time(NULL));
            } else {
                // Optimized printable ASCII generation
                static unsigned int rand_cache = 0;
                static int rand_bits = 0;
                
                for (int i = 0; i < length - 1; i++) {
                    if (rand_bits < 8) {
                        rand_cache = rand();
                        rand_bits = 32;
                    }
                    buffer[i] = (char)(32 + ((rand_cache & 0xFF) % 95));
                    rand_cache >>= 8;
                    rand_bits -= 8;
                }
                buffer[length - 1] = '\0';
            }
            break;
    }
}

// Table definitions with realistic schemas
typedef struct {
    const char *name;
    const char *schema;
    const char *indexes[5];  // Up to 5 indexes per table
} TableDef;

static TableDef table_definitions[] = {
    // Users table
    {
        "users",
        "CREATE TABLE IF NOT EXISTS users ("
        "user_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username VARCHAR(50) UNIQUE NOT NULL, "
        "email VARCHAR(100) UNIQUE NOT NULL, "
        "password_hash VARCHAR(255) NOT NULL, "
        "first_name VARCHAR(50), "
        "last_name VARCHAR(50), "
        "phone VARCHAR(20), "
        "status VARCHAR(20) DEFAULT 'active', "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "profile_data TEXT"
        ")",
        {
            "CREATE INDEX IF NOT EXISTS idx_users_username ON users(username)",
            "CREATE INDEX IF NOT EXISTS idx_users_email ON users(email)",
            "CREATE INDEX IF NOT EXISTS idx_users_status ON users(status)",
            "CREATE INDEX IF NOT EXISTS idx_users_created ON users(created_at)",
            NULL
        }
    },
    // Products table
    {
        "products",
        "CREATE TABLE IF NOT EXISTS products ("
        "product_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "sku VARCHAR(50) UNIQUE NOT NULL, "
        "name VARCHAR(200) NOT NULL, "
        "description TEXT, "
        "category_id INTEGER, "
        "price DECIMAL(10,2) NOT NULL, "
        "stock_quantity INTEGER DEFAULT 0, "
        "weight DECIMAL(8,3), "
        "status VARCHAR(20) DEFAULT 'active', "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "metadata TEXT"
        ")",
        {
            "CREATE INDEX IF NOT EXISTS idx_products_sku ON products(sku)",
            "CREATE INDEX IF NOT EXISTS idx_products_category ON products(category_id)",
            "CREATE INDEX IF NOT EXISTS idx_products_price ON products(price)",
            "CREATE INDEX IF NOT EXISTS idx_products_status ON products(status)",
            "CREATE INDEX IF NOT EXISTS idx_products_name ON products(name)"
        }
    },
    // Orders table
    {
        "orders",
        "CREATE TABLE IF NOT EXISTS orders ("
        "order_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER NOT NULL, "
        "order_number VARCHAR(50) UNIQUE NOT NULL, "
        "status VARCHAR(20) DEFAULT 'pending', "
        "total_amount DECIMAL(12,2) NOT NULL, "
        "tax_amount DECIMAL(10,2) DEFAULT 0, "
        "shipping_amount DECIMAL(10,2) DEFAULT 0, "
        "payment_method VARCHAR(50), "
        "shipping_address TEXT, "
        "billing_address TEXT, "
        "notes TEXT, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")",
        {
            "CREATE INDEX IF NOT EXISTS idx_orders_user ON orders(user_id)",
            "CREATE INDEX IF NOT EXISTS idx_orders_number ON orders(order_number)",
            "CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status)",
            "CREATE INDEX IF NOT EXISTS idx_orders_created ON orders(created_at)",
            "CREATE INDEX IF NOT EXISTS idx_orders_amount ON orders(total_amount)"
        }
    },
    // Order items table
    {
        "order_items",
        "CREATE TABLE IF NOT EXISTS order_items ("
        "item_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "order_id INTEGER NOT NULL, "
        "product_id INTEGER NOT NULL, "
        "quantity INTEGER NOT NULL, "
        "unit_price DECIMAL(10,2) NOT NULL, "
        "total_price DECIMAL(12,2) NOT NULL, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")",
        {
            "CREATE INDEX IF NOT EXISTS idx_order_items_order ON order_items(order_id)",
            "CREATE INDEX IF NOT EXISTS idx_order_items_product ON order_items(product_id)",
            "CREATE INDEX IF NOT EXISTS idx_order_items_composite ON order_items(order_id, product_id)",
            NULL,
            NULL
        }
    },
    // Categories table
    {
        "categories",
        "CREATE TABLE IF NOT EXISTS categories ("
        "category_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name VARCHAR(100) NOT NULL, "
        "parent_id INTEGER, "
        "description TEXT, "
        "image_url VARCHAR(255), "
        "sort_order INTEGER DEFAULT 0, "
        "is_active BOOLEAN DEFAULT 1, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")",
        {
            "CREATE INDEX IF NOT EXISTS idx_categories_parent ON categories(parent_id)",
            "CREATE INDEX IF NOT EXISTS idx_categories_active ON categories(is_active)",
            "CREATE INDEX IF NOT EXISTS idx_categories_sort ON categories(sort_order)",
            NULL,
            NULL
        }
    },
    // Logs table
    {
        "activity_logs",
        "CREATE TABLE IF NOT EXISTS activity_logs ("
        "log_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER, "
        "action VARCHAR(50) NOT NULL, "
        "resource_type VARCHAR(50), "
        "resource_id INTEGER, "
        "ip_address VARCHAR(45), "
        "user_agent TEXT, "
        "details TEXT, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")",
        {
            "CREATE INDEX IF NOT EXISTS idx_logs_user ON activity_logs(user_id)",
            "CREATE INDEX IF NOT EXISTS idx_logs_action ON activity_logs(action)",
            "CREATE INDEX IF NOT EXISTS idx_logs_resource ON activity_logs(resource_type, resource_id)",
            "CREATE INDEX IF NOT EXISTS idx_logs_created ON activity_logs(created_at)",
            NULL
        }
    },
    // Sessions table
    {
        "user_sessions",
        "CREATE TABLE IF NOT EXISTS user_sessions ("
        "session_id VARCHAR(255) PRIMARY KEY, "
        "user_id INTEGER NOT NULL, "
        "ip_address VARCHAR(45), "
        "user_agent TEXT, "
        "last_activity DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "expires_at DATETIME NOT NULL, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")",
        {
            "CREATE INDEX IF NOT EXISTS idx_sessions_user ON user_sessions(user_id)",
            "CREATE INDEX IF NOT EXISTS idx_sessions_expires ON user_sessions(expires_at)",
            "CREATE INDEX IF NOT EXISTS idx_sessions_activity ON user_sessions(last_activity)",
            NULL,
            NULL
        }
    },
    // Settings table
    {
        "app_settings",
        "CREATE TABLE IF NOT EXISTS app_settings ("
        "setting_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "key VARCHAR(100) UNIQUE NOT NULL, "
        "value TEXT, "
        "type VARCHAR(20) DEFAULT 'string', "
        "category VARCHAR(50), "
        "description TEXT, "
        "is_public BOOLEAN DEFAULT 0, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")",
        {
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_settings_key ON app_settings(key)",
            "CREATE INDEX IF NOT EXISTS idx_settings_category ON app_settings(category)",
            "CREATE INDEX IF NOT EXISTS idx_settings_public ON app_settings(is_public)",
            NULL,
            NULL
        }
    }
};

static int table_definitions_count = sizeof(table_definitions) / sizeof(table_definitions[0]);

// Create database tables and indexes
static int create_tables(sqlite3 *db, GeneratorConfig *config) {
    char sql[2048];
    int tables_to_create = config->table_count;
    
    // Limit to available predefined tables, or cycle through them
    if (tables_to_create > table_definitions_count) {
        printf("注意: 请求创建 %d 个表，但只有 %d 个预定义表模式，将循环使用\n", 
               tables_to_create, table_definitions_count);
    }
    
    printf("创建数据库表和索引...\n");
    
    for (int i = 0; i < tables_to_create; i++) {
        int table_def_index = i % table_definitions_count;
        TableDef *table_def = &table_definitions[table_def_index];
        
        // Create table with suffix if cycling through definitions
        if (tables_to_create > table_definitions_count) {
            int suffix = i / table_definitions_count;
            snprintf(sql, sizeof(sql), "%s", table_def->schema);
            
            // Replace table name with suffixed version
            char original_name[100];
            char new_name[100];
            snprintf(original_name, sizeof(original_name), "CREATE TABLE IF NOT EXISTS %s", table_def->name);
            snprintf(new_name, sizeof(new_name), "CREATE TABLE IF NOT EXISTS %s_%d", table_def->name, suffix);
            
            char *pos = strstr(sql, original_name);
            if (pos) {
                // Replace the table name in the schema
                char temp_sql[2048];
                strncpy(temp_sql, sql, pos - sql);
                temp_sql[pos - sql] = '\0';
                strcat(temp_sql, new_name);
                strcat(temp_sql, pos + strlen(original_name));
                strcpy(sql, temp_sql);
            }
        } else {
            strcpy(sql, table_def->schema);
        }
        
        // Create the table
        int rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "错误: 创建表失败: %s\n", sqlite3_errmsg(db));
            return rc;
        }
        
        if (config->verbose) {
            if (tables_to_create > table_definitions_count) {
                printf("✓ 创建表 %s_%d\n", table_def->name, i / table_definitions_count);
            } else {
                printf("✓ 创建表 %s\n", table_def->name);
            }
        }
        
        // Create indexes for this table
        for (int j = 0; j < 5 && table_def->indexes[j] != NULL; j++) {
            if (tables_to_create > table_definitions_count) {
                // Modify index names for suffixed tables
                int suffix = i / table_definitions_count;
                strcpy(sql, table_def->indexes[j]);
                
                // Replace table references in index
                char search_name[100];
                char replace_name[100];
                snprintf(search_name, sizeof(search_name), " %s(", table_def->name);
                snprintf(replace_name, sizeof(replace_name), " %s_%d(", table_def->name, suffix);
                
                char *pos = strstr(sql, search_name);
                if (pos) {
                    char temp_sql[2048];
                    strncpy(temp_sql, sql, pos - sql);
                    temp_sql[pos - sql] = '\0';
                    strcat(temp_sql, replace_name);
                    strcat(temp_sql, pos + strlen(search_name));
                    strcpy(sql, temp_sql);
                }
                
                // Also update index name
                snprintf(search_name, sizeof(search_name), "idx_%s_", table_def->name);
                snprintf(replace_name, sizeof(replace_name), "idx_%s_%d_", table_def->name, suffix);
                pos = strstr(sql, search_name);
                if (pos) {
                    char temp_sql[2048];
                    strncpy(temp_sql, sql, pos - sql);
                    temp_sql[pos - sql] = '\0';
                    strcat(temp_sql, replace_name);
                    strcat(temp_sql, pos + strlen(search_name));
                    strcpy(sql, temp_sql);
                }
            } else {
                strcpy(sql, table_def->indexes[j]);
            }
            
            rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
            if (rc != SQLITE_OK) {
                fprintf(stderr, "错误: 创建索引失败: %s\n", sqlite3_errmsg(db));
                return rc;
            }
            
            if (config->verbose) {
                printf("  ✓ 创建索引 %d\n", j + 1);
            }
        }
    }
    
    printf("✅ 完成创建 %d 个表和相应索引\n\n", tables_to_create);
    return SQLITE_OK;
}

// Generate realistic data for specific table types
static int generate_table_data(sqlite3 *db, const char *table_name, int table_suffix, 
                              GeneratorConfig *config, int record_id) {
    char sql[2048];
    sqlite3_stmt *stmt = NULL;
    int rc;
    
    // Generate table name with suffix if needed
    char full_table_name[100];
    if (table_suffix >= 0) {
        snprintf(full_table_name, sizeof(full_table_name), "%s_%d", table_name, table_suffix);
    } else {
        strcpy(full_table_name, table_name);
    }
    
    if (strcmp(table_name, "users") == 0) {
        // Users table data
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s (username, email, password_hash, first_name, last_name, phone, status, profile_data) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)", full_table_name);
            
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return rc;
        
        char username[50], email[100], password_hash[255], first_name[50], last_name[50];
        char phone[20], status[20], profile_data[512];
        
        snprintf(username, sizeof(username), "user_%d", record_id);
        snprintf(email, sizeof(email), "user_%d@example.com", record_id);
        snprintf(password_hash, sizeof(password_hash), "hash_%08x", rand());
        generate_data(first_name, sizeof(first_name), DATA_MODE_LOREM, record_id);
        generate_data(last_name, sizeof(last_name), DATA_MODE_LOREM, record_id + 1);
        snprintf(phone, sizeof(phone), "+1%03d%03d%04d", rand() % 900 + 100, rand() % 900 + 100, rand() % 10000);
        strcpy(status, (record_id % 10 == 0) ? "inactive" : "active");
        snprintf(profile_data, sizeof(profile_data), "{\"preferences\":{\"theme\":\"dark\",\"notifications\":%s}}", 
                 (record_id % 3 == 0) ? "true" : "false");
        
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, email, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, password_hash, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, first_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, last_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, phone, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, status, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, profile_data, -1, SQLITE_STATIC);
        
    } else if (strcmp(table_name, "products") == 0) {
        // Products table data
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s (sku, name, description, category_id, price, stock_quantity, weight, status, metadata) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", full_table_name);
            
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return rc;
        
        char sku[50], name[200], description[512], status[20], metadata[512];
        
        snprintf(sku, sizeof(sku), "SKU-%08d", record_id);
        generate_data(name, sizeof(name), DATA_MODE_LOREM, record_id);
        generate_data(description, sizeof(description), config->data_mode, record_id);
        strcpy(status, (record_id % 20 == 0) ? "discontinued" : "active");
        snprintf(metadata, sizeof(metadata), "{\"brand\":\"Brand_%d\",\"weight_unit\":\"kg\"}", record_id % 50);
        
        sqlite3_bind_text(stmt, 1, sku, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, description, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, (record_id % 10) + 1);
        sqlite3_bind_double(stmt, 5, (double)(rand() % 10000) / 100.0);
        sqlite3_bind_int(stmt, 6, rand() % 1000);
        sqlite3_bind_double(stmt, 7, (double)(rand() % 5000) / 1000.0);
        sqlite3_bind_text(stmt, 8, status, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, metadata, -1, SQLITE_STATIC);
        
    } else if (strcmp(table_name, "orders") == 0) {
        // Orders table data
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s (user_id, order_number, status, total_amount, tax_amount, shipping_amount, payment_method, shipping_address, billing_address, notes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", full_table_name);
            
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return rc;
        
        char order_number[50], status[20], payment_method[50], shipping_address[256], billing_address[256], notes[512];
        const char *statuses[] = {"pending", "processing", "shipped", "delivered", "cancelled"};
        const char *payment_methods[] = {"credit_card", "paypal", "bank_transfer", "cash"};
        
        snprintf(order_number, sizeof(order_number), "ORD-%08d", record_id);
        strcpy(status, statuses[record_id % 5]);
        strcpy(payment_method, payment_methods[record_id % 4]);
        generate_data(shipping_address, sizeof(shipping_address), DATA_MODE_LOREM, record_id);
        generate_data(billing_address, sizeof(billing_address), DATA_MODE_LOREM, record_id + 1);
        generate_data(notes, sizeof(notes), config->data_mode, record_id);
        
        sqlite3_bind_int(stmt, 1, (record_id % 1000) + 1);
        sqlite3_bind_text(stmt, 2, order_number, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, status, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 4, (double)(rand() % 100000) / 100.0);
        sqlite3_bind_double(stmt, 5, (double)(rand() % 1000) / 100.0);
        sqlite3_bind_double(stmt, 6, (double)(rand() % 5000) / 100.0);
        sqlite3_bind_text(stmt, 7, payment_method, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, shipping_address, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, billing_address, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 10, notes, -1, SQLITE_STATIC);
        
    } else if (strcmp(table_name, "order_items") == 0) {
        // Order items table data
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s (order_id, product_id, quantity, unit_price, total_price) "
            "VALUES (?, ?, ?, ?, ?)", full_table_name);
            
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return rc;
        
        int quantity = (rand() % 10) + 1;
        double unit_price = (double)(rand() % 10000) / 100.0;
        
        sqlite3_bind_int(stmt, 1, (record_id % 500) + 1);
        sqlite3_bind_int(stmt, 2, (record_id % 1000) + 1);
        sqlite3_bind_int(stmt, 3, quantity);
        sqlite3_bind_double(stmt, 4, unit_price);
        sqlite3_bind_double(stmt, 5, unit_price * quantity);
        
    } else if (strcmp(table_name, "categories") == 0) {
        // Categories table data
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s (name, parent_id, description, image_url, sort_order, is_active) "
            "VALUES (?, ?, ?, ?, ?, ?)", full_table_name);
            
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return rc;
        
        char name[100], description[256], image_url[255];
        
        generate_data(name, sizeof(name), DATA_MODE_LOREM, record_id);
        generate_data(description, sizeof(description), config->data_mode, record_id);
        snprintf(image_url, sizeof(image_url), "https://example.com/images/category_%d.jpg", record_id);
        
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, (record_id > 10) ? (record_id % 10) + 1 : 0);
        sqlite3_bind_text(stmt, 3, description, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, image_url, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 5, record_id % 100);
        sqlite3_bind_int(stmt, 6, (record_id % 20 == 0) ? 0 : 1);
        
    } else if (strcmp(table_name, "activity_logs") == 0) {
        // Activity logs table data
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s (user_id, action, resource_type, resource_id, ip_address, user_agent, details) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)", full_table_name);
            
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return rc;
        
        char action[50], resource_type[50], ip_address[45], user_agent[256], details[512];
        const char *actions[] = {"login", "logout", "create", "update", "delete", "view"};
        const char *resources[] = {"user", "product", "order", "category"};
        
        strcpy(action, actions[record_id % 6]);
        strcpy(resource_type, resources[record_id % 4]);
        snprintf(ip_address, sizeof(ip_address), "192.168.%d.%d", rand() % 256, rand() % 256);
        generate_data(user_agent, sizeof(user_agent), DATA_MODE_RANDOM, record_id);
        generate_data(details, sizeof(details), config->data_mode, record_id);
        
        sqlite3_bind_int(stmt, 1, (record_id % 1000) + 1);
        sqlite3_bind_text(stmt, 2, action, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, resource_type, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, record_id);
        sqlite3_bind_text(stmt, 5, ip_address, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, user_agent, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, details, -1, SQLITE_STATIC);
        
    } else if (strcmp(table_name, "user_sessions") == 0) {
        // User sessions table data
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s (session_id, user_id, ip_address, user_agent, expires_at) "
            "VALUES (?, ?, ?, ?, datetime('now', '+1 day'))", full_table_name);
            
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return rc;
        
        char session_id[255], ip_address[45], user_agent[256];
        
        snprintf(session_id, sizeof(session_id), "sess_%08x_%08x", rand(), rand());
        snprintf(ip_address, sizeof(ip_address), "10.0.%d.%d", rand() % 256, rand() % 256);
        generate_data(user_agent, sizeof(user_agent), DATA_MODE_RANDOM, record_id);
        
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, (record_id % 1000) + 1);
        sqlite3_bind_text(stmt, 3, ip_address, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, user_agent, -1, SQLITE_STATIC);
        
    } else if (strcmp(table_name, "app_settings") == 0) {
        // App settings table data
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s (key, value, type, category, description, is_public) "
            "VALUES (?, ?, ?, ?, ?, ?)", full_table_name);
            
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return rc;
        
        char key[100], value[512], type[20], category[50], description[256];
        const char *types[] = {"string", "integer", "boolean", "json"};
        const char *categories[] = {"system", "ui", "security", "performance"};
        
        snprintf(key, sizeof(key), "setting_%d", record_id);
        generate_data(value, sizeof(value), config->data_mode, record_id);
        strcpy(type, types[record_id % 4]);
        strcpy(category, categories[record_id % 4]);
        generate_data(description, sizeof(description), DATA_MODE_LOREM, record_id);
        
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, category, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, description, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 6, (record_id % 5 == 0) ? 1 : 0);
    }
    
    if (stmt) {
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
    }
    
    return SQLITE_ERROR;
}

// Generate database content with realistic data - optimized version
static int generate_database_content(sqlite3 *db, GeneratorConfig *config) {
    int record_id = 0;
    long current_size = 0;
    time_t start_time = time(NULL);
    int records_inserted = 0;
    int size_check_counter = 0;  // Only check file size periodically
    
    // Increase batch size for better performance
    int effective_batch_size = config->batch_size * 5;  // 5x larger batches
    
    // Start transaction
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    printf("开始生成数据库内容...\n");
    printf("目标大小: %ld 字节 (%.2f MB)\n", config->target_size, config->target_size / (1024.0 * 1024.0));
    printf("数据模式: ");
    switch (config->data_mode) {
        case DATA_MODE_RANDOM: printf("随机数据\n"); break;
        case DATA_MODE_SEQUENTIAL: printf("顺序数据\n"); break;
        case DATA_MODE_LOREM: printf("Lorem ipsum\n"); break;
        case DATA_MODE_BINARY: printf("二进制数据\n"); break;
        case DATA_MODE_MIXED: printf("混合数据\n"); break;
    }
    printf("表数量: %d\n", config->table_count);
    printf("批量大小优化: %d -> %d\n", config->batch_size, effective_batch_size);
    printf("\n");
    
    while (current_size < config->target_size) {
        int table_index = record_id % config->table_count;
        int table_def_index = table_index % table_definitions_count;
        const char *table_name = table_definitions[table_def_index].name;
        
        int table_suffix = -1;
        if (config->table_count > table_definitions_count) {
            table_suffix = table_index / table_definitions_count;
        }
        
        int rc = generate_table_data(db, table_name, table_suffix, config, record_id);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "错误: 插入数据失败: %s\n", sqlite3_errmsg(db));
            break;
        }
        
        record_id++;
        records_inserted++;
        
        // Commit transaction periodically with larger batches
        if (record_id % effective_batch_size == 0) {
            sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
            sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
            
            // Only check file size every few commits to reduce I/O
            size_check_counter++;
            if (size_check_counter >= 3) {  // Check size every 3 commits
                current_size = get_file_size(config->output_file);
                size_check_counter = 0;
                
                if (current_size >= config->target_size) {
                    break;
                }
            }
            
            // Less frequent progress updates
            if (config->verbose || record_id % (effective_batch_size * 2) == 0) {
                if (current_size == 0) current_size = get_file_size(config->output_file);
                time_t current_time = time(NULL);
                double elapsed = difftime(current_time, start_time);
                double progress = (double)current_size / config->target_size * 100.0;
                
                printf("\r进度: %.1f%% (%ld/%ld 字节) - %d 记录 - %.1f 记录/秒",
                       progress, current_size, config->target_size, records_inserted,
                       records_inserted / (elapsed > 0 ? elapsed : 1));
                fflush(stdout);
            }
        }
    }
    
    // Final commit
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    printf("\n");
    
    printf("✓ 数据生成完成: %d 记录插入到 %d 个表中\n", records_inserted, config->table_count);
    return SQLITE_OK;
}

// Parse size string (e.g., "10MB", "500KB", "2GB")
static double parse_size_string(const char *size_str) {
    if (!size_str) return 0;
    
    char *endptr;
    double value = strtod(size_str, &endptr);
    
    if (value <= 0) return 0;
    
    // Handle size suffixes
    if (*endptr) {
        if (strcasecmp(endptr, "B") == 0) {
            // bytes - no change
        } else if (strcasecmp(endptr, "KB") == 0 || strcasecmp(endptr, "K") == 0) {
            value *= 1024;
        } else if (strcasecmp(endptr, "MB") == 0 || strcasecmp(endptr, "M") == 0) {
            value *= 1024 * 1024;
        } else if (strcasecmp(endptr, "GB") == 0 || strcasecmp(endptr, "G") == 0) {
            value *= 1024 * 1024 * 1024;
            printf("%f\n", value);
        } else {
            return 0;  // Invalid suffix
        }
    }
    
    return value;
}

// Parse page size string
static uint32_t parse_page_size(const char *size_str) {
    if (!size_str) return 0;
    
    char *endptr;
    long value = strtol(size_str, &endptr, 10);
    
    if (value <= 0) return 0;
    
    // Handle size suffixes
    if (*endptr) {
        if (strcmp(endptr, "K") == 0 || strcmp(endptr, "k") == 0) {
            value *= 1024;
        } else if (strcmp(endptr, "M") == 0 || strcmp(endptr, "m") == 0) {
            value *= 1024 * 1024;
        } else {
            return 0;  // Invalid suffix
        }
    }
    
    // Validate range
    if (value < CCVFS_MIN_PAGE_SIZE || value > CCVFS_MAX_PAGE_SIZE) {
        return 0;
    }
    
    // Check if power of 2
    if ((value & (value - 1)) != 0) {
        return 0;
    }
    
    return (uint32_t)value;
}

// Print usage information
static void print_usage(const char *program_name) {
    printf("数据库生成工具 - 创建任意大小的压缩或非压缩数据库\n");
    printf("用法: %s [选项] <输出文件> <目标大小>\n\n", program_name);
    
    printf("参数:\n");
    printf("  <输出文件>        输出数据库文件路径\n");
    printf("  <目标大小>        目标文件大小 (例如: 10MB, 500KB, 2GB)\n\n");
    
    printf("选项:\n");
    printf("  -c, --compress              启用压缩 (使用CCVFS)\n");
    printf("  -a, --compress-algo <算法>  压缩算法 (rle, lz4, zlib, 默认: zlib)\n");
    printf("  -e, --encrypt-algo <算法>   加密算法 (xor, aes128, aes256, chacha20)\n");
    printf("  -b, --page-size <大小>     压缩页大小 (1K-1M, 默认: 64K)\n");
    printf("  -l, --level <等级>          压缩等级 (1-9, 默认: 6)\n");
    printf("  -m, --mode <模式>           数据生成模式:\n");
    printf("                                random    - 随机文本数据 (默认)\n");
    printf("                                sequential - 顺序数字和文本\n");
    printf("                                lorem     - Lorem ipsum 文本\n");
    printf("                                binary    - 二进制数据\n");
    printf("                                mixed     - 混合数据类型\n");
    printf("  -r, --record-size <大小>    平均记录大小 (字节, 默认: 1024)\n");
    printf("  -t, --tables <数量>         创建表的数量 (默认: 1)\n");
    printf("  -batch, --batch-size <大小> 每个事务的记录数 (默认: 1000)\n");
    printf("  --no-wal                    禁用WAL模式，使用DELETE journal模式\n");
    printf("  -v, --verbose               详细输出\n");
    printf("  -h, --help                  显示帮助信息\n\n");
    
    printf("示例:\n");
    printf("  %s test.db 10MB                          # 创建10MB非压缩数据库\n", program_name);
    printf("  %s -c test.ccvfs 50MB                    # 创建50MB压缩数据库\n", program_name);
    printf("  %s -c -a zlib -b 4K test.ccvfs 100MB    # 使用zlib和4KB页压缩\n", program_name);
    printf("  %s -m lorem -r 2048 -t 5 test.db 25MB   # Lorem文本,2KB记录,5个表\n", program_name);
    printf("  %s -c -e aes128 secure.ccvfs 1GB        # 压缩+AES128加密的1GB数据库\n", program_name);
}

int main(int argc, char *argv[]) {
    GeneratorConfig config = {
        .output_file = NULL,
        .target_size = 0,
        .use_compression = 0,
        .compress_algorithm = "zlib",
        .encrypt_algorithm = NULL,
        .page_size = 0,  // Use default
        .compression_level = 6,
        .data_mode = DATA_MODE_RANDOM,
        .record_size = 1024,
        .table_count = 1,
        .verbose = 0,
        .batch_size = 1000,
        .use_wal_mode = 1  // Default to WAL mode
    };
    
    static struct option long_options[] = {
        {"compress",        no_argument,       0, 'c'},
        {"compress-algo",   required_argument, 0, 'a'},
        {"encrypt-algo",    required_argument, 0, 'e'},
        {"page-size",      required_argument, 0, 'b'},
        {"level",           required_argument, 0, 'l'},
        {"mode",            required_argument, 0, 'm'},
        {"record-size",     required_argument, 0, 'r'},
        {"tables",          required_argument, 0, 't'},
        {"batch-size",      required_argument, 0, 1000},
        {"no-wal",          no_argument,       0, 1001},
        {"verbose",         no_argument,       0, 'v'},
        {"help",            no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "ca:e:b:l:m:r:t:vh", long_options, NULL)) != -1) {
        switch (c) {
            case 'c':
                config.use_compression = 1;
                break;
            case 'a':
                config.compress_algorithm = optarg;
                break;
            case 'e':
                config.encrypt_algorithm = optarg;
                if (strcmp(config.encrypt_algorithm, "none") == 0) {
                    config.encrypt_algorithm = NULL;
                }
                break;
            case 'b':
                config.page_size = parse_page_size(optarg);
                if (config.page_size == 0) {
                    fprintf(stderr, "错误: 无效的页大小 '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'l':
                config.compression_level = atoi(optarg);
                if (config.compression_level < 1 || config.compression_level > 9) {
                    fprintf(stderr, "错误: 压缩等级必须在1-9之间\n");
                    return 1;
                }
                break;
            case 'm':
                if (strcmp(optarg, "random") == 0) {
                    config.data_mode = DATA_MODE_RANDOM;
                } else if (strcmp(optarg, "sequential") == 0) {
                    config.data_mode = DATA_MODE_SEQUENTIAL;
                } else if (strcmp(optarg, "lorem") == 0) {
                    config.data_mode = DATA_MODE_LOREM;
                } else if (strcmp(optarg, "binary") == 0) {
                    config.data_mode = DATA_MODE_BINARY;
                } else if (strcmp(optarg, "mixed") == 0) {
                    config.data_mode = DATA_MODE_MIXED;
                } else {
                    fprintf(stderr, "错误: 无效的数据模式 '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'r':
                config.record_size = atoi(optarg);
                if (config.record_size < 100 || config.record_size > 1000000) {
                    fprintf(stderr, "错误: 记录大小必须在100-1000000字节之间\n");
                    return 1;
                }
                break;
            case 't':
                config.table_count = atoi(optarg);
                if (config.table_count < 1 || config.table_count > 100) {
                    fprintf(stderr, "错误: 表数量必须在1-100之间\n");
                    return 1;
                }
                break;
            case 1000:  // --batch-size
                config.batch_size = atoi(optarg);
                if (config.batch_size < 10 || config.batch_size > 100000) {
                    fprintf(stderr, "错误: 批量大小必须在10-100000之间\n");
                    return 1;
                }
                break;
            case 1001:  // --no-wal
                config.use_wal_mode = 0;
                break;
            case 'v':
                config.verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case '?':
                fprintf(stderr, "未知选项。使用 -h 查看帮助。\n");
                return 1;
            default:
                break;
        }
    }
    
    // Validate arguments
    if (optind + 2 > argc) {
        fprintf(stderr, "错误: 缺少必需参数\n");
        print_usage(argv[0]);
        return 1;
    }
    
    config.output_file = argv[optind];
    config.target_size = parse_size_string(argv[optind + 1]);
    
    if (config.target_size <= 0) {
        fprintf(stderr, "错误: 无效的目标大小 '%s' %ld\n", argv[optind + 1], config.target_size);
        return 1;
    }
    
    // Optimize batch size based on target size
    if (config.target_size > 100 * 1024 * 1024) {  // > 100MB
        config.batch_size = 5000;  // Larger batches for big files
    } else if (config.target_size > 10 * 1024 * 1024) {  // > 10MB
        config.batch_size = 2000;
    }
    
    // Initialize random seed
    srand((unsigned int)time(NULL));
    
    printf("=== 数据库生成工具 ===\n");
    printf("输出文件: %s\n", config.output_file);
    printf("目标大小: %f 字节 (%.2f MB)\n", config.target_size, config.target_size / (1024.0 * 1024.0));
    printf("压缩: %s\n", config.use_compression ? "是" : "否");
    printf("Journal模式: %s\n", config.use_wal_mode ? "WAL" : "DELETE");
    if (config.use_compression) {
        printf("压缩算法: %s\n", config.compress_algorithm);
        printf("加密算法: %s\n", config.encrypt_algorithm ? config.encrypt_algorithm : "无");
        printf("页大小: %s\n", config.page_size > 0 ? "自定义" : "64KB (默认)");
        printf("压缩等级: %d\n", config.compression_level);
    }
    printf("\n");
    
    // Remove existing file
    remove(config.output_file);
    
    sqlite3 *db = NULL;
    int rc;
    
    if (config.use_compression) {
        // Create CCVFS for compression
        rc = sqlite3_ccvfs_create("generator_vfs", NULL, 
                                  config.compress_algorithm, 
                                  config.encrypt_algorithm,
                                  config.page_size,
                                  CCVFS_CREATE_OFFLINE);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "错误: 创建压缩VFS失败: %d\n", rc);
            return 1;
        }
        
        // Open database with compression
        rc = sqlite3_open_v2(config.output_file, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             "generator_vfs");
    } else {
        // Open regular database
        rc = sqlite3_open_v2(config.output_file, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             NULL);
    }
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "错误: 打开数据库失败: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        if (config.use_compression) {
            sqlite3_ccvfs_destroy("generator_vfs");
        }
        return 1;
    }
    
    // Configure database for performance
    if (config.use_wal_mode) {
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    } else {
        sqlite3_exec(db, "PRAGMA journal_mode=DELETE", NULL, NULL, NULL);
    }
    sqlite3_exec(db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);  // Faster writes
    sqlite3_exec(db, "PRAGMA cache_size=-8000", NULL, NULL, NULL);  // 8MB cache
    sqlite3_exec(db, "PRAGMA temp_store=MEMORY", NULL, NULL, NULL);  // Memory temp storage
    sqlite3_exec(db, "PRAGMA mmap_size=268435456", NULL, NULL, NULL);  // 256MB mmap
    
    time_t start_time = time(NULL);
    
    // Create tables
    rc = create_tables(db, &config);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        if (config.use_compression) {
            sqlite3_ccvfs_destroy("generator_vfs");
        }
        return 1;
    }
    
    // Generate content
    rc = generate_database_content(db, &config);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        if (config.use_compression) {
            sqlite3_ccvfs_destroy("generator_vfs");
        }
        return 1;
    }
    
    // Close database
    sqlite3_close(db);
    
    if (config.use_compression) {
        sqlite3_ccvfs_destroy("generator_vfs");
    }
    
    time_t end_time = time(NULL);
    double elapsed = difftime(end_time, start_time);
    
    // Get final file size
    long final_size = get_file_size(config.output_file);
    
    printf("\n=== 生成完成 ===\n");
    printf("最终文件大小: %ld 字节 (%.2f MB)\n", final_size, final_size / (1024.0 * 1024.0));
    printf("目标达成率: %.2f%%\n", (double)final_size / config.target_size * 100.0);
    printf("用时: %.0f 秒\n", elapsed);
    printf("生成速度: %.2f MB/秒\n", (final_size / (1024.0 * 1024.0)) / (elapsed > 0 ? elapsed : 1));
    
    if (config.use_compression) {
        printf("\n数据库已使用CCVFS压缩格式保存\n");
        printf("要解压缩，请使用: ./db_tool decompress %s output.db\n", config.output_file);
    }
    
    return 0;
}