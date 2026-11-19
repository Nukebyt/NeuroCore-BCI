cmake_minimum_required(VERSION 3.16)
project(nip_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
enable_testing()

set(NIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

add_executable(test_nip test_nip.c ${NIP_DIR}/nip.c)
target_include_directories(test_nip PRIVATE ${NIP_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/tests/c)
target_compile_options(test_nip PRIVATE -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow)
if(NOT WIN32)
  target_compile_options(test_nip PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_nip PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME nip_codec COMMAND test_nip)
