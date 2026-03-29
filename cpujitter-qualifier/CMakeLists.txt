cmake_minimum_required(VERSION 3.16)
project(cpujitter_qualifier C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(QUALIFIER_ENABLE_NIST "Enable optional NIST hook logic" OFF)

add_library(qualifier_core
    src/qualifier.c
    src/platform_detect.c
    src/config_loader.c
    src/evaluator.c
    src/exporter.c
    src/nist_hook.c
)

target_include_directories(qualifier_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(qualifier_core PRIVATE
    QUALIFIER_ENABLE_NIST_DEFAULT=$<IF:$<BOOL:${QUALIFIER_ENABLE_NIST}>,1,0>
)

add_executable(cpujitter_qualifier src/main.c)
target_link_libraries(cpujitter_qualifier PRIVATE qualifier_core)
target_compile_definitions(cpujitter_qualifier PRIVATE QUALIFIER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()
add_executable(qualifier_smoke_test tests/smoke_test.c)
target_link_libraries(qualifier_smoke_test PRIVATE qualifier_core)
target_compile_definitions(qualifier_smoke_test PRIVATE QUALIFIER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME qualifier_smoke_test COMMAND qualifier_smoke_test)
