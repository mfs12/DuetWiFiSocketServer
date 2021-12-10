# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

#get_filename_component(ENV_IDF_PATH "lib/esp8266-rtos-sdk" ABSOLUTE)
#set(IDF_PATH "lib/esp8266-rtos-sdk")
#set(IDF_PATH ${ENV_IDF_PATH} CACHE PATH "somehting new")
#set(ENV{IDF_PATH} ${ENV_IDF_PATH})

message("env IDF_PATH: " $ENV{IDF_PATH})

include(lib/esp8266-rtos-sdk/tools/cmake/project.cmake)
project(dwss)

add_custom_command( OUTPUT dwss-image.bin
	DEPENDS bootloader/bootloader.bin partition_table/partition-table.bin dwss.bin
	COMMAND ${PROJECT_SOURCE_DIR}/tools/image-create.sh
                    bootloader/bootloader.bin
                    partition_table/partition-table.bin
                    dwss.bin
                    dwss-image.bin
)

add_custom_target( image-create ALL
	DEPENDS dwss-image.bin
	COMMAND echo "*** image dwss-image.bin successfully created ***"
)
