cmake_minimum_required(VERSION 3.16)
project(libcpujitter C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CPUJITTER_ENABLE_MOCK_BACKEND "Enable NON-PRODUCTION mock backend when jitterentropy is unavailable" ON)
option(CPUJITTER_BUILD_PROBE "Run dice_app --probe after build" ON)

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

set(CPUJITTER_JENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/jitterentropy")
set(CPUJITTER_JENT_HEADER "${CPUJITTER_JENT_DIR}/jitterentropy.h")
file(GLOB CPUJITTER_JENT_SOURCES CONFIGURE_DEPENDS "${CPUJITTER_JENT_DIR}/*.c")

if (EXISTS "${CPUJITTER_JENT_HEADER}" AND CPUJITTER_JENT_SOURCES)
    message(STATUS "Using vendored jitterentropy from external/jitterentropy")
    add_library(cpujitter_jent STATIC ${CPUJITTER_JENT_SOURCES})
    target_include_directories(cpujitter_jent PUBLIC ${CPUJITTER_JENT_DIR})
    target_include_directories(cpujitter PRIVATE ${CPUJITTER_JENT_DIR})
    target_link_libraries(cpujitter PRIVATE cpujitter_jent)
else()
    if (CPUJITTER_ENABLE_MOCK_BACKEND)
        message(WARNING "Building with NON-PRODUCTION mock backend (vendored jitterentropy header+sources not found in external/jitterentropy; run scripts/fetch_external_deps.sh).")
        target_compile_definitions(cpujitter PRIVATE CPUJITTER_ENABLE_MOCK_BACKEND=1)
    else()
        message(FATAL_ERROR "vendored jitterentropy not found (expected jitterentropy.h and *.c in external/jitterentropy) and CPUJITTER_ENABLE_MOCK_BACKEND=OFF")
    endif()
endif()

add_executable(dice_app examples/dice_app.c)
target_link_libraries(dice_app PRIVATE cpujitter m)

if (CPUJITTER_BUILD_PROBE)
    add_custom_command(TARGET dice_app POST_BUILD
        COMMAND $<TARGET_FILE:dice_app> --probe --dump ${CMAKE_BINARY_DIR}/cpujitter_probe_random.bin --meta ${CMAKE_BINARY_DIR}/cpujitter_probe_meta.txt
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "cpujitter probe: selected vars + estimated entropy per bit + random dump")
endif()

enable_testing()
add_executable(cpujitter_tests tests/test_cpujitter.c)
target_link_libraries(cpujitter_tests PRIVATE cpujitter)
target_compile_definitions(cpujitter_tests PRIVATE CPUJITTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME cpujitter_tests COMMAND cpujitter_tests)

add_executable(cpujitter_match_tests tests/test_profile_matching.c)
target_link_libraries(cpujitter_match_tests PRIVATE cpujitter)
add_test(NAME cpujitter_match_tests COMMAND cpujitter_match_tests)
