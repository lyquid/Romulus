cmake_minimum_required(VERSION 3.25)

project(
    romulus
    VERSION 0.1.0
    DESCRIPTION "Production-grade ROM collection verification system"
    LANGUAGES CXX
)

# ── C++ Standard ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── CMake Modules ─────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(StaticAnalysis)

# ── Build Options ─────────────────────────────────────────────
option(ROMULUS_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
option(BUILD_TESTING "Build unit and integration tests" ON)

if(ROMULUS_ENABLE_CLANG_TIDY)
    romulus_enable_clang_tidy()
endif()

# ── Export compile commands for tooling ────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Subdirectories ────────────────────────────────────────────
add_subdirectory(lib)
add_subdirectory(apps)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
