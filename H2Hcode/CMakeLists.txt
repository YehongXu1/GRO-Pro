cmake_minimum_required(VERSION 3.17)
project(H2HCode)

set(CMAKE_CXX_STANDARD 23)

set(BOOST_ROOT "/usr/local/include")

include_directories(.)

FIND_PACKAGE(Boost)
IF (Boost_FOUND)
    INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})
    ADD_DEFINITIONS( "-DHAS_BOOST" )
ENDIF()

add_executable(H2H.cpp
        H2H.cpp
        head.h
        main.cpp)
