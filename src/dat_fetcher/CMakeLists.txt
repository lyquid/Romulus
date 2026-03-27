find_package(spdlog CONFIG REQUIRED)

add_library(romulus_dat_fetcher STATIC dat_fetcher.cpp)
target_include_directories(romulus_dat_fetcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(romulus_dat_fetcher
  PUBLIC romulus_common
  PRIVATE spdlog::spdlog
)
