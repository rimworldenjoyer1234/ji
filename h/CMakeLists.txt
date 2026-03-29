cmake_minimum_required(VERSION 3.16)
project(autojitter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(JENT_ROOT "$ENV{HOME}/Documents/jitterentropy-library" CACHE PATH "Path to jitterentropy-library")

if(NOT EXISTS "${JENT_ROOT}/jitterentropy.h")
    message(FATAL_ERROR "Could not find jitterentropy.h in ${JENT_ROOT}")
endif()

set(JENT_SOURCES
    ${JENT_ROOT}/src/jitterentropy-base.c
    ${JENT_ROOT}/src/jitterentropy-gcd.c
    ${JENT_ROOT}/src/jitterentropy-health.c
    ${JENT_ROOT}/src/jitterentropy-noise.c
    ${JENT_ROOT}/src/jitterentropy-sha3.c
    ${JENT_ROOT}/src/jitterentropy-status.c
    ${JENT_ROOT}/src/jitterentropy-timer.c
)

foreach(f ${JENT_SOURCES})
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "Missing Jitterentropy source file: ${f}")
    endif()
endforeach()

add_library(jent_local STATIC ${JENT_SOURCES})

target_include_directories(jent_local
    PUBLIC
        ${JENT_ROOT}
        ${JENT_ROOT}/src
)

target_link_libraries(jent_local
    PUBLIC
        pthread
        rt
)

add_executable(autotune autotune.c)
target_link_libraries(autotune PRIVATE jent_local)

add_executable(hello hello.c)
target_link_libraries(hello PRIVATE jent_local)

install(TARGETS autotune hello
    RUNTIME DESTINATION bin
)

install(CODE [[
    execute_process(
        COMMAND "${CMAKE_INSTALL_PREFIX}/bin/autotune"
        WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
        RESULT_VARIABLE AUTOTUNE_RESULT
    )
    if(NOT AUTOTUNE_RESULT EQUAL 0)
        message(FATAL_ERROR "autotune failed during installation")
    endif()
]])
