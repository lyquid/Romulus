find_package(spdlog CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(romulus_database STATIC database.cpp)
target_include_directories(romulus_database PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(romulus_database
  PUBLIC romulus_common
  PRIVATE SQLite::SQLite3 spdlog::spdlog
)
