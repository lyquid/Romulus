# CHANGELOG

> *ROMULUS imposes order on chaos — and keeps a record of it.*

All notable changes to this project will be documented in this file.
This changelog is automatically generated from [Conventional Commits](https://www.conventionalcommits.org/).

## [Unreleased]

### ⚡ Features

- **GUI**: Restructured main window into three tabs — **DATs** (ROM checklist with inline DAT controls), **Folders** (ROM directory management), **Log** (application log)
- **GUI**: DATs tab ROM table now shows SHA1 instead of CRC32 as the hash column
- **GUI**: Folders tab — lists all registered ROM scan directories loaded from the database; supports adding new folders and removing existing ones with an `[X]` button
- **Database**: Added `scanned_directories` table — ROM scan directories are now persisted across sessions; `add_scanned_directory`, `get_all_scanned_directories`, and `remove_scanned_directory` CRUD operations added
- **Service**: Exposed `add_scan_directory`, `get_scan_directories`, and `remove_scan_directory` methods on `RomulusService`; scan directories are registered in the DB before scanning so they survive restarts
- **GUI**: Fixed status label display — replaced non-ASCII Unicode symbols (✓ / ✗) that rendered as `?` with ImGui's default ProggyClean font with ASCII equivalents: `[OK]`, `[--]`, `[??]`, `[!!]`
- **GUI**: Foundation overhaul — applied a custom polished dark-blue theme replacing the stock ImGui dark theme; all colours, rounding values, padding, and spacing are tuned for a cohesive professional look
- **GUI**: ROM checklist now shows a full status breakdown in the summary header — individual counts and colour-coded badges for Verified, Missing, Unverified, and Mismatch entries alongside the completion percentage
- **GUI**: Added filter bar to the ROM checklist — free-text substring search (case-insensitive) and a status dropdown (All / Verified / Missing / Unverified / Mismatch) to narrow down large ROM lists

### 🔧 Refactoring

- **HashService / HashDigest**: `HashDigest` now stores raw bytes (`std::array<std::byte, N>`) instead of hex strings — 4 bytes for CRC32, 16 for MD5, 20 for SHA-1, 32 for SHA-256. Binary storage enables direct equality comparison with no string parsing, and is more compact for future DB optimisation. Display-ready lowercase hex strings are produced on demand via `to_hex_crc32()`, `to_hex_md5()`, `to_hex_sha1()`, and `to_hex_sha256()` accessor methods. All call-sites (scanner, logging) updated accordingly. A new `KnownContentProducesExpectedHexDigests` unit test pins exact hash values against externally-computed reference vectors.

- **HashService / HashDigest**: `HashContext::finalize()` now returns `Result<core::HashDigest>` and properly checks the return value of `EVP_DigestFinal_ex`, returning a `HashComputeError` on failure. Digest lengths reported by OpenSSL are validated against expected sizes (MD5=16, SHA-1=20, SHA-256=32); a mismatch also produces an error instead of silently copying wrong-length data. The duplicate `to_hex_sha256()` call in `rom_scanner.cpp` is eliminated by caching the result in a local variable.

- **HashService**: Introduced `compute_hashes_stream(StreamReader)` as the single core hashing primitive. Both `compute_hashes` (disk file) and `compute_hashes_archive` (archive entry) are now thin wrappers that build a `StreamReader` lambda and delegate to it. The new `DataChunkCallback` and `StreamReader` type aliases are exposed in `hash_service.hpp`, allowing callers to hash any byte-stream source without adding new `HashService` methods. Zero performance impact: the hot path (per-byte hash computation) is unchanged; the added `std::function` call is a one-time setup cost, negligible against I/O and hash work.

- **ArchiveService**: `ArchiveEntry` now carries a `std::size_t index` field — the zero-based sequential position of the entry in the archive. `stream_entry` and `compute_hashes_archive` now accept this stable numeric index instead of an `entry_name` string. Names are kept on `ArchiveEntry` for display purposes only. This eliminates subtle bugs from duplicate entry names, case-sensitivity differences, and encoding ambiguities.

### 🐛 Bug Fixes

- **Scanner**: Fixed `ArchiveService::is_archive()` incorrectly classifying ZIP (and other archive) files whose names contain dots inside parentheses (e.g. version strings like `(v1.1)`) as non-archives. The naive stem-extension check was building a bogus double-extension such as `.1).zip`, which was not recognised, causing the ZIP container itself to be stored in the database and displayed in the "Local ROMs" list instead of the ROM it contains.

### ⚡ Features

- **GUI**: Added "Log" tab — displays all ROMULUS log messages captured since the application started, colour-coded by severity (amber = warning, red = error/critical, grey = debug/trace). A **Clear** button removes all visible entries. The log auto-scrolls to the newest entry.


- **GUI**: Added tabs to the main window — "Local ROMs" (scanned ROM files, renamed from "Scanned Files") and "Systems" (lists all systems imported from DAT files); each tab re-fetches its data fresh from the database on activation, and systems are also automatically refreshed after every DAT import and database purge
- **GUI**: Added modular ImGui + GLFW desktop GUI (`apps/gui/`) with DAT import, folder scanning, verification, database purge, collection summary display, and scrollable file table
- **GUI**: Added `--no-gui` launch parameter to skip GUI initialization for headless operation
- **GUI**: Added `ROMULUS_ENABLE_GUI` CMake option (default ON) to opt out of building the GUI
- **GUI**: Background threading for scan, import, verify, and purge operations — UI stays responsive during long-running tasks
- **GUI**: Animated indeterminate progress bar while background operations run
- **GUI**: Right-click any hash cell to copy to clipboard; toast notification confirms the copy
- **GUI**: Native file/folder picker dialogs via Browse buttons (Windows Shell API on Windows, zenity/kdialog on Linux)
- **GUI**: Human-readable file sizes in the table (KB, MB, GB)
- **GUI**: File table now shows Filename, Size, CRC32, MD5, SHA1, and SHA256 columns (removed Path column)
- **GUI**: Sortable columns in the file table — click any column header to sort ascending/descending; sort order is preserved after data refresh
- **Service**: Added `get_all_files()` and `purge_database()` methods to `RomulusService` for GUI data access and admin operations
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
- Fixed CI: added `libx11-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, `libxi-dev`, and `libgl-dev` installation step for Ubuntu runners so that vcpkg can build `glfw3` (required by the GUI); both GCC and Clang Linux jobs and the clang-tidy job now install these system headers before CMake configuration

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
