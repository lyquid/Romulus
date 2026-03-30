# ── Tests ─────────────────────────────────────────────────────
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# ── Test fixture path ─────────────────────────────────────────
set(ROMULUS_TEST_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(ROMULUS_REPO_DATS_DIR "${CMAKE_SOURCE_DIR}/dats")

# ── Unit tests ────────────────────────────────────────────────
set(UNIT_TEST_SOURCES
    unit/test_dat_parser.cpp
    unit/test_hash_service.cpp
    unit/test_matcher.cpp
    unit/test_classifier.cpp
    unit/test_database.cpp
    unit/test_report_generator.cpp
)

add_executable(romulus_unit_tests ${UNIT_TEST_SOURCES})

target_link_libraries(romulus_unit_tests
    PRIVATE
        romulus::lib
        GTest::gtest
        GTest::gtest_main
)

target_compile_features(romulus_unit_tests PRIVATE cxx_std_23)

target_compile_definitions(romulus_unit_tests
    PRIVATE
        ROMULUS_TEST_FIXTURES_DIR="${ROMULUS_TEST_FIXTURES_DIR}"
        ROMULUS_REPO_DATS_DIR="${ROMULUS_REPO_DATS_DIR}"
)

romulus_set_warnings(romulus_unit_tests)

gtest_discover_tests(romulus_unit_tests)

# ── Integration tests ─────────────────────────────────────────
set(INTEGRATION_TEST_SOURCES
    integration/test_full_scan.cpp
    integration/test_dat_update.cpp
)

add_executable(romulus_integration_tests ${INTEGRATION_TEST_SOURCES})

target_link_libraries(romulus_integration_tests
    PRIVATE
        romulus::lib
        GTest::gtest
        GTest::gtest_main
)

target_compile_features(romulus_integration_tests PRIVATE cxx_std_23)

target_compile_definitions(romulus_integration_tests
    PRIVATE
        ROMULUS_TEST_FIXTURES_DIR="${ROMULUS_TEST_FIXTURES_DIR}"
        ROMULUS_REPO_DATS_DIR="${ROMULUS_REPO_DATS_DIR}"
)

romulus_set_warnings(romulus_integration_tests)

gtest_discover_tests(romulus_integration_tests)
