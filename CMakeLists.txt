cmake_minimum_required (VERSION 2.6)
project (asio-pipe-transport)
add_executable(debug-driver/debug-driver debug-driver/main.cpp)
add_executable(test-driver/test-driver test-driver/main.cpp)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

find_package(Boost 1.47 REQUIRED system)

target_link_libraries(debug-driver/debug-driver ${Boost_LIBRARIES})
target_link_libraries(test-driver/test-driver ${Boost_LIBRARIES})

