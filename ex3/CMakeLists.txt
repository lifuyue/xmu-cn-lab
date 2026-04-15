cmake_minimum_required(VERSION 3.20)

project(ex3_pcap_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_library(PCAP_LIBRARY NAMES pcap REQUIRED)

add_library(ex3_core
    src/pcap_lab.cpp
)

target_include_directories(ex3_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ex3_core
    PUBLIC
        ${PCAP_LIBRARY}
)

foreach(target IN ITEMS pcap_stats ftp_login_sniffer http_login_sniffer)
    add_executable(${target} apps/${target}.cpp)
    target_link_libraries(${target} PRIVATE ex3_core)
endforeach()
