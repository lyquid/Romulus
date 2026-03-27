# Changelog

All notable changes to ROMULUS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2024-01-01

### Added
- Initial project structure with C++23 standard
- SQLite3 database layer with schema migrations
- No-Intro DAT file fetching and parsing (pugixml)
- CRC32/MD5/SHA1 hash service (OpenSSL EVP API + constexpr CRC32)
- Recursive ROM scanner
- ROM-to-file matcher (exact, CRC-only, renamed)
- ROM classifier (Have/Missing/Duplicate/BadDump)
- Text report generator with per-system statistics
- CMake build system with vcpkg manifest mode
- Catch2 unit tests for all modules
- clang-format and clang-tidy configuration
