# CHANGELOG

> *ROMULUS imposes order on chaos — and keeps a record of it.*

All notable changes to this project will be documented in this file.
This changelog is automatically generated from [Conventional Commits](https://www.conventionalcommits.org/).

## [Unreleased]

### 📖 Docs

- **README rewrite**: Replaced ASCII art box borders in the workflow section with clean numbered steps and markdown formatting. Adopted a retro 1982-game-manual style with quirky section headers, tilde dividers, and a Gandalf-ism. All technical content preserved.
- **Security (XXE false positive)**: Added inline comments to `dat_parser.cpp` (`load_buffer` and `load_file` call sites) documenting that PugiXML does not implement DTD processing or external entity resolution, making XXE scanner warnings a false positive. `pugi::parse_default` is retained to preserve standard XML escape-sequence decoding (e.g. `&amp;`, `&quot;`, numeric references).

### 🗄️ Database — Schema v4: archive modeling, system context, Unix timestamps, drop redundant columns

- **Schema** (version → 4): `k_SchemaVersion` bumped to 4 — existing databases are auto-rebuilt on open.
- **`files` table — archive modeling (#4)**: replaced the single `is_archive_entry INTEGER` flag with two explicit columns: `archive_path TEXT` (nullable — `NULL` for bare files, physical archive path for archive entries) and `entry_name TEXT` (in-archive entry name, `NULL` for bare files). For bare files the physical path is already stored in `path`, so `archive_path` is omitted entirely. `FileInfo::archive_path` changed from `std::string` to `std::optional<std::string>`.
- **`files` table — drop `filename` (#3)**: removed the redundant `filename TEXT NOT NULL` column. The display filename is always derivable: `std::filesystem::path(entry_name).filename()` for archive entries, or `std::filesystem::path(path).filename()` for bare files — stored nowhere, derived on demand.
- **GUI DB tab — human-readable timestamps**: `query_table_data()` now wraps known Unix epoch INTEGER columns (`last_scanned`) in `datetime(..., 'unixepoch', 'localtime')` in the SELECT statement, so the DB browser renders them as `"YYYY-MM-DD HH:MM:SS"` local time — matching the TEXT timestamp columns (`imported_at`, `added_at`). Storage stays as compact epoch integers.
- **`dat_versions` table — system context (#5)**: added `system TEXT` column populated from the DAT `<description>` header field. Gives a human-readable system description separate from the short `name` identifier, and provides a hook for future system-level metadata.
- **`core::FileInfo`**: removed `filename` and `bool is_archive_entry` fields; added `std::optional<std::string> archive_path` (NULL for bare files), `std::optional<std::string> entry_name`, and `[[nodiscard]] is_archive_entry() const` method (derived from `entry_name.has_value()`). `last_scanned` changed to `std::int64_t`.
- **`core::DatVersion`**: added `std::string system` field populated via `DatHeader::description` on import.
- **Service**: `import_dat()` sets `dat_version.system = header.description`; scan loop builds `FileInfo` with `archive_path` + `entry_name` from `ScannedROM`.
- **All CRUD queries** (`upsert_file`, `find_file_by_path`, `get_all_files`, `get_unverified_files`, `insert_dat_version`, `find_dat_version*`, `get_all_dat_versions`) updated to the new column layouts.

### 🗄️ Database — Normalize `game_name` into a proper `games` table

- **Schema** (version → 4): Re-introduced a first-class `games` table (`id`, `dat_version_id` FK, `name`, `UNIQUE(dat_version_id, name)`). `roms.game_name TEXT` (denormalized copy) has been replaced by `roms.game_id INTEGER NOT NULL REFERENCES games(id)`. Each unique game name is now stored exactly once per DAT version, eliminating duplication and enabling future metadata (year, publisher, etc.) to be attached to game entries.
- **Schema**: Added index `idx_games_dat_version ON games(dat_version_id)` and `idx_roms_game ON roms(game_id)` for fast per-DAT lookups.
- **Schema versioning**: `k_SchemaVersion` bumped to 4 — existing databases are automatically rebuilt on open.
- **Database API**: Added `Database::find_or_insert_game(dat_version_id, name) → Result<int64_t>` — idempotent upsert that returns the game id for the given `(dat_version_id, name)` pair.
- **Database API**: Added `Database::get_games_for_dat_version(dat_version_id) → Result<vector<GameEntry>>` — returns all game entries for a DAT version, sorted by name.
- **All ROM read queries** (`get_roms_for_dat_version`, `get_all_roms`, `find_rom_by_sha1`, `find_rom_by_sha256`, `find_rom_by_md5`, `find_rom_by_crc32`, `get_missing_roms`, `get_duplicate_files`, `get_collection_summary`) updated to JOIN `games` so `game_name` and `dat_version_id` are still available on `RomInfo` as convenience fields.
- **types.hpp**: Added `core::GameEntry` struct (`id`, `dat_version_id`, `name`). `RomInfo::game_name` and `RomInfo::dat_version_id` are now **display-only / JOIN-populated fields** (moved to end of struct to avoid designated-initialiser warnings); `RomInfo::game_id` is the new storage FK.
- **Service**: `RomulusService::import_dat()` now calls `find_or_insert_game` per game entry before inserting its ROMs with the returned `game_id`.

### 🗄️ Database — Schema & API Fixes (PR review)

- **Schema**: `files.path` now declared `TEXT NOT NULL COLLATE NOCASE` at column level; `UNIQUE(path)` inherits the collation — fixes a SQLite conflict-target mismatch that could prevent `ON CONFLICT(path) DO UPDATE` from triggering on case-differing paths.
- **Schema**: Added `files.is_archive_entry INTEGER NOT NULL DEFAULT 0` — persists whether a file record originates from inside an archive; `FileInfo::is_archive_entry` now correctly round-trips through the database.
- **Schema versioning**: `run_migrations()` now checks `PRAGMA user_version` against `k_SchemaVersion=2`. If an existing DB has a different (non-zero) version all application tables are dropped and recreated, preventing stale column layouts from causing runtime errors after schema changes.
- **Database API**: `find_dat_version(name, version)` now appends `ORDER BY imported_at DESC, id DESC LIMIT 1` — result is deterministic even if `(name, version)` is not unique (only `checksum` is unique).
- **Reports**: Fixed user-visible labels — `"System"` / `"system"` renamed to `"DAT"` / `"dat"` in text, CSV, and JSON summary and missing-ROM reports to match the new DAT-centric model.

### 🗄️ Database — Massive Schema Refactor

- **Schema**: Dropped `systems` and `games` tables entirely — game info is now denormalized into `roms.game_name` (TEXT). System concepts were premature for the current core workflow (file ↔ DAT verification).
- **Schema**: Dropped `rom_status` table — ROM status (Verified / Missing / Unverified / Mismatch) is now computed dynamically from `rom_matches` + `files` via a CTE SQL query. No stale cache, no desync.
- **Schema**: All hash columns are now **BLOB** uniformly: `roms.expected_sha1`, `roms.crc32`, `roms.md5`, `roms.sha256`; `files.sha1`, `files.crc32`, `files.md5`; `global_roms.*`. Eliminates the previous mixed TEXT/BLOB chaos.
- **Schema**: `roms.sha1` renamed to `roms.expected_sha1` (BLOB) to clearly distinguish the DAT-declared expected hash from `global_roms.sha1` (actual file identity).
- **Schema**: `dat_versions` now uses `UNIQUE(checksum)` instead of `UNIQUE(name, version)` — prevents importing the same file twice even when re-packaged, and avoids cross-source name clashes.
- **Schema**: `dat_versions` no longer has a `system_id` column — DATs are self-contained, not bound to a system record.
- **Schema**: `files.sha1 BLOB NOT NULL` (was nullable TEXT); `files.sha256` is nullable BLOB (was NOT NULL TEXT).
- **Schema**: `rom_matches.match_type` changed from `TEXT` to `INTEGER` enum (`0=Exact`, `1=Sha256Only`, `2=Sha1Only`, `3=Md5Only`, `4=Crc32Only`, `5=SizeOnly`, `6=NoMatch`).
- **Schema**: Added missing hot-path index `CREATE INDEX idx_rom_matches_sha1 ON rom_matches(global_rom_sha1)`.
- **Database API**: Removed `insert_system`, `find_system_by_name`, `get_all_systems`, `get_or_create_system`, `insert_game`, `get_games_by_dat_version`, `upsert_rom_status`, `get_rom_status`, `get_all_roms_for_system`, `get_latest_dat_version`.
- **Database API**: Added `get_all_roms()` — returns all ROM entries across all DATs.
- **Database API**: Added `get_computed_rom_status(rom_id)` — computes ROM status dynamically from `rom_matches` + `files`.
- **Database API**: `get_collection_summary`, `get_missing_roms`, `get_duplicate_files` now filter by `dat_version_id` (not `system_id`).
- **Database API**: `get_unverified_files()` — signature simplified (no `system_id` parameter).
- **Engine**: `Matcher::match_all()` now iterates `get_all_roms()` directly (no outer system loop).
- **Engine**: `Classifier::classify_all()` now computes status dynamically instead of calling `upsert_rom_status`.
- **Service**: `import_dat()` no longer creates `systems` or `games` rows; game info is folded into `roms.game_name`.
- **Service**: `list_systems()` removed; replaced by `list_dat_versions()`.
- **Service**: `verify()`, `get_summary()`, `get_missing_roms()` now accept `dat_name` filter (not `system` name).
- **Service**: `purge_database()` only clears `rom_matches`, `files`, `global_roms`, `roms`, `dat_versions` (no more `systems`/`games`/`rom_status`).
- **CLI**: `systems` subcommand replaced by `dats` subcommand (lists imported DAT versions).
- **CLI**: `--system` option on `verify`, `report`, `status` subcommands replaced by `--dat` option.
- **DAT Fetcher**: `has_version_changed()` now checks by checksum (not by `get_latest_dat_version(system_id)`).
- **Types**: `GameInfo.description` removed (not stored); `RomInfo` now has `dat_version_id` + `game_name` instead of `game_id`.
- **Reports**: All report generators updated to use `dat_name` instead of `system_name`.

- **GUI**: Added **DB tab** — a read-only database explorer. Database path shown in a disabled text field (with a placeholder Browse button for future use). "Read DB" button loads all table names, a tables dropdown selects a table, a collapsible Schema panel shows column metadata (type + `[PK]`/`[NN]`/`[UQ]`/`[FK]→table.column` badges), and a scrollable read-only data grid shows all rows. Right-click any cell to copy its value to the clipboard.
- **Database**: Added `get_table_names()` — returns all user-defined table names from `sqlite_master` (excludes internal `sqlite_*` tables).
- **Database**: Added `query_table_data(table_name)` — returns full column metadata (PK/NN/UQ/FK flags via `PRAGMA table_info`, `PRAGMA index_list`, `PRAGMA index_info`, `PRAGMA foreign_key_list`) and all rows with BLOB→hex conversion; no row limit.
- **Database**: Added `PreparedStatement::column_display_text()` — BLOB columns rendered as lowercase hex, NULL rendered as "(NULL)", other types via `sqlite3_column_text`.
- **Service**: Added `get_db_path()`, `get_db_table_names()`, and `query_db_table()` on `RomulusService` to expose the DB explorer API to the GUI layer.
- **Core**: Added `ColumnInfo` struct and updated `TableQueryResult` to use `std::vector<ColumnInfo>` for rich column metadata.

- **Scanner**: Introduced `ScannedROM` struct in `core/types.hpp` — a first-class abstraction for a ROM discovered during scanning. `ScannedROM` carries `archive_path` (physical file or containing archive), `entry_name` (optional; set for archive entries), `size`, and a `HashDigest`. Helper methods `is_archive_entry()`, `filename()`, and `virtual_path()` encode the archive multiplicity directly in the type. `ScanResult::files` now holds `std::vector<ScannedROM>` instead of `std::vector<FileInfo>`, making it explicit that one archive file can produce many ROM records. The service layer converts `ScannedROM` → `FileInfo` before DB persistence; the `FileInfo` struct and all database APIs are unchanged.

- **GUI**: Restructured main window into three tabs — **DATs** (ROM checklist with inline DAT controls), **Folders** (ROM directory management), **Log** (application log)
- **GUI**: DATs tab ROM table now shows SHA1 instead of CRC32 as the hash column
- **GUI**: Folders tab — lists all registered ROM scan directories loaded from the database; supports adding new folders and removing existing ones with an `[X]` button
- **Database**: Added `scanned_directories` table — ROM scan directories are now persisted across sessions; `add_scanned_directory`, `get_all_scanned_directories`, and `remove_scanned_directory` CRUD operations added
- **Service**: Exposed `add_scan_directory`, `get_scan_directories`, and `remove_scan_directory` methods on `RomulusService`; scan directories are registered in the DB before scanning so they survive restarts
- **GUI**: Fixed status label display — replaced non-ASCII Unicode symbols (✓ / ✗) that rendered as `?` with ImGui's default ProggyClean font with ASCII equivalents: `[OK]`, `[--]`, `[??]`, `[!!]`
- **GUI**: Foundation overhaul — applied a custom polished dark-blue theme replacing the stock ImGui dark theme; all colours, rounding values, padding, and spacing are tuned for a cohesive professional look
- **GUI**: ROM checklist now shows a full status breakdown in the summary header — individual counts and colour-coded badges for Verified, Missing, Unverified, and Mismatch entries alongside the completion percentage
- **GUI**: Added filter bar to the ROM checklist — free-text substring search (case-insensitive) and a status dropdown (All / Verified / Missing / Unverified / Mismatch) to narrow down large ROM lists
- **GUI**: DAT selector dropdown is now sorted alphabetically by name; the selected DAT is tracked by ID and restored correctly after each refresh
- **GUI**: ROM table default sort is now **ROM Name ascending** (was Status); the ImGui sort arrow reflects this from the first render
- **GUI**: Removed the completion progress bar from the DATs tab — the text summary already conveys the same information
- **GUI**: Right-click on any ROM Name, Size, or SHA1 cell in the DATs table copies the raw value to the clipboard; right-click on a folder path in the Folders tab copies the path. A "Right-click to copy" tooltip hints users. A toast confirms each copy.
- **GUI**: Fixed toast notification rendering — reimplemented using `ImGui::GetForegroundDrawList()` instead of a separate ImGui window. The old approach relied on window Z-ordering which ImGui breaks by always drawing the focused window last, making the toast invisible after the first appearance or after interacting with the table (e.g. sorting). The foreground draw list is always rendered on top of all windows, bypassing the Z-order system entirely.
- **GUI**: Active DAT now shown in a full-width highlighted banner below the control toolbar instead of a truncated inline label. The banner (dark-blue background, rounded corners) displays: `Active DAT | <name> <version>  imported <date>` — full name is never clipped and the selected DAT is always easy to identify at a glance. The combo dropdown is widened to fill the available toolbar space now that the inline label is removed.
- **GUI**: Toast text is now centered both horizontally and vertically within the notification box.
- **GUI**: Active DAT banner child window no longer shows a spurious scrollbar — added `ImGuiWindowFlags_NoScrollbar | NoScrollWithMouse`.
- **GUI**: Added `[^] Top` and `[v] Bot` navigation buttons to the DATs tab filter bar — jump to the first or last row of the ROM table in one click while preserving the current sort order.
- **Database**: `imported_at` timestamps are now stored in **local time** (`datetime('now', 'localtime')`) rather than UTC, so the DAT selector shows the correct time for the user's timezone

### ⚡ Performance

- **GUI**: Replaced `glfwPollEvents()` busy-loop with `glfwWaitEventsTimeout(1/60)` — the app now sleeps when idle instead of spinning at thousands of FPS, dropping CPU usage to near zero when no user input arrives
- **GUI**: Disabled `ImGuiConfigFlags_ViewportsEnable` explicitly — multi-viewport is unused and was adding unnecessary overhead
- **GUI**: Checklist status counters (Verified / Missing / Unverified / Mismatch) are now precomputed once when the checklist loads (`check_pending_task`) and stored as `ChecklistStats`; removed the O(n) per-frame iteration over `rom_checklist_`

### 🔧 Refactoring

- **Scanner**: Refactored extension filter type from `std::optional<std::string>` (comma-separated) to `std::optional<std::vector<std::string>>` in `RomScanner::scan()` and `RomulusService::scan_directory()`. The `split_extensions()` helper has been removed from the scanner; callers now parse and normalise the extension list once before passing it in. The CLI (`--extensions`) uses CLI11's built-in `delimiter(',')` support and a one-time `normalize_extensions()` pass to produce a ready-to-use vector. This eliminates redundant string parsing on every scan invocation.
- **Scanner**: Decoupled `RomScanner` from the database layer. `RomScanner::scan()` no longer takes a `database::Database&` parameter. Instead it accepts an optional `std::function<bool(std::string_view)> skip_check` predicate and returns `Result<core::ScanResult>` — a new type that bundles the `ScanReport` statistics with the `vector<FileInfo>` of newly discovered files. The service layer (`RomulusService::scan_directory`) now owns the full pipeline: pre-loading known paths from the database via the new `Database::get_all_file_paths()` (path-only query, no wasted blob→hex decoding), supplying the skip predicate to the scanner, and persisting the returned files in a single explicitly checked transaction (BEGIN/COMMIT/ROLLBACK all propagate `Result<void>`). The results vector is pre-reserved to `jobs.size()` before parallel hashing to avoid reallocations under the mutex. This cleanly separates *discovery* (scanner) from *persistence* (service + database), making the scanner independently testable without a database.

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
