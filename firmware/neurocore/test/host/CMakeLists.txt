cmake_minimum_required(VERSION 3.16)
project(neurocore_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
enable_testing()

set(NC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(REPO_ROOT ${NC_DIR}/../..)

add_executable(test_intent_fsm
  test_intent_fsm.c
  ${NC_DIR}/intent/intent_fsm.c)
target_include_directories(test_intent_fsm PRIVATE
  ${NC_DIR}/intent ${NC_DIR}/intent/tests ${REPO_ROOT}/protocol/nip ${REPO_ROOT}/tests/c)
target_compile_options(test_intent_fsm PRIVATE -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow)
if(NOT WIN32)
  target_compile_options(test_intent_fsm PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_intent_fsm PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME intent_fsm COMMAND test_intent_fsm)

add_executable(test_dsp
  test_dsp.c
  ${NC_DIR}/dsp/biquad.c
  ${NC_DIR}/dsp/emg_detect.c)
target_include_directories(test_dsp PRIVATE ${NC_DIR}/dsp ${NC_DIR}/dsp/tests ${REPO_ROOT}/tests/c)
target_compile_options(test_dsp PRIVATE -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow -Wdouble-promotion)
target_link_libraries(test_dsp PRIVATE m)
if(NOT WIN32)
  target_compile_options(test_dsp PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_dsp PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME dsp COMMAND test_dsp)
