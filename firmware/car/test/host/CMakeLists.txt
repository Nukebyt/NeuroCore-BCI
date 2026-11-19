cmake_minimum_required(VERSION 3.16)
project(car_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
enable_testing()

set(CAR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(REPO_ROOT ${CAR_DIR}/../..)

add_executable(test_car_ctrl
  test_car_ctrl.c
  ${CAR_DIR}/control/car_ctrl.c
  ${CAR_DIR}/safety/link_watchdog.c
  ${REPO_ROOT}/protocol/nip/nip.c)
target_include_directories(test_car_ctrl PRIVATE
  ${CAR_DIR}/control ${CAR_DIR}/safety ${REPO_ROOT}/protocol/nip ${REPO_ROOT}/tests/c)
target_compile_options(test_car_ctrl PRIVATE -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow)
if(NOT WIN32)
  target_compile_options(test_car_ctrl PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_car_ctrl PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME car_ctrl COMMAND test_car_ctrl)
