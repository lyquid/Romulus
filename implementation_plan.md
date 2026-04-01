# ROMULUS — Implementation Plan

> *ROMULUS doesn't "manage ROMs." ROMULUS imposes order on chaos.*

## Overview

ROMULUS is a C++23 backend system that tracks, verifies, and reports the state of ROM collections using No-Intro DAT files as the authoritative dataset. The system follows a pipeline architecture with SQLite as the single source of truth.

---

## Prerequisites & Toolchain

> [!IMPORTANT]
> The following tools must be installed and available on PATH before building:
> - **CMake** ≥ 3.25
> - **vcpkg** (with `VCPKG_ROOT` environment variable set)
> - **A C++23 compiler**: MSVC (Visual Studio 2022 17.x), GCC ≥ 13, or Clang ≥ 17
> - **clang-format** and **clang-tidy** (for formatting/linting)

---

## Project Directory Structure

The project is split into **library** (all core logic) and **apps** (frontends that consume the library). This ensures a future web interface can link against `romulus_lib` without touching internals.

```
c:\repos\romulus\
├── CMakeLists.txt                    # Root CMake — project definition + subdirs
├── CMakePresets.json                 # CMake presets for dev/release/CI
├── vcpkg.json                       # vcpkg manifest (all dependencies)
├── vcpkg-configuration.json          # vcpkg registry baseline
├── .clang-format                    # Code formatting rules
├── .clang-tidy                      # Static analysis rules
├── .gitignore
├── README.md                        # Retro gaming themed README
├── CHANGELOG.md                     # Keep a Changelog format
├── LICENSE
│
├── cmake/                           # CMake helper modules
│   ├── CompilerWarnings.cmake        # Warning flags per compiler
│   └── StaticAnalysis.cmake          # clang-tidy integration
│
├── lib/                             # ══ CORE LIBRARY (romulus_lib) ══
│   └── romulus/
│       ├── CMakeLists.txt            # romulus_lib STATIC library target
│       │
│       ├── core/                     # Shared types, error handling, logging
│       │   ├── error.hpp             # Error types using std::expected
│       │   ├── types.hpp             # Shared type definitions
│       │   └── logging.hpp           # spdlog wrapper
│       │
│       ├── database/                 # SQLite database layer
│       │   ├── database.hpp / .cpp   # Connection, migrations, transactions
│       │   └── schema.sql            # Embedded SQL schema
│       │
│       ├── dat/                      # DAT fetching & parsing
│       │   ├── dat_fetcher.hpp / .cpp
│       │   └── dat_parser.hpp / .cpp
│       │
│       ├── scanner/                  # Filesystem scanning & hashing
│       │   ├── rom_scanner.hpp / .cpp
│       │   └── hash_service.hpp / .cpp
│       │
│       ├── engine/                   # Matching & classification
│       │   ├── matcher.hpp / .cpp
│       │   └── classifier.hpp / .cpp
│       │
│       ├── report/                   # Report generation
│       │   └── report_generator.hpp / .cpp
│       │
│       └── service/                  # ★ HIGH-LEVEL SERVICE API
│           └── romulus_service.hpp / .cpp  # Facade for CLI / Web / API consumers
│
├── apps/                            # ══ C++ APPLICATIONS ══
│   ├── cli/                          # CLI executable (v0.1)
│   │   ├── CMakeLists.txt            # romulus_cli target
│   │   └── main.cpp                  # CLI11-based entry point
│   │
│   └── api/                          # REST API server (future)
│       ├── CMakeLists.txt            # romulus_api target (placeholder)
│       ├── main.cpp                  # HTTP server entry point
│       └── routes.hpp / .cpp         # Route handlers → RomulusService calls
│
├── web/                             # ══ WEB FRONTEND (future, React/TypeScript) ══
│   ├── package.json                  # npm project (Vite + React + TypeScript)
│   ├── tsconfig.json
│   ├── vite.config.ts
│   ├── src/
│   │   ├── App.tsx
│   │   ├── main.tsx
│   │   └── ...                       # React components, hooks, API client
│   └── README.md                     # Web frontend setup instructions
│
└── tests/
    ├── CMakeLists.txt                # Test configuration
    ├── unit/                         # Unit tests per module
    │   ├── test_dat_parser.cpp
    │   ├── test_hash_service.cpp
    │   ├── test_matcher.cpp
    │   ├── test_classifier.cpp
    │   ├── test_database.cpp
    │   └── test_report_generator.cpp
    ├── integration/                  # Integration tests
    │   ├── test_full_scan.cpp
    │   └── test_dat_update.cpp
    └── fixtures/                     # Test data files
        ├── sample.dat                # Minimal No-Intro DAT for testing
        └── roms/                     # Dummy ROM files for scan tests
```

---

## Dependencies (vcpkg manifest)

| Library | Purpose | vcpkg Port | Version |
|---------|---------|-----------|---------|
| **SQLite3** | Embedded database | `sqlite3` | ≥ 3.45.0 |
| **spdlog** | Structured logging | `spdlog` | ≥ 1.14.0 |
| **pugixml** | XML parsing (DAT files) | `pugixml` | ≥ 1.14 |
| **CLI11** | Command-line parsing | `cli11` | ≥ 2.4.0 |
| **GTest** | Unit testing framework | `gtest` | ≥ 1.14.0 |
| **libarchive** | Archive reading (zip/7z/tar) | `libarchive` | ≥ 3.7.0 |
| **OpenSSL** | SHA1/MD5 hashing | `openssl` | ≥ 3.0 |

> [!NOTE]
> **pugixml** is chosen over TinyXML2 for its DOM-based API which maps naturally to the LogiqX DAT format's structure. **libarchive** handles zip, 7z, tar, and more — used to read ROM files inside archives without extracting to disk.

---

## User Review Required

> [!IMPORTANT]
> ### Design Decisions Requiring Approval
>
> 1. **XML Parser Choice**: Using **pugixml** (fast, DOM-style) over TinyXML2 (SAX-style). Pugixml's DOM approach is more natural for DAT files where we need random access to game/rom nodes. Agree?
>
> 2. **Hashing Library**: Using **OpenSSL** for CRC32/MD5/SHA1. Alternative: a lighter library like `picosha2` + custom CRC32. OpenSSL is heavier but battle-tested and already available in vcpkg. Agree?
>
> 3. **HTTP Client**: Using **cpp-httplib** (header-only, simple). Alternative: libcurl (more features, heavier). For downloading DAT files the simpler option should suffice. Agree?
>
> 4. **No ORM**: Using raw SQLite3 C API with prepared statements (wrapped in RAII classes) rather than sqlite-orm. This gives full control over queries and avoids ORM abstraction leaks. Agree?
>
> 5. **CLI Framework**: Using **CLI11** for the command-line interface. It's header-only, modern C++, and integrates cleanly. Agree?
>
> 6. **Initial Platform**: Building and testing primarily on **Windows/MSVC** first, with cross-platform compatibility as a goal. Agree?

---

## Proposed Changes

### Phase 1: Project Skeleton & Build System

#### [NEW] [CMakeLists.txt](file:///c:/repos/romulus/CMakeLists.txt)
Root CMake configuration:
- `cmake_minimum_required(VERSION 3.25)`
- `project(romulus VERSION 0.1.0 LANGUAGES CXX)`
- Set `CMAKE_CXX_STANDARD 23` and `CMAKE_CXX_STANDARD_REQUIRED ON`
- Include compiler warnings module
- `add_subdirectory(lib)`, `add_subdirectory(apps)`, `add_subdirectory(tests)`
- `enable_testing()` support

#### [NEW] [CMakePresets.json](file:///c:/repos/romulus/CMakePresets.json)
Presets for development:
- `dev` preset: Debug build, vcpkg toolchain, warnings enabled
- `release` preset: Release build, optimizations
- Both use out-of-source `build/` directory

#### [NEW] [vcpkg.json](file:///c:/repos/romulus/vcpkg.json)
Manifest with all dependencies and version constraints.

#### [NEW] [vcpkg-configuration.json](file:///c:/repos/romulus/vcpkg-configuration.json)
Registry baseline pinning.

#### [NEW] [cmake/CompilerWarnings.cmake](file:///c:/repos/romulus/cmake/CompilerWarnings.cmake)
Function `romulus_set_warnings(target)`:
- GCC/Clang: `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wnull-dereference -Wdouble-promotion`
- MSVC: `/W4 /WX /permissive-`

#### [NEW] [cmake/StaticAnalysis.cmake](file:///c:/repos/romulus/cmake/StaticAnalysis.cmake)
clang-tidy integration (optional, enabled by CMake option).

#### [NEW] [.clang-format](file:///c:/repos/romulus/.clang-format)
Based on LLVM style, customized for the project.

#### [NEW] [.clang-tidy](file:///c:/repos/romulus/.clang-tidy)
Checks: modernize-*, performance-*, readability-*, bugprone-*.

#### [NEW] [.gitignore](file:///c:/repos/romulus/.gitignore)
Ignoring `build/`, `out/`, IDE files, vcpkg installed artifacts.

#### [NEW] [.github/copilot-instructions.md](file:///c:/repos/romulus/.github/copilot-instructions.md) ✅ Created
C++ Core Guidelines-based coding conventions for GitHub Copilot.

#### [NEW] [.github/instructions/cpp.instructions.md](file:///c:/repos/romulus/.github/instructions/cpp.instructions.md) ✅ Created
Path-specific Copilot rules for `*.hpp`/`*.cpp` files.

#### [NEW] [.github/instructions/cmake.instructions.md](file:///c:/repos/romulus/.github/instructions/cmake.instructions.md) ✅ Created
Path-specific Copilot rules for `CMakeLists.txt` files.

#### [NEW] [.github/workflows/ci.yml](file:///c:/repos/romulus/.github/workflows/ci.yml) ✅ Created
CI pipeline (triggers on push to `main`/`develop` and PRs):
- **Build matrix**: MSVC (Windows), GCC 13 (Linux), Clang 17 (Linux)
- **clang-format check**: Ensures code formatting compliance
- **clang-tidy**: Static analysis with zero-warning enforcement
- **Test run**: `ctest` with `--output-on-failure`
- **vcpkg caching**: Speeds up dependency installs across runs

#### [NEW] [.github/workflows/release.yml](file:///c:/repos/romulus/.github/workflows/release.yml) ✅ Created
Release pipeline (triggers on push to `main`):
- **Semantic versioning**: Parses conventional commits → bumps version automatically
  - `feat(...)` → **minor** bump (0.1.0 → 0.2.0)
  - `fix(...)` → **patch** bump (0.1.0 → 0.1.1)
  - `BREAKING CHANGE` → **major** bump (0.1.0 → 1.0.0)
- **Auto-generates**: Git tags, GitHub Releases, CHANGELOG.md
- **Builds release artifacts**: Windows x64 + Linux x64 binaries
- **Uploads to GitHub Release**: Downloadable executables

#### [NEW] [.releaserc.json](file:///c:/repos/romulus/.releaserc.json) ✅ Created
Semantic-release configuration: commit type → version mapping, changelog sections.

---

### Phase 2: Core Module — Types, Errors & Logging

#### [NEW] [src/romulus/core/error.hpp](file:///c:/repos/romulus/src/romulus/core/error.hpp)
```cpp
// Error categories and result types using std::expected
enum class ErrorCode { ... };
struct Error { ErrorCode code; std::string message; };
template<typename T> using Result = std::expected<T, Error>;
```

#### [NEW] [lib/romulus/core/types.hpp](file:///c:/repos/romulus/lib/romulus/core/types.hpp)
Shared data structures:
- `HashDigest` — CRC32, MD5, SHA1 triplet
- `SystemInfo` — System metadata (name, short name, extensions)
- `RomInfo` — ROM metadata from DAT
- `GameInfo` — Game with its ROMs
- `DatVersion` — DAT file version info
- `FileInfo` — Scanned file metadata
- `MatchResult` — Match between file and ROM
- `RomStatus` — Classification status enum

#### [NEW] [lib/romulus/core/logging.hpp](file:///c:/repos/romulus/lib/romulus/core/logging.hpp)
spdlog-based structured logging facade. INFO/WARN/ERROR levels.

---

### Phase 2b: Service API Layer

#### [NEW] [lib/romulus/service/romulus_service.hpp](file:///c:/repos/romulus/lib/romulus/service/romulus_service.hpp)
#### [NEW] [lib/romulus/service/romulus_service.cpp](file:///c:/repos/romulus/lib/romulus/service/romulus_service.cpp)

High-level facade that orchestrates all modules. Both the CLI and the future web interface consume **only this API** — they never call modules directly.

```cpp
class RomulusService {
public:
    explicit RomulusService(const std::filesystem::path& db_path);

    // DAT operations
    Result<DatVersion> import_dat(const std::filesystem::path& path);
    Result<DatVersion> fetch_and_import_dat(const std::string& url);
    Result<bool>       check_dat_update(const std::string& url);

    // Scan operations
    Result<ScanReport>    scan_directory(const std::filesystem::path& dir,
                                        std::optional<std::string> system = {});
    Result<void>          verify(std::optional<std::string> system = {});
    Result<void>          full_sync(const std::string& dat_url,
                                   const std::filesystem::path& rom_dir);

    // Queries
    Result<CollectionSummary>          get_summary(std::optional<std::string> system = {});
    Result<std::vector<SystemInfo>>    list_systems();
    Result<std::vector<MissingRom>>    get_missing_roms(std::optional<std::string> system = {});
    Result<std::vector<DuplicateFile>> get_duplicates(std::optional<std::string> system = {});

    // Reports
    Result<std::string> generate_report(ReportType type, ReportFormat format,
                                        std::optional<std::string> system = {});
};
```

> [!NOTE]
> This service layer is the **contract boundary**. The future web app (e.g. REST API via crow/drogon) will wrap exactly these methods as HTTP endpoints. No core refactoring required — just add `apps/web/` and call `RomulusService`.

---

### Phase 3: Database Module

#### [NEW] [src/romulus/database/database.hpp](file:///c:/repos/romulus/src/romulus/database/database.hpp)
#### [NEW] [src/romulus/database/database.cpp](file:///c:/repos/romulus/src/romulus/database/database.cpp)

RAII database class wrapping SQLite3:
- `Database(path)` — opens/creates DB, runs migrations
- Transaction support (`begin()`, `commit()`, `rollback()`, RAII guard)
- Prepared statement caching
- CRUD operations for all 7 tables:

**Schema (7 tables):**

```sql
CREATE TABLE IF NOT EXISTS systems (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,  -- e.g. "Nintendo - Game Boy"
    short_name  TEXT,                  -- e.g. "GB"
    extensions  TEXT                   -- e.g. ".gb,.gbc" (default scan extensions for this system)
);

CREATE TABLE IF NOT EXISTS dat_versions (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    system_id     INTEGER NOT NULL REFERENCES systems(id),
    name          TEXT NOT NULL,
    version       TEXT NOT NULL,
    source_url    TEXT,
    checksum      TEXT NOT NULL,
    imported_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(name, version)
);

CREATE TABLE IF NOT EXISTS games (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT NOT NULL,
    system_id       INTEGER NOT NULL REFERENCES systems(id),
    dat_version_id  INTEGER NOT NULL REFERENCES dat_versions(id),
    UNIQUE(name, system_id, dat_version_id)
);

CREATE TABLE IF NOT EXISTS roms (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    game_id  INTEGER NOT NULL REFERENCES games(id),
    name     TEXT NOT NULL,
    size     INTEGER,
    crc32    TEXT,
    md5      TEXT,
    sha1     TEXT,
    region   TEXT
);

CREATE TABLE IF NOT EXISTS files (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    path          TEXT NOT NULL UNIQUE,
    size          INTEGER NOT NULL,
    crc32         TEXT,
    md5           TEXT,
    sha1          TEXT,
    last_scanned  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS file_matches (
    file_id     INTEGER NOT NULL REFERENCES files(id),
    rom_id      INTEGER NOT NULL REFERENCES roms(id),
    match_type  TEXT NOT NULL,  -- 'exact', 'crc_only', 'sha1_only', etc.
    PRIMARY KEY (file_id, rom_id)
);

CREATE TABLE IF NOT EXISTS rom_status (
    rom_id        INTEGER PRIMARY KEY REFERENCES roms(id),
    status        TEXT NOT NULL,  -- 'verified', 'missing', 'unverified', 'mismatch'
    last_updated  TEXT NOT NULL DEFAULT (datetime('now'))
);
```

Key implementation details:
- WAL mode for concurrent reads
- Foreign keys enforced
- Batch insert using transactions (100+ rows per transaction)
- Idempotent migrations (CREATE IF NOT EXISTS)

---

### Phase 4: DAT Import & Parsing

#### [NEW] [lib/romulus/dat/dat_fetcher.hpp](file:///c:/repos/romulus/lib/romulus/dat/dat_fetcher.hpp)
#### [NEW] [lib/romulus/dat/dat_fetcher.cpp](file:///c:/repos/romulus/lib/romulus/dat/dat_fetcher.cpp)

- `import_local(path) -> Result<std::filesystem::path>` — validates and registers a local DAT file
- `detect_version_change(path, db) -> Result<bool>` — compares checksum against stored version
- v0.1: local file import only (HTTP fetching deferred to future version)

#### [NEW] [src/romulus/dat/dat_parser.hpp](file:///c:/repos/romulus/src/romulus/dat/dat_parser.hpp)
#### [NEW] [src/romulus/dat/dat_parser.cpp](file:///c:/repos/romulus/src/romulus/dat/dat_parser.cpp)

Parses LogiqX XML format:
- `parse(path) -> Result<DatFile>` where `DatFile` contains header info + vector of `GameInfo`
- Extracts: `<header>` (name, description, version), `<game>` entries, `<rom>` entries with hashes
- **Auto-populates `systems` table**: derives system name from `<header><name>` (e.g. `"Nintendo - Game Boy"`), creates or reuses the system entry
- Normalizes data (trims whitespace, standardizes hash case to lowercase)
- Validates required fields (flags errors for incomplete entries)

**Expected DAT XML structure:**
```xml
<?xml version="1.0"?>
<!DOCTYPE datafile SYSTEM "http://www.logiqx.com/Dats/datafile.dtd">
<datafile>
  <header>
    <name>Nintendo - Game Boy</name>
    <description>Nintendo - Game Boy</description>
    <version>20240101-000000</version>
  </header>
  <game name="Alleyway (World)">
    <description>Alleyway (World)</description>
    <rom name="Alleyway (World).gb" size="32768" crc="9A085857" md5="..." sha1="..."/>
  </game>
</datafile>
```

---

### Phase 5: Scanner, Hasher, Matcher & Classifier

#### [NEW] [lib/romulus/scanner/rom_scanner.hpp](file:///c:/repos/romulus/lib/romulus/scanner/rom_scanner.hpp)
#### [NEW] [lib/romulus/scanner/rom_scanner.cpp](file:///c:/repos/romulus/lib/romulus/scanner/rom_scanner.cpp)

- `scan(directory, extensions) -> Result<vector<FileInfo>>`
- Recursively walks `std::filesystem::recursive_directory_iterator`
- Filters by extension: ROM files (`.nes`, `.gb`, `.gba`, `.smc`, `.md`, `.bin`, etc.) **AND archives** (`.zip`, `.7z`)
- For archives: opens with `libarchive`, lists entries, creates `FileInfo` per inner ROM file
- Records path, size; skips files already scanned (by path + mtime check)
- Uses `std::ranges` for filtering/transforming

#### [NEW] [lib/romulus/scanner/archive_service.hpp](file:///c:/repos/romulus/lib/romulus/scanner/archive_service.hpp)
#### [NEW] [lib/romulus/scanner/archive_service.cpp](file:///c:/repos/romulus/lib/romulus/scanner/archive_service.cpp)

- `is_archive(path) -> bool` — checks if file is a supported archive format
- `list_entries(path) -> Result<vector<ArchiveEntry>>` — lists ROM files inside an archive
- `stream_entry(path, entry_name, callback) -> Result<void>` — streams an entry's data in chunks to a callback (used by HashService)
- Supports: zip, 7z, tar, tar.gz — all via `libarchive`
- **Never extracts to disk** — all reading is in-memory via streaming

#### [NEW] [lib/romulus/scanner/hash_service.hpp](file:///c:/repos/romulus/lib/romulus/scanner/hash_service.hpp)
#### [NEW] [lib/romulus/scanner/hash_service.cpp](file:///c:/repos/romulus/lib/romulus/scanner/hash_service.cpp)

- `compute_hashes(path) -> Result<HashDigest>` — computes CRC32 + MD5 + SHA1 in single pass for regular files
- `compute_hashes_archive(path, entry_name) -> Result<HashDigest>` — hashes a specific entry inside an archive via streaming
- Reads data once, feeds to all 3 hash algorithms simultaneously
- Buffer size: 64KB for I/O efficiency
- **Avoids re-hashing**: checks if file hash is already in DB and file hasn't changed (size + mtime)

> [!NOTE]
> **Archive path convention**: Files inside archives are stored in the `files` table as `archive_path::entry_name`, e.g. `C:\roms\game.zip::Alleyway (World).gb`. This uniquely identifies both the archive and the entry within it.

#### [NEW] [src/romulus/engine/matcher.hpp](file:///c:/repos/romulus/src/romulus/engine/matcher.hpp)
#### [NEW] [src/romulus/engine/matcher.cpp](file:///c:/repos/romulus/src/romulus/engine/matcher.cpp)

- `match(files, db) -> Result<vector<MatchResult>>`
- Match priority: SHA1 > MD5 > CRC32 (most specific to least)
- Match types: `exact` (all 3 match), `sha1_only`, `crc_only`, `size_match`, `no_match`
- Uses database indexes for efficient lookup
- Batch matching with prepared statements

#### [NEW] [src/romulus/engine/classifier.hpp](file:///c:/repos/romulus/src/romulus/engine/classifier.hpp)
#### [NEW] [src/romulus/engine/classifier.cpp](file:///c:/repos/romulus/src/romulus/engine/classifier.cpp)

- `classify(db) -> Result<void>`
- For each ROM in the database, determines status:
  - `verified` — exact match exists in files
  - `missing` — no matching file found
  - `unverified` — partial match only
  - `mismatch` — file exists but hashes don't fully match
- Updates `rom_status` table transactionally
- Handles DAT version changes: reclassifies affected ROMs

---

### Phase 6: Report Generator & CLI

#### [NEW] [src/romulus/report/report_generator.hpp](file:///c:/repos/romulus/src/romulus/report/report_generator.hpp)
#### [NEW] [src/romulus/report/report_generator.cpp](file:///c:/repos/romulus/src/romulus/report/report_generator.cpp)

Report types:
- **Collection Summary**: Total ROMs, verified count, missing count, percentages per system
- **Missing ROMs Report**: List of all missing ROMs grouped by game/system
- **Duplicate Files Report**: Files matching the same ROM
- **Unverified Files Report**: Files that don't match any known ROM

Output formats:
- Plain text (console)
- CSV
- JSON (for programmatic consumption)

#### [NEW] [src/cli/main.cpp](file:///c:/repos/romulus/src/cli/main.cpp)

CLI11-based interface:

```
romulus import-dat <path|url> [--system <name>]
romulus scan <directory> [--system <name>] [--extensions .nes,.gb,...]
romulus verify [--system <name>]        # Run full match + classify
romulus sync <dat-url> <rom-dir>        # Full pipeline: fetch → parse → scan → match → classify
romulus report [summary|missing|duplicates|unverified] [--system <name>] [--format text|csv|json]
romulus systems                         # List all known systems
romulus status [--system <name>]        # Show DB state summary
```

---

## CMake Module Targets

| Target | Type | Location | Dependencies | Linked Libraries |
|--------|------|----------|-------------|------------------|
| `romulus_core` | INTERFACE | `lib/romulus/core/` | — | spdlog |
| `romulus_database` | STATIC | `lib/romulus/database/` | `romulus_core` | sqlite3 |
| `romulus_dat` | STATIC | `lib/romulus/dat/` | `romulus_core`, `romulus_database` | pugixml, cpp-httplib, OpenSSL |
| `romulus_scanner` | STATIC | `lib/romulus/scanner/` | `romulus_core`, `romulus_database` | OpenSSL |
| `romulus_engine` | STATIC | `lib/romulus/engine/` | `romulus_core`, `romulus_database` | — |
| `romulus_report` | STATIC | `lib/romulus/report/` | `romulus_core`, `romulus_database` | — |
| `romulus_service` | STATIC | `lib/romulus/service/` | All lib modules above | — |
| **`romulus_lib`** | **STATIC** | `lib/romulus/` | **Aggregates all above** | — |
| `romulus_cli` | EXECUTABLE | `apps/cli/` | `romulus_lib` | CLI11 |
| `romulus_api` | EXECUTABLE | `apps/api/` *(future)* | `romulus_lib` | cpp-httplib, nlohmann-json |
| `romulus_tests` | EXECUTABLE | `tests/` | `romulus_lib` | GTest |

The key insight: **`romulus_lib`** is the single library target that any C++ app links against. CLI and API server are thin shells.

The `web/` frontend is a **separate npm project** — it has its own build toolchain (Vite) and communicates with `romulus_api` over HTTP/JSON. It is NOT part of the CMake build.

> [!NOTE]
> **Future web stack**: When ready to add the web interface:
> 1. `apps/api/` — C++ REST API server using cpp-httplib (already a dependency), maps routes to `RomulusService` methods, returns JSON (nlohmann-json)
> 2. `web/` — React + TypeScript + Vite frontend, calls the REST API
> 3. Both can run independently — API server on port 8080, Vite dev server on port 5173 with proxy

Each module uses `target_include_directories(PRIVATE/PUBLIC)` — no global pollution.

---

## Open Questions

> [!IMPORTANT]
> 1. **DAT file source**: Should the initial implementation support HTTP downloads, or would local file import only be sufficient for v0.1? HTTP adds the OpenSSL + cpp-httplib dependencies.
>
> 2. **ROM file extensions**: What default set of extensions should be scanned? I'm proposing: `.nes, .snes, .smc, .sfc, .gb, .gbc, .gba, .nds, .md, .gen, .sms, .gg, .pce, .n64, .z64, .v64, .bin, .rom, .iso, .zip, .7z`. Should zip/7z archives be extracted and scanned in v0.1, or treated as opaque files?
>
> 3. **Concurrency/Threading**: The spec mentions handling 100k+ ROMs. Should v0.1 implement parallel hashing (e.g., thread pool for hash computation), or is sequential sufficient initially?
>
> 4. **Database location**: Default location for the SQLite DB — should it be in the ROM directory, in user's app data, or configurable via CLI flag?

---

## Verification Plan

### Automated Tests

1. **Unit Tests** (GTest):
   - `test_dat_parser.cpp` — Parse sample DAT, verify game/ROM extraction, test malformed input handling
   - `test_hash_service.cpp` — Hash known files, verify CRC32/MD5/SHA1 against expected values
   - `test_matcher.cpp` — Match files against ROM entries, verify match types
   - `test_classifier.cpp` — Classify ROMs with various match scenarios
   - `test_database.cpp` — CRUD operations, migration idempotency, transaction rollback
   - `test_report_generator.cpp` — Verify report content and formatting

2. **Integration Tests**:
   - `test_full_scan.cpp` — End-to-end: import DAT → scan dir → match → classify → report
   - `test_dat_update.cpp` — Import DAT v1, scan, then import DAT v2, verify reclassification

3. **Build Verification**:
   ```powershell
   cmake --preset dev
   cmake --build build --config Debug
   ctest --test-dir build --output-on-failure
   ```

### Manual Verification
- Build succeeds with zero warnings on MSVC (`/W4 /WX`)
- All tests pass
- CLI commands produce expected output with test data
- Idempotency: running the same scan twice produces identical DB state

---

## Execution Phases

| Phase | Scope | Key Deliverables |
|-------|-------|-----------------|
| **1** | Build System | CMake, vcpkg, presets, warnings, .clang-format/tidy, .gitignore |
| **2** | Core | Error types, shared types, logging facade |
| **3** | Database | SQLite wrapper, schema, migrations, CRUD |
| **4** | DAT | Fetcher (local files first), parser (LogiqX XML) |
| **5** | Scanner + Engine | RomScanner, HashService, Matcher, Classifier |
| **6** | Report + CLI | ReportGenerator, CLI11 interface, README, CHANGELOG |

Each phase is independently buildable and testable. Tests are written alongside each module.
