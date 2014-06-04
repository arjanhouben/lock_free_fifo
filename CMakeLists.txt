project( fifo )

cmake_minimum_required( VERSION 2.8 )

if( NOT MSVC )
    set( CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11\ -Wall )
endif()

include_directories( include )

add_executable( fifotest
    main.cpp
    include/lock_free/fifo.h
    include/lock_free/shared_mutex.h
)
