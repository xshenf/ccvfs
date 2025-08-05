# Design Document

## Overview

The CCVFS space hole detection system implements an efficient space allocation strategy that tracks and reuses freed space within the database file. Instead of always appending new data to the end of the file, the system maintains a record of available space holes and uses a best-fit algorithm to minimize fragmentation and optimize storage efficiency.

The design focuses on memory efficiency, performance, and data integrity while providing configurable options for different usage patterns.

## Architecture

### Core Components

1. **Hole Tracking Structure**: A compact in-memory data structure that tracks available space holes
2. **Best-Fit Allocator**: An algorithm that finds the most suitable hole for a given space requirement
3. **Hole Management System**: Handles hole creation, merging, and cleanup operations
4. **Configuration System**: Allows runtime configuration of hole detection behavior

### Data Flow

```
Page Write Request
       ↓
Check for suitable holes
       ↓
Best-fit algorithm search
       ↓
Hole found? → Yes → Allocate from hole → Update hole record
       ↓
      No
       ↓
Allocate at file end
       ↓
Update space tracking
```

## Components and Interfaces

### 1. Hole Tracking Data Structure

```c
typedef struct CCVFSSpaceHole {
    sqlite3_int64 offset;        // Starting offset of the hole
    uint32_t size;               // Size of the hole in bytes
    struct CCVFSSpaceHole *next; // Next hole in the list
} CCVFSSpaceHole;

typedef struct CCVFSHoleManager {
    CCVFSSpaceHole *holes;       // Linked list of holes
    uint32_t hole_count;         // Number of tracked holes
    uint32_t max_holes;          // Maximum holes to track
    uint32_t min_hole_size;      // Minimum hole size to track
    int enabled;                 // Whether hole detection is enabled
} CCVFSHoleManager;
```

### 2. Enhanced CCVFSFile Structure

The existing `CCVFSFile` structure will be extended with hole management:

```c
// Add to CCVFSFile structure
CCVFSHoleManager hole_manager;   // Hole tracking system
uint32_t hole_allocation_count;  // Statistics: holes used
uint32_t hole_merge_count;       // Statistics: holes merged
uint32_t hole_cleanup_count;     // Statistics: small holes removed
```

### 3. Core Functions

#### Primary Interface
```c
static sqlite3_int64 ccvfs_find_best_fit_space(CCVFSFile *pFile, uint32_t requiredSize, uint32_t *pWastedSpace);
```

#### Supporting Functions
```c
static int ccvfs_init_hole_manager(CCVFSFile *pFile);
static void ccvfs_cleanup_hole_manager(CCVFSFile *pFile);
static int ccvfs_add_hole(CCVFSFile *pFile, sqlite3_int64 offset, uint32_t size);
static int ccvfs_remove_hole(CCVFSFile *pFile, sqlite3_int64 offset);
static void ccvfs_merge_adjacent_holes(CCVFSFile *pFile);
static void ccvfs_cleanup_small_holes(CCVFSFile *pFile);
```

## Data Models

### Hole Record Structure

Each hole is represented by:
- **Offset**: Physical file offset where the hole starts
- **Size**: Size of the available space in bytes
- **Next**: Pointer to next hole (linked list implementation)

### Hole Manager Configuration

- **enabled**: Boolean flag to enable/disable hole detection
- **max_holes**: Maximum number of holes to track (default: 256)
- **min_hole_size**: Minimum hole size worth tracking (default: 64 bytes)

### Statistics Tracking

Extended statistics for monitoring hole detection effectiveness:
- **hole_allocation_count**: Number of successful hole allocations
- **hole_merge_count**: Number of hole merge operations
- **hole_cleanup_count**: Number of small holes removed
- **fragmentation_score**: Calculated fragmentation metric

## Error Handling

### Graceful Degradation

1. **Memory Allocation Failures**: If hole tracking structures cannot be allocated, fall back to end-of-file allocation
2. **Hole Corruption**: If hole data becomes inconsistent, disable hole detection and continue with append-only mode
3. **Performance Degradation**: If hole search takes too long, implement timeout and fall back to end-of-file allocation

### Data Integrity Protection

1. **Hole Validation**: Verify that holes don't overlap with existing data before allocation
2. **Atomic Operations**: Ensure hole updates are atomic to prevent corruption
3. **Recovery Mechanisms**: Ability to rebuild hole information from file structure if needed

### Error Codes

- **SQLITE_OK**: Successful operation
- **SQLITE_NOMEM**: Memory allocation failure
- **SQLITE_CORRUPT**: Hole data corruption detected
- **SQLITE_BUSY**: Hole detection temporarily disabled due to performance

## Testing Strategy

### Unit Tests

1. **Hole Management Tests**
   - Test hole creation, deletion, and merging
   - Verify hole list integrity
   - Test edge cases (empty list, single hole, overlapping holes)

2. **Best-Fit Algorithm Tests**
   - Test exact fit scenarios
   - Test best-fit selection among multiple candidates
   - Test no-fit scenarios (fall back to end-of-file)

3. **Memory Management Tests**
   - Test memory allocation failures
   - Test cleanup operations
   - Test memory leak prevention

### Integration Tests

1. **Database Operations**
   - Test hole detection during normal database operations
   - Verify data integrity after hole reuse
   - Test performance under various workloads

2. **Configuration Tests**
   - Test enabling/disabling hole detection
   - Test different configuration parameters
   - Test configuration changes during runtime

### Performance Tests

1. **Benchmark Tests**
   - Compare performance with and without hole detection
   - Measure search time for different hole counts
   - Test memory usage under various scenarios

2. **Stress Tests**
   - Test with large numbers of holes
   - Test with frequent allocation/deallocation patterns
   - Test long-running operations

### Regression Tests

1. **Compatibility Tests**
   - Ensure existing functionality remains intact
   - Test backward compatibility with existing databases
   - Verify no performance regression in append-only scenarios

## Implementation Phases

### Phase 1: Basic Hole Tracking
- Implement hole data structure and basic management
- Add hole creation when pages are deleted/updated
- Implement simple linear search for hole allocation

### Phase 2: Best-Fit Algorithm
- Implement efficient best-fit search algorithm
- Add hole merging and cleanup operations
- Integrate with existing space allocation logic

### Phase 3: Configuration and Optimization
- Add configuration options for hole detection
- Implement performance optimizations
- Add comprehensive statistics tracking

### Phase 4: Advanced Features
- Implement hole persistence across file reopens
- Add automatic hole defragmentation
- Implement adaptive hole management strategies