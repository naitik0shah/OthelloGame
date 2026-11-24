cmake_minimum_required(VERSION 3.16)
project(OthelloGame VERSION 1.2.1 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Keep output binaries in build/ directory consistently across all generators
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(OTHELLO_SOURCES
    src/common.c
    src/validation_capture.c
    src/cli_platform.c
    src/cli_render.c
    src/cli_commands.c
    src/storage.c
    src/stats.c
    src/misc.c
)
add_library(othello_core STATIC ${OTHELLO_SOURCES})
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_VERSION)
    set(GIT_VERSION "v${PROJECT_VERSION}")
endif()
target_compile_definitions(othello_core PUBLIC OTHELLO_VERSION="${GIT_VERSION}")
add_executable(othello src/main.c)
target_link_libraries(othello PRIVATE othello_core)

enable_testing()
foreach(test_name test_validation_and_capture test_game_storage test_command_parser test_misc)
    add_executable(${test_name} tests/${test_name}.c)
    target_link_libraries(${test_name} PRIVATE othello_core)
    if(MSVC)
        target_compile_options(${test_name} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${test_name} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()