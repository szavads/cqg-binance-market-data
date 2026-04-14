# CMakeList.txt : CMake project for cqg_binance_service, include source and define
# OWNER szavads

cmake_minimum_required (VERSION 3.10)
project(cqg_binance_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build unit tests" ON)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Main executable
add_library(binance_service_core
  src/app.cpp
)

target_include_directories(binance_service_core PUBLIC src)

add_executable(binance_service
  src/main.cpp
)

target_link_libraries(binance_service PRIVATE binance_service_core)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()