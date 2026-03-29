cmake_minimum_required(VERSION 3.16)
project(libcpujitter C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(cpujitter
    src/cpujitter.c
    src/platform_detect.c
    src/json_profiles.c
    src/cache.c
    src/entropy_backend.c
    src/recalibrate.c
)

target_include_directories(cpujitter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(dice_app examples/dice_app.c)
target_link_libraries(dice_app PRIVATE cpujitter)

enable_testing()
add_executable(cpujitter_tests tests/test_cpujitter.c)
target_link_libraries(cpujitter_tests PRIVATE cpujitter)
target_compile_definitions(cpujitter_tests PRIVATE CPUJITTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME cpujitter_tests COMMAND cpujitter_tests)
