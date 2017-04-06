cmake_minimum_required (VERSION 3.1)

project(ray)

add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0) #compatibility with CNTK binary release

file(GLOB sources ${CMAKE_SOURCE_DIR}/src/*.h ${CMAKE_SOURCE_DIR}/src/*.cpp)

include_directories(${CMAKE_SOURCE_DIR}/cntk/Include)
link_directories(${CMAKE_SOURCE_DIR}/cntk/lib)

add_executable(ray ${sources})
target_link_libraries(ray eval pthread)