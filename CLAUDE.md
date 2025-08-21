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

**Unified system test suite (recommended):**
```bash
# From build directory
cd build

# Run all system tests
ctest -L SystemTest

# Run specific test categories
ctest -L Basic          # Basic functionality tests
ctest -L Performance    # Performance and stress tests
ctest -L Storage        # Hole detection and storage tests
ctest -L Buffer         # Buffer management tests
ctest -L Tools          # Database tools tests

# Run individual system tests
ctest -R SystemTest_VFS_Connection
ctest -R SystemTest_Simple_DB
ctest -R SystemTest_Large_DB_Stress

# Run system tests with verbose output
ctest -L SystemTest -V

# Run all tests (unit + system)
ctest

# Direct execution of unified test runner
./test/st/system_tests --help
./test/st/system_tests --list
./test/st/system_tests vfs_connection
./test/st/system_tests --all --verbose
```

**Unit test suite:**
```bash
# Run all unit tests
ctest -R unit_tests

# Run specific unit test categories
ctest -R BasicTests       # Basic tests (recommended first)
ctest -R CoreTests        # Core tests
ctest -R CompressionTests # Compression tests
ctest -R BatchWriterTests # Batch writer tests
ctest -R IntegrationTests # Integration tests

# Run with verbose output
ctest -R unit_tests -V
ctest --output-on-failure # Show output on failure
```

**Database tools:**
```bash
./db_tool.exe compress input.db output.ccvfs --compress-algo zlib
./db_tool.exe decompress input.ccvfs output.db
./db_tool.exe compare db1.db db2.db  # Compare database contents
./db_tool.exe generate test.db 100M  # Generate test databases
```

**Interactive shell:**
```bash
./shell.exe  # SQLite shell with CCVFS support
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
- Default page size: 8KB (configurable via `CCVFS_DEFAULT_PAGE_SIZE`)
- Each page processed independently for compression and encryption
- Block headers contain metadata for validation and reconstruction

## Debugging Features

The codebase includes comprehensive debug logging controlled by compile-time macros:
- `DEBUG`: Basic debug information
- `VERBOSE`: Detailed verbose logging
- Four log levels: ERROR, INFO, DEBUG, VERBOSE

Debug output provides detailed information about VFS operations, algorithm selection, page processing, and error conditions.

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