cmake_minimum_required(VERSION 3.16)
project(draz VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Password hashing runs 600,000 rounds, which is slow without optimizations.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(draz_core
  src/crypto.cpp
  src/storage.cpp
  src/store.cpp
)
target_include_directories(draz_core PUBLIC include)

if(MSVC)
  target_compile_options(draz_core PUBLIC /W4 /utf-8 /permissive-)
  target_compile_definitions(draz_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(draz_core PUBLIC -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  target_link_libraries(draz_core PUBLIC bcrypt)
endif()

add_executable(draz src/main.cpp src/app.cpp src/console.cpp)
target_link_libraries(draz PRIVATE draz_core)

option(DRAZ_BUILD_TESTS "Build the tests" ON)
if(DRAZ_BUILD_TESTS)
  enable_testing()
  add_executable(draz_tests
    tests/main.cpp
    tests/test_crypto.cpp
    tests/test_storage.cpp
    tests/test_structures.cpp
    tests/test_store.cpp
  )
  target_link_libraries(draz_tests PRIVATE draz_core)
  add_test(NAME draz_tests COMMAND draz_tests)
endif()
