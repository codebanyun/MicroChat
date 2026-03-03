cmake_minimum_required(VERSION 3.10)
project(MicroChat-Backend)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/message)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/user)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/file)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/speech)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/forward)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/friend)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/gateway)
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)