cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(yogyui-esp32-sink-iot)

idf_build_set_property(COMPILE_OPTIONS "-Wformat=0" APPEND)