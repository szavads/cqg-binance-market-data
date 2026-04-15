# CMakeList.txt : CMake project for cqg_binance_service, include source and define
# OWNER szavads

cmake_minimum_required (VERSION 3.10)

option(BUILD_TESTS "Build unit tests" ON)

cmake_policy(SET CMP0091 NEW)

project(cqg_binance_service LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find packages
find_package(Boost REQUIRED COMPONENTS system)
find_package(websocketpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(OpenSSL REQUIRED)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

if(MSVC)
  if(WIN32)
    add_compile_definitions(WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  endif()
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
  string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG:FULL")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /DEBUG:FULL")
endif()

# Main executable
add_library(binance_service_core
  src/app.cpp
)

target_include_directories(binance_service_core PUBLIC src)

add_executable(binance_service
  src/main.cpp
  src/network/WebSocketClient.cpp
)

target_link_libraries(binance_service PRIVATE
  binance_service_core
    Boost::system
    websocketpp::websocketpp
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto 
)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()