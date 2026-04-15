find_package(GTest REQUIRED)

add_executable(binance_service_tests
    ConfigTests.cpp
    FileWriterTests.cpp
    AggregatorTests.cpp
    ../src/config/Config.cpp
    ../src/storage/FileWriter.cpp
    ../src/core/Aggregator.cpp
)

target_include_directories(binance_service_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(binance_service_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    Threads::Threads
)

include(GoogleTest)
gtest_discover_tests(binance_service_tests)