find_package(Catch2 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_executable(test_database test_database.cpp)
target_link_libraries(test_database PRIVATE romulus_database Catch2::Catch2WithMain spdlog::spdlog)

add_executable(test_dat_parser test_dat_parser.cpp)
target_link_libraries(test_dat_parser PRIVATE romulus_dat_parser Catch2::Catch2WithMain spdlog::spdlog)

add_executable(test_hash_service test_hash_service.cpp)
target_link_libraries(test_hash_service PRIVATE romulus_hash_service Catch2::Catch2WithMain spdlog::spdlog)

add_executable(test_matcher test_matcher.cpp)
target_link_libraries(test_matcher PRIVATE romulus_matcher Catch2::Catch2WithMain spdlog::spdlog)

add_executable(test_classifier test_classifier.cpp)
target_link_libraries(test_classifier PRIVATE romulus_classifier Catch2::Catch2WithMain spdlog::spdlog)

add_executable(test_report_generator test_report_generator.cpp)
target_link_libraries(test_report_generator PRIVATE romulus_report_generator Catch2::Catch2WithMain spdlog::spdlog)

include(Catch)
catch_discover_tests(test_database)
catch_discover_tests(test_dat_parser)
catch_discover_tests(test_hash_service)
catch_discover_tests(test_matcher)
catch_discover_tests(test_classifier)
catch_discover_tests(test_report_generator)
