cmake_minimum_required(VERSION 3.16)
project(OthelloGame C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
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
if(GIT_VERSION)
    target_compile_definitions(othello_core PUBLIC OTHELLO_VERSION="${GIT_VERSION}")
endif()
add_executable(othello src/main.c)
target_link_libraries(othello PRIVATE othello_core)

enable_testing()
foreach(test_name test_move_validation test_capture test_game_storage test_command_parser test_misc)
    add_executable(${test_name} tests/${test_name}.c)
    target_link_libraries(${test_name} PRIVATE othello_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
