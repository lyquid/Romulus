add_subdirectory(common)
add_subdirectory(database)
add_subdirectory(dat_fetcher)
add_subdirectory(dat_parser)
add_subdirectory(hash_service)
add_subdirectory(rom_scanner)
add_subdirectory(matcher)
add_subdirectory(classifier)
add_subdirectory(report_generator)

find_package(spdlog CONFIG REQUIRED)

add_executable(romulus main.cpp)
target_link_libraries(romulus PRIVATE
  romulus_database
  romulus_dat_fetcher
  romulus_dat_parser
  romulus_hash_service
  romulus_rom_scanner
  romulus_matcher
  romulus_classifier
  romulus_report_generator
  spdlog::spdlog
)
