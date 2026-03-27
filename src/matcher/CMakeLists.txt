find_package(spdlog CONFIG REQUIRED)

add_library(romulus_matcher STATIC matcher.cpp)
target_include_directories(romulus_matcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(romulus_matcher
  PUBLIC romulus_common romulus_database
  PRIVATE spdlog::spdlog
)
