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

# The BLE sources talk to ESP-IDF and ESPHome directly, so they are compiled
# unmodified against the fakes in tests/fakes rather than being refactored to
# suit the tests. Only the stack beneath them is replaced.
add_executable(
  zwift_ride_hid_ride_client_tests
  ride_client_tests.cpp
  fakes/fake_ble.cpp
  fakes/millis.cpp
  "${PROJECT_ROOT}/components/zwift_ride_hid/ride_client.cpp"
)

target_include_directories(
  zwift_ride_hid_ride_client_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/fakes"
  "${PROJECT_ROOT}"
)
target_compile_definitions(
  zwift_ride_hid_ride_client_tests PRIVATE USE_ESP32 USE_ESP32_FRAMEWORK_ESP_IDF
)
target_compile_features(zwift_ride_hid_ride_client_tests PRIVATE cxx_std_17)
target_compile_options(zwift_ride_hid_ride_client_tests PRIVATE -Wall -Wextra -Werror)

add_executable(
  zwift_ride_hid_hid_keyboard_tests
  hid_keyboard_tests.cpp
  fakes/fake_ble.cpp
  fakes/millis.cpp
  "${PROJECT_ROOT}/components/zwift_ride_hid/hid_keyboard.cpp"
)

target_include_directories(
  zwift_ride_hid_hid_keyboard_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/fakes"
  "${PROJECT_ROOT}"
)
target_compile_definitions(
  zwift_ride_hid_hid_keyboard_tests PRIVATE USE_ESP32 USE_ESP32_FRAMEWORK_ESP_IDF
)
target_compile_features(zwift_ride_hid_hid_keyboard_tests PRIVATE cxx_std_17)
target_compile_options(zwift_ride_hid_hid_keyboard_tests PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME zwift_ride_hid_host_tests COMMAND zwift_ride_hid_host_tests)
add_test(NAME zwift_ride_hid_ride_client_tests COMMAND zwift_ride_hid_ride_client_tests)
add_test(NAME zwift_ride_hid_hid_keyboard_tests COMMAND zwift_ride_hid_hid_keyboard_tests)
