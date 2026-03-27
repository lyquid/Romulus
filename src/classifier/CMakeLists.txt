find_package(spdlog CONFIG REQUIRED)

add_library(romulus_classifier STATIC classifier.cpp)
target_include_directories(romulus_classifier PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(romulus_classifier
  PUBLIC romulus_common romulus_database
  PRIVATE spdlog::spdlog
)
