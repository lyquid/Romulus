find_package(spdlog CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(romulus_hash_service STATIC hash_service.cpp)
target_include_directories(romulus_hash_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(romulus_hash_service
  PUBLIC romulus_common
  PRIVATE OpenSSL::Crypto spdlog::spdlog
)
