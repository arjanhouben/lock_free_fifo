project( jobthread )

if( NOT MSVC )
    set( CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11\ -Wall )
endif()

add_executable( jobthread
    main.cpp
    include/lock_free/fifo.h
)
