#  Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT License.

cmake_minimum_required (VERSION 3.20)

project (Dryer-RT C)

azsphere_configure_tools(TOOLS_REVISION "22.11")

# include drivers
include_directories(mt3620-m4-drivers-master)

# Create executable
add_executable (${PROJECT_NAME} main.c)
target_link_libraries (${PROJECT_NAME})
set_target_properties (${PROJECT_NAME} PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/linker.ld)

azsphere_target_add_image_package(${PROJECT_NAME})
