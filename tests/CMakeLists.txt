cmake_minimum_required(VERSION 3.16)
project(zwift_ride_hid_host_tests LANGUAGES CXX)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_executable(
  zwift_ride_hid_host_tests
  host_core_tests.cpp
  "${PROJECT_ROOT}/components/zwift_ride_hid/ride_protocol.cpp"
  "${PROJECT_ROOT}/components/zwift_ride_hid/input_state.cpp"
  "${PROJECT_ROOT}/components/zwift_ride_hid/keymap.cpp"
  "${PROJECT_ROOT}/components/zwift_ride_hid/idle_policy.cpp"
)

target_include_directories(zwift_ride_hid_host_tests PRIVATE "${PROJECT_ROOT}")
target_compile_features(zwift_ride_hid_host_tests PRIVATE cxx_std_17)
target_compile_options(zwift_ride_hid_host_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_test(NAME zwift_ride_hid_host_tests COMMAND zwift_ride_hid_host_tests)
