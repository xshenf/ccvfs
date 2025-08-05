# Implementation Plan

- [x] 1. Define hole tracking data structures and extend CCVFSFile


  - Add CCVFSSpaceHole and CCVFSHoleManager structures to ccvfs.h
  - Extend CCVFSFile structure with hole_manager field and statistics counters
  - Define configuration constants for hole management (max holes, min hole size)
  - _Requirements: 2.1, 2.2, 3.1_




- [ ] 2. Implement basic hole manager initialization and cleanup
  - [ ] 2.1 Create ccvfs_init_hole_manager function
    - Initialize hole manager with default configuration
    - Allocate initial memory for hole tracking

    - Set up configuration parameters from VFS settings
    - _Requirements: 2.1, 3.1_

  - [ ] 2.2 Create ccvfs_cleanup_hole_manager function
    - Free all allocated hole tracking memory


    - Clean up linked list of holes
    - Reset statistics counters
    - _Requirements: 2.1_




  - [ ] 2.3 Integrate hole manager lifecycle with file operations
    - Call init_hole_manager in ccvfsOpen when creating/opening CCVFS files
    - Call cleanup_hole_manager in ccvfsIoClose before file closure
    - Handle initialization failures gracefully
    - _Requirements: 2.1, 4.3_



- [ ] 3. Implement hole creation and tracking functions
  - [ ] 3.1 Create ccvfs_add_hole function
    - Add new hole to linked list with proper sorting by offset


    - Validate hole parameters (offset, size) for consistency
    - Check for overlaps with existing holes and merge if adjacent
    - Respect max_holes limit and remove smallest holes if needed

    - _Requirements: 1.1, 2.3, 4.1_



  - [ ] 3.2 Create ccvfs_remove_hole function
    - Remove hole from linked list when space is allocated
    - Handle partial hole allocation (split hole if only part is used)
    - Update hole statistics and counters


    - _Requirements: 1.5_

  - [ ] 3.3 Implement hole detection in page deletion/update scenarios
    - Modify writePage function to detect when existing page space is freed


    - Call ccvfs_add_hole when page is deleted or significantly shrunk
    - Handle sparse page conversion (add hole for previous physical space)
    - _Requirements: 1.1_




- [ ] 4. Implement best-fit space allocation algorithm
  - [ ] 4.1 Replace ccvfs_find_best_fit_space stub implementation
    - Implement linear search through hole list for best-fit candidate
    - Select hole with smallest size that still fits required space
    - Calculate and return wasted space for allocation decision

    - Return 0 if no suitable hole found (fall back to end-of-file allocation)
    - _Requirements: 1.2, 1.3, 5.1_

  - [ ] 4.2 Add hole allocation logic and statistics tracking
    - Update hole record when space is allocated (remove or split hole)


    - Increment hole_allocation_count and other relevant statistics
    - Log hole allocation decisions for debugging when enabled
    - _Requirements: 1.5, 5.3_

  - [ ] 4.3 Integrate best-fit allocation with existing writePage logic
    - Ensure hole allocation path in writePage calls ccvfs_find_best_fit_space
    - Handle allocation failures gracefully (fall back to end-of-file)
    - Verify allocated space is actually free before writing data
    - _Requirements: 1.4, 4.1, 4.2_

- [ ] 5. Implement hole optimization and maintenance
  - [ ] 5.1 Create ccvfs_merge_adjacent_holes function
    - Scan hole list for adjacent or overlapping holes
    - Merge adjacent holes into single larger holes
    - Update hole_merge_count statistics
    - Call periodically during normal operations
    - _Requirements: 2.2_

  - [ ] 5.2 Create ccvfs_cleanup_small_holes function
    - Remove holes smaller than min_hole_size threshold
    - Free memory used by removed small holes
    - Update hole_cleanup_count statistics
    - _Requirements: 2.3_

  - [ ] 5.3 Add periodic hole maintenance calls
    - Call merge and cleanup functions during file sync operations
    - Implement threshold-based maintenance (trigger after N operations)
    - Add maintenance call in ccvfsIoSync function
    - _Requirements: 2.2, 2.3_

- [ ] 6. Add configuration and runtime control
  - [ ] 6.1 Add hole detection configuration to CCVFS structure
    - Add enable_hole_detection boolean flag to CCVFS VFS structure
    - Add max_holes and min_hole_size configuration parameters
    - Set default values in sqlite3_ccvfs_create function
    - _Requirements: 3.1, 3.2, 3.3_

  - [ ] 6.2 Implement runtime configuration checks
    - Check enable_hole_detection flag in ccvfs_find_best_fit_space
    - Return 0 immediately if hole detection is disabled
    - Allow dynamic enabling/disabling during file operations
    - _Requirements: 3.2_

  - [ ] 6.3 Add configuration validation and bounds checking
    - Validate max_holes is within reasonable limits (16-1024)
    - Validate min_hole_size is reasonable (16-4096 bytes)
    - Handle invalid configuration gracefully with defaults
    - _Requirements: 3.4_

- [ ] 7. Implement comprehensive error handling and data integrity
  - [ ] 7.1 Add hole validation functions
    - Create ccvfs_validate_hole_integrity function to check hole list consistency
    - Verify holes don't overlap with existing page data
    - Check hole list for corruption (broken links, invalid offsets)
    - _Requirements: 4.1, 4.2_

  - [ ] 7.2 Implement graceful degradation on errors
    - Disable hole detection if corruption is detected
    - Fall back to end-of-file allocation on any hole-related errors
    - Log error conditions for debugging and monitoring
    - _Requirements: 4.3_

  - [ ] 7.3 Add atomic hole operations
    - Ensure hole list updates are atomic to prevent corruption
    - Implement rollback mechanisms for failed hole operations
    - Add critical section protection for hole list modifications
    - _Requirements: 4.2_

- [ ] 8. Add debugging and statistics reporting
  - [ ] 8.1 Enhance debug logging for hole operations
    - Add detailed logging for hole creation, allocation, and cleanup
    - Log hole search results and allocation decisions
    - Include hole statistics in existing file health reporting
    - _Requirements: 4.4_

  - [ ] 8.2 Implement hole statistics collection
    - Track hole allocation success rate and efficiency metrics
    - Calculate fragmentation score based on hole distribution
    - Add hole-related counters to space utilization tracking
    - _Requirements: 5.4_

  - [ ] 8.3 Add hole information to file health reports
    - Include hole count and total hole space in ccvfs_report_file_health
    - Report hole allocation efficiency and fragmentation metrics
    - Add warnings for excessive fragmentation or hole count
    - _Requirements: 4.4_

- [ ] 9. Write comprehensive unit tests for hole detection
  - [ ] 9.1 Create hole manager basic functionality tests
    - Test hole creation, deletion, and list management
    - Test hole merging with various adjacent hole scenarios
    - Test hole cleanup with different size thresholds
    - Verify memory management and leak prevention
    - _Requirements: 1.1, 1.5, 2.2, 2.3_

  - [ ] 9.2 Create best-fit algorithm tests
    - Test exact fit, best fit, and no fit scenarios
    - Test hole splitting when partial space is allocated
    - Test fallback to end-of-file allocation when no holes available
    - Verify wasted space calculations are accurate
    - _Requirements: 1.2, 1.3, 1.4_

  - [ ] 9.3 Create error handling and edge case tests
    - Test behavior with memory allocation failures
    - Test hole corruption detection and recovery
    - Test configuration boundary conditions and invalid values
    - Test concurrent access scenarios and thread safety
    - _Requirements: 4.1, 4.2, 4.3_

- [ ] 10. Integration testing and performance validation
  - [ ] 10.1 Create integration tests with existing CCVFS operations
    - Test hole detection during normal database read/write operations
    - Verify data integrity is maintained when reusing holes
    - Test hole detection with compression and encryption enabled
    - _Requirements: 1.1, 1.2, 4.1, 4.2_

  - [ ] 10.2 Create performance benchmarks
    - Measure hole search performance with varying hole counts
    - Compare allocation performance with and without hole detection
    - Test memory usage under different hole management scenarios
    - _Requirements: 5.1, 5.2_

  - [ ] 10.3 Add regression tests for existing functionality
    - Ensure existing space allocation behavior is preserved when hole detection is disabled
    - Verify no performance regression in append-only scenarios
    - Test backward compatibility with existing CCVFS databases
    - _Requirements: 3.2, 5.1_