# ROMULUS — Build Task List

## Phase 1: Project Skeleton & Build System
- [/] Root CMakeLists.txt
- [ ] CMakePresets.json
- [ ] vcpkg.json
- [ ] vcpkg-configuration.json
- [ ] cmake/CompilerWarnings.cmake
- [ ] cmake/StaticAnalysis.cmake
- [ ] .clang-format
- [ ] .clang-tidy
- [ ] .gitignore
- [x] .github/copilot-instructions.md
- [x] .github/instructions/cpp.instructions.md
- [x] .github/instructions/cmake.instructions.md
- [x] .github/workflows/ci.yml
- [x] .github/workflows/release.yml
- [x] .releaserc.json
- [ ] lib/romulus/CMakeLists.txt
- [ ] apps/cli/CMakeLists.txt
- [ ] apps/api/CMakeLists.txt (placeholder)
- [ ] apps/web/README.md (placeholder)
- [ ] tests/CMakeLists.txt

## Phase 2: Core Module
- [ ] lib/romulus/core/error.hpp
- [ ] lib/romulus/core/types.hpp
- [ ] lib/romulus/core/logging.hpp

## Phase 2b: Service API Layer
- [ ] lib/romulus/service/romulus_service.hpp
- [ ] lib/romulus/service/romulus_service.cpp

## Phase 3: Database Module
- [ ] lib/romulus/database/database.hpp
- [ ] lib/romulus/database/database.cpp

## Phase 4: DAT Import & Parsing
- [ ] lib/romulus/dat/dat_fetcher.hpp
- [ ] lib/romulus/dat/dat_fetcher.cpp
- [ ] lib/romulus/dat/dat_parser.hpp
- [ ] lib/romulus/dat/dat_parser.cpp

## Phase 5: Scanner, Hasher, Matcher & Classifier
- [ ] lib/romulus/scanner/rom_scanner.hpp
- [ ] lib/romulus/scanner/rom_scanner.cpp
- [ ] lib/romulus/scanner/archive_service.hpp
- [ ] lib/romulus/scanner/archive_service.cpp
- [ ] lib/romulus/scanner/hash_service.hpp
- [ ] lib/romulus/scanner/hash_service.cpp
- [ ] lib/romulus/engine/matcher.hpp
- [ ] lib/romulus/engine/matcher.cpp
- [ ] lib/romulus/engine/classifier.hpp
- [ ] lib/romulus/engine/classifier.cpp

## Phase 6: Report Generator, CLI & Docs
- [ ] lib/romulus/report/report_generator.hpp
- [ ] lib/romulus/report/report_generator.cpp
- [ ] apps/cli/main.cpp
- [ ] README.md
- [ ] CHANGELOG.md

## Testing
- [ ] tests/unit/test_dat_parser.cpp
- [ ] tests/unit/test_hash_service.cpp
- [ ] tests/unit/test_matcher.cpp
- [ ] tests/unit/test_classifier.cpp
- [ ] tests/unit/test_database.cpp
- [ ] tests/unit/test_report_generator.cpp
- [ ] tests/integration/test_full_scan.cpp
- [ ] tests/integration/test_dat_update.cpp
- [ ] tests/fixtures/sample.dat

## Verification
- [ ] CMake configure succeeds
- [ ] Build succeeds with zero warnings
- [ ] All tests pass
