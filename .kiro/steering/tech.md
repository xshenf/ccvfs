# CCVFS Technology Stack

## Build System

- **CMake**: Primary build system (minimum version 3.10)
- **C Standard**: C99 compliance required
- **Platform**: Cross-platform (Windows, Linux, macOS)

## Core Dependencies

- **SQLite3**: Core database engine (bundled in `sqlite3/` directory)
- **Zlib**: Required compression library
- **LZ4**: Optional high-performance compression (detected via pkg-config)
- **LZMA**: Optional compression algorithm
- **OpenSSL**: For encryption algorithms (AES, etc.)

## Build Configuration

### Standard Build
```bash
mkdir build
cd build
cmake ..
make
```

### Debug Build
```bash
cmake -DENABLE_DEBUG=ON -DENABLE_VERBOSE=ON ..
make
```

### Available CMake Options
- `ENABLE_DEBUG`: Enable debug output and assertions
- `ENABLE_VERBOSE`: Enable detailed logging
- `HAVE_LZ4`: Automatically detected if LZ4 library found
- `HAVE_LZMA`: Automatically detected if LZMA library found

## Testing

### Unit Tests
```bash
# Run all tests
ctest

# Run specific test categories
ctest -L unit
ctest -R BasicTests
ctest -R CoreTests
ctest -R CompressionTests
ctest -R BatchWriterTests
ctest -R IntegrationTests

# Verbose output
ctest -V
ctest --output-on-failure
```

### Manual Testing Tools
```bash
# Database compression tool
./db_tool compress source.db target.ccvfs

# Database generation for testing
./db_generator -c -a zlib test.ccvfs 10MB

# VFS connection testing
./vfs_connection_test

# Performance testing
./large_db_stress_test
```

## Compilation Flags

- `SQLITE_ENABLE_FTS5`: Full-text search support
- `SQLITE_ENABLE_CEROD`: Compression extension support
- `DEBUG`: Enable debug logging (conditional)
- `VERBOSE`: Enable verbose logging (conditional)

## Library Structure

- **Static Library**: `libsqlitecc.a` (main library)
- **Shared Library**: `ccvfs.dll` (Windows) / `libccvfs.so` (Linux)
- **Tools**: Various executables for testing and database management