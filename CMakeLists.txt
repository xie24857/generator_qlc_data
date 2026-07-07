cmake_minimum_required(VERSION 3.10)
project(generator CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(generator
    main.cpp
    ini/Ini.cpp
    logger/Logger.cpp
    config/Config.cpp
    algo/QuotaShuffleGenerator.cpp
    algo/PageProcessor.cpp
    algo/BlockGenerator.cpp
)

target_include_directories(generator PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(generator PRIVATE
    pthread
)

target_compile_options(generator PRIVATE
    -O3
    -march=native
    -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=
)
