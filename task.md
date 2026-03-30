# ROMULUS — Build Task List

## Phase 1: Project Skeleton & Build System
- [x] Root CMakeLists.txt
- [x] CMakePresets.json
- [x] vcpkg.json
- [x] vcpkg-configuration.json
- [x] cmake/CompilerWarnings.cmake
- [x] cmake/StaticAnalysis.cmake
- [x] .clang-format
- [x] .clang-tidy
- [x] .gitignore
- [x] .github/copilot-instructions.md
- [x] .github/instructions/cpp.instructions.md
- [x] .github/instructions/cmake.instructions.md
- [x] .github/workflows/ci.yml
- [x] .github/workflows/release.yml
- [x] .releaserc.json
- [x] lib/romulus/CMakeLists.txt
- [x] apps/cli/CMakeLists.txt
- [x] apps/api/README.md (placeholder)
- [x] web/README.md (placeholder)
- [x] tests/CMakeLists.txt

## Phase 2: Core Module
- [x] lib/romulus/core/error.hpp
- [x] lib/romulus/core/types.hpp
- [x] lib/romulus/core/logging.hpp

## Phase 2b: Service API Layer
- [x] lib/romulus/service/romulus_service.hpp
- [x] lib/romulus/service/romulus_service.cpp

## Phase 3: Database Module
- [x] lib/romulus/database/database.hpp
- [x] lib/romulus/database/database.cpp

## Phase 4: DAT Import & Parsing
- [x] lib/romulus/dat/dat_fetcher.hpp
- [x] lib/romulus/dat/dat_fetcher.cpp
- [x] lib/romulus/dat/dat_parser.hpp
- [x] lib/romulus/dat/dat_parser.cpp

## Phase 5: Scanner, Hasher, Matcher & Classifier
- [x] lib/romulus/scanner/rom_scanner.hpp
- [x] lib/romulus/scanner/rom_scanner.cpp
- [x] lib/romulus/scanner/archive_service.hpp
- [x] lib/romulus/scanner/archive_service.cpp
- [x] lib/romulus/scanner/hash_service.hpp
- [x] lib/romulus/scanner/hash_service.cpp
- [x] lib/romulus/engine/matcher.hpp
- [x] lib/romulus/engine/matcher.cpp
- [x] lib/romulus/engine/classifier.hpp
- [x] lib/romulus/engine/classifier.cpp

## Phase 6: Report Generator, CLI & Docs
- [x] lib/romulus/report/report_generator.hpp
- [x] lib/romulus/report/report_generator.cpp
- [x] apps/cli/main.cpp
- [x] README.md
- [x] CHANGELOG.md

## Testing
- [x] tests/unit/test_dat_parser.cpp
- [x] tests/unit/test_hash_service.cpp
- [x] tests/unit/test_matcher.cpp
- [x] tests/unit/test_classifier.cpp
- [x] tests/unit/test_database.cpp
- [x] tests/unit/test_report_generator.cpp
- [x] tests/integration/test_full_scan.cpp
- [x] tests/integration/test_dat_update.cpp
- [x] tests/fixtures/sample.dat

## Verification
- [x] CMake configure succeeds
- [x] Build succeeds with zero warnings
- [x] All tests pass
