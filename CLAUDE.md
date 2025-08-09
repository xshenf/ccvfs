# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a SQLite Virtual File System (VFS) extension that provides transparent compression and encryption for database files. The project implements a custom VFS layer that sits between SQLite and the underlying file system, automatically compressing and encrypting data during writes and decompressing/decrypting during reads.

The system supports advanced features including:
- Block-based compression with multiple algorithms (RLE, LZ4, Zlib)
- Encryption with multiple algorithms (XOR, AES-128, AES-256, ChaCha20)
- Space hole detection and management for efficient storage
- Batch write buffering for improved write performance
- Configurable page sizes (1KB to 1MB)

## Core Architecture

The system follows a layered architecture:

```
SQLite Engine
     ↓
CCVFS (Custom VFS Implementation)
     ↓ 
Codec Layer (Compression/Decompression + Encryption/Decryption)
     ↓
System Default VFS
```

### Key Components

- **VFS Layer** (`src/ccvfs.c`, `src/ccvfs_core.c`): Implements `sqlite3_vfs` interface, intercepts all file operations
- **IO Methods Layer** (`src/ccvfs_io.c`): Implements `sqlite3_io_methods` interface for actual file read/write operations  
- **Codec Layer** (`src/ccvfs_algorithm.c`): Handles compression/decompression and encryption/decryption operations
- **Page Management** (`src/ccvfs_page.c`): Manages page-level operations and data structures
- **Batch Writer** (`src/ccvfs_batch_writer.c`): Implements write buffering for improved performance
- **Hole Management** (`src/ccvfs_core.c`): Tracks and manages space holes for efficient storage allocation

### Data Structures

- **CCVFS**: Main VFS structure containing algorithm pointers and configuration
- **CCVFSFile**: File wrapper structure that wraps the actual file handle
- **CCVFSFileHeader**: File header structure with metadata and configuration
- **CCVFSPageIndex**: Page index entry for tracking page locations and metadata
- **CCVFSDataPage**: Data page structure with compression/encryption metadata
- **CCVFSHoleManager**: Manages space holes for efficient storage allocation
- **CCVFSBatchWriter**: Implements batch write buffering for performance

## Development Commands

### Building the Project

**Primary build method (CMake):**
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

**Debug builds with verbose output:**
```bash
cmake -DENABLE_DEBUG=ON -DENABLE_VERBOSE=ON ..
cmake --build .
```

### Running Tests

**Essential functionality tests:**
```bash
# From build directory
./simple_db_test          # Basic compression/decompression test
./vfs_connection_test     # VFS connection and operation test
./simple_large_test       # Large database test
./batch_write_buffer_test # Batch write buffering test
./test_hole_detection     # Hole detection functionality test
```

**Performance and stress tests:**
```bash
./large_db_stress_test    # Stress test with large databases
./simple_buffer_test      # Buffer performance test
./improved_batch_writer_test # Improved batch writer test
```

**Interactive shell:**
```bash
./shell
```

### Project Structure

- `include/ccvfs.h`: Public API definitions and algorithm interfaces
- `include/ccvfs_internal.h`: Internal structures and constants
- `src/ccvfs.c`: Main VFS registration and initialization
- `src/ccvfs_core.c`: Core VFS operations (open, delete, access)
- `src/ccvfs_io.c`: File I/O operations with compression/encryption
- `src/ccvfs_algorithm.c`: Compression and encryption algorithm implementations
- `src/ccvfs_batch_writer.c`: Batch write buffering implementation
- `src/ccvfs_page.c`: Page-level operations and management
- `src/ccvfs_utils.c`: Utility functions and helpers
- `src/shell.c`: Interactive SQLite shell with VFS support
- `src/db_tool.c`: Database compression/decompression tool
- `sqlite3/`: Embedded SQLite source code
- `test/`: Test programs for various scenarios
- `docs/`: Documentation and implementation reports

## Algorithm Support

### Built-in Algorithms
- **Compression**: RLE (Run-Length Encoding), LZ4, Zlib  
- **Encryption**: XOR (simple), AES-128, AES-256, ChaCha20

### Custom Algorithm Registration
Use `sqlite3_ccvfs_register_compress_algorithm()` and `sqlite3_ccvfs_register_encrypt_algorithm()` to register custom implementations following the `CompressAlgorithm` and `EncryptAlgorithm` interfaces.

## Usage Patterns

### Basic VFS Creation
```c
// Create VFS with specific algorithms and 64KB page size
sqlite3_ccvfs_create("ccvfs", NULL, "zlib", "aes-256", 65536, 0);

// Open database with custom VFS
sqlite3_open_v2("test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "ccvfs");
```

### Offline Database Compression
```c
// Compress existing SQLite database
sqlite3_ccvfs_compress_database("source.db", "compressed.ccvfs", "zlib", "aes-256", 6);

// Decompress compressed database back to standard format
sqlite3_ccvfs_decompress_database("compressed.ccvfs", "restored.db");
```

### Batch Write Buffering Configuration
```c
// Configure write buffering for better performance
sqlite3_ccvfs_configure_write_buffer("ccvfs", 1, 32, 4*1024*1024, 16);

// Get buffer statistics
uint32_t hits, flushes, merges, total;
sqlite3_ccvfs_get_buffer_stats(db, &hits, &flushes, &merges, &total);

// Force flush buffer
sqlite3_ccvfs_flush_write_buffer(db);
```

### Page-based Processing
- Configurable page sizes from 1KB to 1MB
- Each page processed independently for compression and encryption
- Page headers contain metadata for validation and reconstruction
- Space hole detection for efficient storage allocation

## Debugging Features

The codebase includes comprehensive debug logging controlled by compile-time macros:
- `DEBUG`: Basic debug information
- `VERBOSE`: Detailed verbose logging
- Four log levels: ERROR, INFO, DEBUG, VERBOSE

Debug output provides detailed information about VFS operations, algorithm selection, page processing, buffer operations, and error conditions.

Environment variable for runtime debugging:
```bash
export CCVFS_DEBUG=1  # Enable debug output at runtime
```

## Important Technical Constraints

- Not compatible with WAL mode online backup
- Encrypted databases cannot be opened with standard SQLite tools
- Performance overhead depends on chosen algorithms
- Block-based architecture optimized for random access patterns
- Requires proper key management for encrypted databases
- SQLite auxiliary files (-journal, -wal, -shm) are not compressed/encrypted

## Development Notes

- The project is implemented in C99 standard
- Uses SQLite's VFS interface version 3
- Thread-safety considerations built into the design
- Extensive error handling and validation throughout
- Modular algorithm interface allows easy extension
- Memory management uses SQLite's memory allocation functions
- All file operations go through the underlying VFS for portability

## Recent Feature Additions

### Batch Write Buffering
- Implemented in `src/ccvfs_batch_writer.c`
- Provides significant performance improvements for write-heavy workloads
- Configurable buffer size and automatic flushing
- Statistics tracking for performance monitoring

### Space Hole Detection
- Implemented in `src/ccvfs_core.c`
- Tracks available space in files for efficient storage allocation
- Reduces file fragmentation and improves space utilization
- Configurable hole size thresholds and tracking limits