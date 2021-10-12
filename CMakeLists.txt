# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

#get_filename_component(ENV_IDF_PATH "lib/esp8266-rtos-sdk" ABSOLUTE)
#set(IDF_PATH "lib/esp8266-rtos-sdk")
#set(IDF_PATH ${ENV_IDF_PATH} CACHE PATH "somehting new")
#set(ENV{IDF_PATH} ${ENV_IDF_PATH})

message("env IDF_PATH: " $ENV{IDF_PATH})

include(lib/esp8266-rtos-sdk/tools/cmake/project.cmake)
project(dwss)
