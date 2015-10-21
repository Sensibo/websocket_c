cmake_minimum_required(VERSION 3.3)
project(websocket_c)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    main.c src/websocket_client.h src/websocket_client.c
        src/debug_utils.h src/debug_utils.c
        )

add_executable(websocket_c ${SOURCE_FILES})