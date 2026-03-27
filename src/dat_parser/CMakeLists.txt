find_package(spdlog CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

add_library(romulus_dat_parser STATIC dat_parser.cpp)
target_include_directories(romulus_dat_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(romulus_dat_parser
  PUBLIC romulus_common
  PRIVATE pugixml::pugixml spdlog::spdlog
)
