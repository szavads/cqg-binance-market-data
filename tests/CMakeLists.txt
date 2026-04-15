find_package(GTest REQUIRED)

add_executable(binance_service_tests
    ConfigTests.cpp
    FileWriterTests.cpp
    ../src/config/Config.cpp
    ../src/storage/FileWriter.cpp
    ../src/core/Aggregator.cpp
    main_test.cpp
)

target_link_libraries(binance_service_tests PRIVATE
    binance_service_core
    GTest::gtest
    GTest::gtest_main
	nlohmann_json::nlohmann_json
)

include(GoogleTest)
gtest_discover_tests(binance_service_tests)