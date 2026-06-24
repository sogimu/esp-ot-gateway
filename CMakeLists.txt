cmake_minimum_required(VERSION 3.16)

# ESP-IDF project — подключает всю систему сборки через IDF_PATH
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp-ot-gateway)

# web_page_gz target is defined in main/CMakeLists.txt
add_dependencies(esp-ot-gateway.elf web_page_gz)
