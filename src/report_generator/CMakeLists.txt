find_package(spdlog CONFIG REQUIRED)

add_library(romulus_report_generator STATIC report_generator.cpp)
target_include_directories(romulus_report_generator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(romulus_report_generator
  PUBLIC romulus_common romulus_database
  PRIVATE spdlog::spdlog
)
