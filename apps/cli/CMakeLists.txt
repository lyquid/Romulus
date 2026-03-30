# ── romulus_cli ────────────────────────────────────────────────
find_package(CLI11 CONFIG REQUIRED)

add_executable(romulus_cli main.cpp)

target_link_libraries(romulus_cli
    PRIVATE
        romulus::lib
        CLI11::CLI11
)

target_compile_features(romulus_cli PRIVATE cxx_std_23)

romulus_set_warnings(romulus_cli)

# Output as 'romulus' executable
set_target_properties(romulus_cli PROPERTIES
    OUTPUT_NAME romulus
)
