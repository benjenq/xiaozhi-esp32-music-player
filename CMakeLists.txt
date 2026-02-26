# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

set(EXTRA_COMPONENT_DIRS
    $ENV{ADF_PATH}/components/audio_pipeline
    $ENV{ADF_PATH}/components/audio_stream
    $ENV{ADF_PATH}/components/audio_board
    $ENV{ADF_PATH}/components/audio_hal
    $ENV{ADF_PATH}/components/audio_sal
    $ENV{ADF_PATH}/components/audio_recorder
    $ENV{ADF_PATH}/components/esp_actions
    $ENV{ADF_PATH}/components/esp-adf-libs
    $ENV{ADF_PATH}/components/esp_dispatcher
    $ENV{ADF_PATH}/components/esp_peripherals
    $ENV{ADF_PATH}/components/clouds
    $ENV{ADF_PATH}/components/display_service
    $ENV{ADF_PATH}/components/dueros_service
    $ENV{ADF_PATH}/components/tone_partition
    $ENV{ADF_PATH}/components/wifi_service
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.2.3")
project(xiaozhi)
