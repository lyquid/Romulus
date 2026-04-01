# CHANGELOG

> *ROMULUS imposes order on chaos — and keeps a record of it.*

All notable changes to this project will be documented in this file.
This changelog is automatically generated from [Conventional Commits](https://www.conventionalcommits.org/).

## [Unreleased]

### ⚡ Features

- **GUI**: Added modular ImGui + GLFW desktop GUI (`apps/gui/`) with DAT import, folder scanning, verification, database purge, collection summary display, and scrollable file table
- **GUI**: Added `--no-gui` launch parameter to skip GUI initialization for headless operation
- **GUI**: Added `ROMULUS_ENABLE_GUI` CMake option (default ON) to opt out of building the GUI
- **Service**: Added `get_all_files()` and `execute_raw()` methods to `RomulusService` for GUI data access and admin operations
- **SHA256 Hashing**: Added SHA256 computation to the `HashService` (alongside the existing CRC32, MD5, and SHA1 pass); all four digests are now computed in a single I/O pass via OpenSSL EVP
- **Database — SHA256 columns**: Added `sha256 TEXT UNIQUE` to the `roms` table and `sha256 TEXT NOT NULL` to the `files` table; both indexed for fast look-up; existing databases are upgraded automatically via `ALTER TABLE ADD COLUMN` on open
- **Database — `find_rom_by_sha256`**: New query method on `Database` and corresponding matcher priority (SHA256 takes precedence over SHA1 > MD5 > CRC32)
- **Database — filename/path split**: Added `filename TEXT NOT NULL` column to the `files` table (stores just the file/entry name); `path` continues to store the unique full path; useful for display and search
- **Scanner**: `FileInfo` is now populated with `sha256` and `filename` on every scan; SHA256 is logged at INFO level when a file is hashed

### 🔧 Refactoring

- **CLI**: Replaced all `std::cout`/`std::cerr` with `std::print`/`std::print(stderr, ...)` from C++23 `<print>`; removed `#include <iostream>`

### 🏗️ Build System

- Upgraded CI GCC build from GCC 13 → GCC 14 to support `<print>` (added in libstdc++ 14)
- Upgraded CI Clang build from Clang 17 → Clang 18 for better C++23 `std::expected` support on Ubuntu 24.04
- Updated CI clang-format check from clang-format-17 → clang-format-18 to match Clang toolchain version
- Updated vcpkg baseline from `c82f74667...` → `c3867e714...` (2026.03.18 release) to fix Windows/MSVC CLI11 build failure caused by outdated msys2 runtime package
- Fixed semantic-release: added missing `conventional-changelog-conventionalcommits` to extra_plugins in release workflow
- Applied clang-format-18 to all source files to fix CI formatting violations

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
