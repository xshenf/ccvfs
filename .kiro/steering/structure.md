# CCVFS Project Structure

## Directory Organization

### Core Implementation
- **`include/`**: Public header files and API definitions
  - `ccvfs.h`: Main public API and data structures
  - `ccvfs_*.h`: Internal module headers (core, io, algorithm, etc.)
- **`src/`**: Source code implementation
  - `ccvfs.c`: Main API implementation
  - `ccvfs_core.c`: Core VFS functionality
  - `ccvfs_io.c`: File I/O operations
  - `ccvfs_algorithm.c`: Compression/encryption algorithms
  - `ccvfs_page.c`: Page management
  - `ccvfs_batch_writer.c`: Batch write operations
  - `ccvfs_utils.c`: Utility functions
  - Tool implementations: `db_tool.c`, `db_generator.c`, etc.

### Dependencies
- **`sqlite3/`**: Bundled SQLite source code
  - `sqlite3.c`, `sqlite3.h`: Core SQLite implementation
  - `shell.c`: SQLite shell program
- **`lib/`**: Pre-built libraries (Windows DLLs, etc.)

### Testing
- **`test/`**: Test programs and verification
  - Individual test files: `*_test.c`
  - **`test/ut/`**: Unit test framework
    - `test_framework.h/.c`: Custom test framework
    - `test_*.c`: Modular unit tests
    - `CMakeLists.txt`: Unit test build configuration

### Documentation
- **`docs/`**: Comprehensive technical documentation
  - `README.md`: Documentation index
  - `CCVFS_USER_GUIDE.md`: User guide and best practices
  - `COMPRESSED_DB_OPERATIONS.md`: Technical implementation details
  - Various specialized documentation files

### Build System
- **Root level**: Build configuration and project metadata
  - `CMakeLists.txt`: Main build configuration
  - `README.md`: Project overview (Chinese)
  - `.gitignore`: Version control exclusions

## Code Organization Patterns

### Header Structure
- Public APIs in `include/ccvfs.h`
- Internal APIs in `include/ccvfs_internal.h`
- Module-specific headers follow `ccvfs_<module>.h` pattern

### Source File Conventions
- Each module has corresponding `.c` file in `src/`
- Tool programs are standalone `.c` files
- Test programs follow `*_test.c` naming convention

### Naming Conventions
- **Functions**: `ccvfs_*` prefix for internal functions, `sqlite3_ccvfs_*` for public API
- **Types**: `CCVFS*` for main structures (e.g., `CCVFSFile`, `CCVFSPageIndex`)
- **Constants**: `CCVFS_*` prefix (e.g., `CCVFS_DEFAULT_PAGE_SIZE`)
- **Macros**: `CCVFS_*` prefix for logging and debugging

### Module Boundaries
- **Core**: VFS implementation and file management
- **IO**: Low-level file operations and page handling
- **Algorithm**: Compression and encryption implementations
- **Page**: Page-level operations and caching
- **Batch Writer**: Write buffering and optimization
- **Utils**: Common utilities and helper functions

## Build Artifacts

### Libraries
- `libsqlitecc.a`: Main static library containing all CCVFS functionality
- Platform-specific shared libraries in `lib/` directory

### Executables
- `shell`: Enhanced SQLite shell with CCVFS support
- `db_tool`: Database compression/decompression utility
- `db_generator`: Test database generation tool
- Various test executables for validation and performance testing