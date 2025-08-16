# CCVFS Product Overview

CCVFS (Compressed and Encrypted Virtual File System) is a SQLite VFS extension that provides transparent compression and encryption for SQLite databases.

## Core Features

- **Transparent Compression**: Automatic compression using RLE, LZ4, or Zlib algorithms
- **Optional Encryption**: AES-128/256 and ChaCha20 encryption support
- **SQLite Compatibility**: Works with standard SQLite API without code changes
- **Configurable Page Sizes**: 1KB to 1MB page sizes for different use cases
- **Performance Optimization**: Write buffering, batch operations, and caching

## Use Cases

- **OLTP Systems**: Fast compression with small pages (4-16KB + LZ4)
- **OLAP Systems**: High compression with large pages (64-256KB + Zlib)
- **Data Archival**: Maximum compression with encryption (256KB-1MB + Zlib + AES)
- **Storage Optimization**: 70-90% space reduction for typical databases

## Architecture

The system implements a layered VFS architecture:
- SQLite Engine → CCVFS Layer → Codec Layer → System VFS
- Page-level compression with configurable algorithms
- Index table for efficient random access
- Write buffering for batch operations