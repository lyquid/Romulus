find_package(spdlog CONFIG REQUIRED)

add_library(romulus_rom_scanner STATIC rom_scanner.cpp)
target_include_directories(romulus_rom_scanner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(romulus_rom_scanner
  PUBLIC romulus_common romulus_hash_service
  PRIVATE spdlog::spdlog
)
