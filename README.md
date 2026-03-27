# ROMULUS

> *"In the beginning, there was the cartridge. And it was good."*

**ROMULUS** is a production-grade C++23 backend for cataloguing, verifying, and managing retro game ROM collections using No-Intro DAT files.

## Features

- 🎮 **No-Intro DAT parsing** — Import and version-track DAT files via pugixml
- 🔍 **ROM scanning** — Recursively scan directories and compute CRC32/MD5/SHA1 hashes
- ✅ **Matching & classification** — Identify Have/Missing/Duplicate/BadDump status per ROM
- 🗄️ **SQLite persistence** — All data stored in a local SQLite3 database
- 📊 **Report generation** — Text reports with per-system statistics
- ⚡ **C++23** — Uses `std::expected`, `std::ranges`, `std::filesystem`, and `constexpr`

## Requirements

| Tool | Version |
|------|---------|
| CMake | ≥ 3.25 |
| GCC | ≥ 13 or Clang ≥ 16 |
| vcpkg | manifest mode |

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| sqlite3 | 3.52.0 | Database |
| pugixml | 1.15 | XML/DAT parsing |
| spdlog | 1.17.0 | Logging |
| openssl | 3.6.1 | MD5/SHA1 hashing |
| catch2 | 3.13.0 | Unit testing |

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/usr/local/share/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --parallel
cd build && ctest --output-on-failure
```

## Architecture

```
src/
├── common/          # Shared types and error enums
├── database/        # SQLite3 persistence layer
├── dat_fetcher/     # DAT file loading
├── dat_parser/      # No-Intro XML parsing (pugixml)
├── hash_service/    # CRC32/MD5/SHA1 hashing (OpenSSL EVP)
├── rom_scanner/     # Recursive directory scanner
├── matcher/         # ROM-to-file matching
├── classifier/      # Have/Missing/Duplicate/BadDump status
└── report_generator/# Text report output
```

## License

MIT
