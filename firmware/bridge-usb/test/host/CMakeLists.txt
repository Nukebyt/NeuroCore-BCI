cmake_minimum_required(VERSION 3.16)
project(bridge_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
enable_testing()

set(BRIDGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(REPO_ROOT ${BRIDGE_DIR}/../..)

add_executable(test_bridge
  test_bridge.c
  ${BRIDGE_DIR}/core/cobs.c
  ${BRIDGE_DIR}/core/bridge.c
  ${REPO_ROOT}/protocol/nip/nip.c)
target_include_directories(test_bridge PRIVATE
  ${BRIDGE_DIR}/core ${REPO_ROOT}/protocol/nip ${REPO_ROOT}/tests/c)
target_compile_options(test_bridge PRIVATE -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow)
if(NOT WIN32)
  target_compile_options(test_bridge PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_bridge PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME bridge COMMAND test_bridge)
