# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
# 自定义组件目录：drivers/ 存放硬件驱动组件，app/ 存放业务层组件
set(EXTRA_COMPONENT_DIRS "app" "drivers")
project(esp32s3)

