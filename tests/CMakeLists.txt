# ── Tests ─────────────────────────────────────────────────────
find_package(GTest CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include(GoogleTest)

# ── Test fixture path ─────────────────────────────────────────
set(ROMULUS_TEST_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(ROMULUS_REPO_DATS_DIR "${CMAKE_SOURCE_DIR}/dats")

# ── Unit tests ────────────────────────────────────────────────
set(UNIT_TEST_SOURCES
    unit/test_archive_service.cpp
    unit/test_dat_parser.cpp
    unit/test_hash_service.cpp
    unit/test_matcher.cpp
    unit/test_classifier.cpp
    unit/test_dat_auditor.cpp
    unit/test_database.cpp
    unit/test_report_generator.cpp
    unit/test_romulus_service.cpp
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

# ── GUI logic unit tests ───────────────────────────────────────
# Tests pure utility functions from gui_logic.hpp that have no ImGui/GLFW dependency.
add_executable(romulus_gui_logic_tests
    unit/test_gui_logic.cpp
)

target_link_libraries(romulus_gui_logic_tests
    PRIVATE
        romulus::lib
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(romulus_gui_logic_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/apps/gui
)

target_compile_features(romulus_gui_logic_tests PRIVATE cxx_std_23)

romulus_set_warnings(romulus_gui_logic_tests)

gtest_discover_tests(romulus_gui_logic_tests)

# ── Integration tests ─────────────────────────────────────────
set(INTEGRATION_TEST_SOURCES
    fixtures/synthetic_fixture_lab.cpp
    integration/test_full_scan.cpp
    integration/test_dat_update.cpp
    integration/test_synthetic_scenario_lab.cpp
    integration/test_operation_framework.cpp
)

add_executable(romulus_integration_tests ${INTEGRATION_TEST_SOURCES})

target_link_libraries(romulus_integration_tests
    PRIVATE
        romulus::lib
        GTest::gtest
        GTest::gtest_main
        LibArchive::LibArchive
        nlohmann_json::nlohmann_json
)

target_include_directories(romulus_integration_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)

target_compile_features(romulus_integration_tests PRIVATE cxx_std_23)

target_compile_definitions(romulus_integration_tests
    PRIVATE
        ROMULUS_TEST_FIXTURES_DIR="${ROMULUS_TEST_FIXTURES_DIR}"
        ROMULUS_REPO_DATS_DIR="${ROMULUS_REPO_DATS_DIR}"
)

romulus_set_warnings(romulus_integration_tests)

gtest_discover_tests(romulus_integration_tests)

# ── Manual synthetic fixture generator ───────────────────────
# The executable and integration tests compile the same implementation. This is important: a
# developer inspecting the GUI must see byte-for-byte the same hostile collection exercised by CI.
add_executable(romulus_fixture_generator
    fixtures/generate_synthetic_fixtures.cpp
    fixtures/synthetic_fixture_lab.cpp
)

target_link_libraries(romulus_fixture_generator
    PRIVATE
        romulus::lib
        LibArchive::LibArchive
        nlohmann_json::nlohmann_json
)

target_include_directories(romulus_fixture_generator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)

target_compile_features(romulus_fixture_generator PRIVATE cxx_std_23)
romulus_set_warnings(romulus_fixture_generator)

set_target_properties(romulus_fixture_generator PROPERTIES
    OUTPUT_NAME romulus-fixture-generator
)
