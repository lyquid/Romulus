# ── romulus_lib ────────────────────────────────────────────────
# Aggregates all library modules into a single STATIC library
# that CLI, API server, and tests link against.

# ── Find dependencies ─────────────────────────────────────────
find_package(SQLite3 REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ── Collect sources ───────────────────────────────────────────
set(ROMULUS_SOURCES
    # Core (header-only for now, but logging has implementation)
    core/logging.cpp

    # Database
    database/database.cpp

    # DAT
    dat/dat_fetcher.cpp
    dat/dat_parser.cpp

    # Scanner
    scanner/rom_scanner.cpp
    scanner/archive_service.cpp
    scanner/hash_service.cpp

    # Engine
    engine/matcher.cpp
    engine/classifier.cpp

    # Report
    report/report_generator.cpp

    # Service
    service/romulus_service.cpp
)

add_library(romulus_lib STATIC ${ROMULUS_SOURCES})
add_library(romulus::lib ALIAS romulus_lib)

target_include_directories(romulus_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(romulus_lib PUBLIC cxx_std_23)

target_link_libraries(romulus_lib
    PUBLIC
        spdlog::spdlog
    PRIVATE
        SQLite::SQLite3
        pugixml::pugixml
        LibArchive::LibArchive
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
)

romulus_set_warnings(romulus_lib)
