cmake_minimum_required(VERSION 3.10)
project(qlc_datagen CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(qlc_datagen
    qlc_datagen.cpp
    ini/Ini.cpp
    logger/Logger.cpp
    config/Config.cpp
)

target_include_directories(qlc_datagen PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_compile_options(qlc_datagen PRIVATE -Wall -O2)
