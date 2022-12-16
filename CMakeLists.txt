cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

project(ora C)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Find Tarantool
set(Tarantool_FIND_REQUIRED ON)
find_package(Tarantool)
include_directories(${TARANTOOL_INCLUDE_DIRS})

# Find Oracle
set(ORACLE_FIND_REQUIRED ON)
find_package(Oracle)
include_directories(${ORACLE_INCLUDE_DIR})
link_directories(${ORACLE_LIBRARY_DIR})

# Set CFLAGS
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra")

if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -undefined dynamic_lookup")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -undefined dynamic_lookup")
endif()

# Build module
add_subdirectory(ora)

add_custom_target(test
    COMMAND ${PROJECT_SOURCE_DIR}/test/ora.test.lua)

## VERSION ####################################################################
###############################################################################

execute_process(
  COMMAND git describe --tags --always
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE GIT_DESCRIBE
  ERROR_QUIET
)

if (NOT GIT_DESCRIBE)
  set(GIT_DESCRIBE "unknown")
endif()

configure_file (
  "${PROJECT_SOURCE_DIR}/ora/VERSION.lua.in"
  "${CMAKE_CURRENT_BINARY_DIR}/VERSION.lua"
)

set (LUA_PATH "LUA_PATH=${PROJECT_SOURCE_DIR}/?.lua\\;${PROJECT_SOURCE_DIR}/?/init.lua\\;;")
