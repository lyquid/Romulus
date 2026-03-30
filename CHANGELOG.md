# CHANGELOG

> *ROMULUS imposes order on chaos — and keeps a record of it.*

All notable changes to this project will be documented in this file.
This changelog is automatically generated from [Conventional Commits](https://www.conventionalcommits.org/).

## [Unreleased]

### 🔧 Refactoring

- **CLI**: Replaced all `std::cout`/`std::cerr` with `std::print`/`std::print(stderr, ...)` from C++23 `<print>`; removed `#include <iostream>`

### 🏗️ Build System

- Upgraded CI GCC build from GCC 13 → GCC 14 to support `<print>` (added in libstdc++ 14)
- Upgraded CI Clang build from libstdc++-13-dev → libstdc++-14-dev and updated `--gcc-install-dir` to `/14`

## [0.1.0] — 2026-03-30

### ⚡ Features

- **Core**: Error handling with `std::expected` (`Result<T>` alias), shared data types, and spdlog-based logging facade
- **Database**: Complete SQLite3 RAII wrapper with WAL mode, foreign keys, prepared statements, transaction guards, and CRUD operations for all 7 tables (systems, dat_versions, games, roms, files, file_matches, rom_status)
- **DAT Import**: LogiqX XML parser (pugixml) with header/game/ROM extraction, hash normalization, and region detection; local file validation with SHA1 checksums for version tracking
- **Scanner**: Recursive directory scanning with extension filtering, multi-threaded hashing (CRC32 + MD5 + SHA1 in a single pass via OpenSSL), archive support (zip/7z/tar via libarchive) with zero-disk streaming, and skip-checking for unchanged files
- **Engine**: Priority-based matching (SHA1 > MD5 > CRC32) with exact/partial match detection; ROM classification (Verified, Missing, Unverified, Mismatch) with per-system filtering
- **Reports**: Summary, Missing, Duplicates, and Unverified reports in Text, CSV, and JSON formats
- **Service**: High-level `RomulusService` facade orchestrating all modules (import → scan → verify → report)
- **CLI**: CLI11-based interface with 7 subcommands: `import-dat`, `scan`, `verify`, `sync`, `report`, `systems`, `status`

### 🏗️ Build System

- CMake 3.25+ with C++23 standard requirement
- vcpkg manifest with 8 dependencies (sqlite3, spdlog, pugixml, cli11, libarchive, openssl, nlohmann-json, gtest)
- CMake presets for dev/release/CI (MSVC, GCC 14, Clang 17)
- Compiler warnings module (`romulus_set_warnings`) with `-Werror`/`/WX`
- clang-tidy integration (optional)
- CI pipeline (GitHub Actions) with build matrix and format checks
- Semantic release automation with conventional commits

### 🧪 Testing

- 6 unit test suites: DatParser, HashService, Matcher, Classifier, Database, ReportGenerator
- 2 integration test suites: FullScan (end-to-end pipeline), DatUpdate (idempotency)
- 27 tests total, all passing with zero warnings

### 📝 Documentation

- Added a repo instruction to update CHANGELOG.md for repository changes unless explicitly waived
