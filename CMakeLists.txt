project( fifo )

cmake_minimum_required( VERSION 2.8 )

if( NOT MSVC )
	set( CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++11\ -Wall )
endif()

find_package( Boost COMPONENTS system )

find_path( TBB_INCLUDE_DIR tbb/concurrent_queue.h
	PATH_SUFFIXES
		tbb
)

find_library( TBB_LIBRARY tbb )

if( TBB_LIBRARY AND TBB_INCLUDE_DIR )
	add_definitions( -DUSE_TBB )
else()
	set( TBB_LIBRARY "" )
	set( TBB_INCLUDE_DIR "" )
endif()

if( Boost_SYSTEM_LIBRARY AND Boost_INCLUDE_DIR )
	add_definitions( -DUSE_BOOST )
else()
	set( Boost_SYSTEM_LIBRARY "" )
	set( Boost_INCLUDE_DIR "" )
endif()

include_directories(
	include
	${Boost_INCLUDE_DIR}
	${TBB_INCLUDE_DIR}
)

add_executable( fifotest
    main.cpp
    include/lock_free/fifo.h
    include/lock_free/shared_mutex.h
)

target_link_libraries(
	fifotest
	${Boost_SYSTEM_LIBRARY}
	${TBB_LIBRARY}
)

install(
	DIRECTORY
		include/lock_free
	DESTINATION
		include
)
