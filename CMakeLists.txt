# CMakeList.txt : CMake project for cqg_binance_service, include source and define
# OWNER szavads

cmake_minimum_required (VERSION 3.10)
project(cqg_binance_service LANGUAGES CXX)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Main executable
add_executable(binance_service
    src/main.cpp
)

target_include_directories(binance_service PRIVATE src)

#if (CMAKE_VERSION VERSION_GREATER 3.12)
#  set_property(TARGET binance_service PROPERTY CXX_STANDARD 20)
#endif()

# TODO: Add tests and install targets when it's time to do that
# Tests
#option(BUILD_TESTS "Build unit tests" ON)
#if(BUILD_TESTS)
#    add_subdirectory(tests)
#endif()