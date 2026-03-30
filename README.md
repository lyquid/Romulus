# ROMULUS

> *Imposes order on chaos — and keeps a record of it.*

A production-grade C++23 backend system for verifying and cataloging video game ROM collections using No-Intro DAT files.

## What It Does

ROMULUS takes your DAT files and ROM directories, then tells you exactly what you have, what you're missing, and what doesn't match.

```
romulus import-dat "Nintendo - Game Boy (20240101).dat"
romulus scan C:\ROMs\GameBoy
romulus verify
romulus report summary
```

```
╔══════════════════════════════════════════════════╗
║           ROMULUS — Collection Summary           ║
╠══════════════════════════════════════════════════╣
║ System:     Nintendo - Game Boy                  ║
╠══════════════════════════════════════════════════╣
║ Total ROMs: 1437                                 ║
║ Verified:   1285 (89%)                           ║
║ Missing:    134                                  ║
║ Unverified: 12                                   ║
║ Mismatch:   6                                    ║
╚══════════════════════════════════════════════════╝
```

## Features

- **DAT Import** — Parses No-Intro LogiqX XML format
- **Archive Support** — Reads zip/7z files without extracting to disk
- **Parallel Hashing** — CRC32 + MD5 + SHA1 in a single pass using all CPU cores
- **Smart Scanning** — Skips unchanged files, tracks file modifications
- **Multi-Hash Matching** — SHA1 > MD5 > CRC32 priority matching
- **Reports** — Summary, missing ROMs, duplicates in text/CSV/JSON
- **Multi-System** — Track multiple systems in one database

## Architecture

```
lib/romulus/          → Core C++ library (all business logic)
apps/cli/             → CLI frontend (this is romulus.exe)
apps/api/  (future)   → REST API server for web frontend
web/       (future)   → React/TypeScript web interface
```

## Building

### Requirements

- C++23 compiler (MSVC 17.8+, GCC 13+, Clang 17+)
- CMake ≥ 3.25
- vcpkg

### Build

```bash
# Configure (Debug)
cmake --preset dev

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

### Release Build

```bash
cmake --preset release
cmake --build build --config Release
```

## Usage

```bash
# Import a DAT file
romulus import-dat path/to/dat_file.dat

# Scan a ROM directory
romulus scan /path/to/roms

# Match files and classify ROM status
romulus verify

# Full pipeline (import → scan → verify)
romulus sync path/to/dat.dat /path/to/roms

# Reports
romulus report summary                    # Text summary
romulus report missing --format json      # Missing ROMs as JSON
romulus report summary --format csv       # CSV export
romulus report summary --system "Nintendo - Game Boy"

# List known systems
romulus systems

# Quick status check
romulus status
```

## Pipeline

```
DAT Import → Scan → Hash → Match → Classify → Report
    │          │       │       │        │          │
    ▼          ▼       ▼       ▼        ▼          ▼
 Systems    Files   CRC32    SHA1    Verified    Text
 Games      Scan    MD5      MD5     Missing     CSV
 ROMs       Skip    SHA1     CRC32   Unverified  JSON
            Arch.
```

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++23 |
| Build | CMake 3.25+ / vcpkg |
| Database | SQLite3 (WAL mode) |
| XML Parsing | pugixml |
| Hashing | OpenSSL (MD5/SHA1) + constexpr CRC32 |
| Archives | libarchive (zip/7z/tar) |
| CLI | CLI11 |
| Logging | spdlog |
| JSON | nlohmann-json |
| Testing | Google Test |

## Contributing

- Follow the C++ Core Guidelines (see `.github/copilot-instructions.md`)
- Conventional Commits: `feat(scope)`, `fix(scope)`, `refactor(scope)`
- Format code with `clang-format` before committing
- All PRs must pass CI (MSVC + GCC + Clang)

## License

MIT
