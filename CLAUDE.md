# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a SQLite Virtual File System (VFS) extension that provides transparent compression and encryption for database files. The project implements a custom VFS layer that sits between SQLite and the underlying file system, automatically compressing and encrypting data during writes and decompressing/decrypting during reads.

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

- **VFS Layer** (`src/compress_vfs.c`): Implements `sqlite3_vfs` interface, intercepts all file operations
- **IO Methods Layer**: Implements `sqlite3_io_methods` interface for actual file read/write operations  
- **Codec Layer**: Handles compression/decompression and encryption/decryption operations
- **Algorithm Interface Layer**: Pluggable architecture supporting custom compression and encryption algorithms

### Data Structures

- **CCVFS**: Main VFS structure containing algorithm pointers and configuration
- **CCVFSFile**: File wrapper structure that wraps the actual file handle
- **CCVFSBlockHeader**: Block header structure with magic number, sequence, checksum, and flags

## Development Commands

### Building the Project

**Primary build method (CMake):**
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

**Alternative build (Windows batch):**
```bash
build.bat
```

**Debug builds:**
```bash
cmake -DENABLE_DEBUG=ON -DENABLE_VERBOSE=ON ..
cmake --build .
```

### Running Tests

**Basic functionality test:**
```bash
# From build directory
./test_main
# With debug output
./test_main --debug
```

**Large database tests:**
```bash
./large_db_test        # Basic large data test
./large_db_test_utf8   # UTF-8 data test  
./large_db_zlib_test   # Zlib compression test
```

**Interactive shell:**
```bash
./shell
```

### Project Structure

- `include/compress_vfs.h`: Public API definitions and algorithm interfaces
- `src/compress_vfs.c`: Main VFS implementation (~1000+ lines)
- `src/shell.c`: Interactive SQLite shell with VFS support
- `sqlite3/`: Embedded SQLite source code
- `test/`: Test programs for various scenarios
- `DESIGN.md`: Detailed technical design documentation
- `COMPRESSED_FILE_FORMAT.md`: File format specification

## Algorithm Support

### Built-in Algorithms
- **Compression**: RLE (Run-Length Encoding), LZ4, Zlib  
- **Encryption**: XOR (simple), AES-128, AES-256, ChaCha20

### Custom Algorithm Registration
Use `sqlite3_ccvfs_register_compress_algorithm()` and `sqlite3_ccvfs_register_encrypt_algorithm()` to register custom implementations following the `CompressAlgorithm` and `EncryptAlgorithm` interfaces.

## Usage Patterns

### Basic VFS Creation
```c
// Create VFS with specific algorithms
sqlite3_ccvfs_create("ccvfs", NULL, "rle", "xor");

// Open database with custom VFS
sqlite3_open_v2("test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "ccvfs");
```

### Block-based Processing
- Default block size: 8KB (configurable via `CCVFS_DEFAULT_BLOCK_SIZE`)
- Each block processed independently for compression and encryption
- Block headers contain metadata for validation and reconstruction

## Debugging Features

The codebase includes comprehensive debug logging controlled by compile-time macros:
- `DEBUG`: Basic debug information
- `VERBOSE`: Detailed verbose logging
- Four log levels: ERROR, INFO, DEBUG, VERBOSE

Debug output provides detailed information about VFS operations, algorithm selection, block processing, and error conditions.

## Important Technical Constraints

- Not compatible with WAL mode online backup
- Encrypted databases cannot be opened with standard SQLite tools
- Performance overhead depends on chosen algorithms
- Block-based architecture optimized for random access patterns
- Requires proper key management for encrypted databases

## Development Notes

- The project is implemented in C99 standard
- Uses SQLite's VFS interface version 3
- Thread-safety considerations built into the design
- Extensive error handling and validation throughout
- Modular algorithm interface allows easy extension