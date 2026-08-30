# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
# 自定义组件目录：本工程的驱动组件统一放在 drivers/ 下
set(EXTRA_COMPONENT_DIRS "drivers")
project(esp32s3)

