#include <iostream>
#include <functional>
#include <thread>
#include <sstream>

#include <atomic>
#include <vector>

#include "include/lock_free/fifo.h"

using namespace std;

typedef function< void() > function_type;

function_type go;

typedef lock_free::fifo< function_type > jobqueue;

template < typename T >
T to( const string &str )
{
	stringstream stream( str );
	T result;
	stream >> result;
	return result;
}

int main( int argc, char *argv[] )
{
	jobqueue queue;
	
	const auto expected = 1e5;
	
	atomic_size_t actual( 0 );
	for ( auto i = 0; i < expected; ++i )
	{
		queue.push(
			[&]()
			{
				++actual;
			}
		);
	}
	
	go = [ &queue ]()
	{
		function_type func;
		while ( queue.pop( func ) )
		{
			func();
		}
	};
	
	struct gothread : thread
	{
		gothread() : thread( go ) { }
	};
	
	auto thread_count = argc > 1 ? to< size_t >( argv[ 1 ] ) : 20;
	
	vector< gothread > threads( thread_count );
	
	for ( auto &t : threads )
	{
		t.join();
	}
	
	cout << "expected: " << expected << " got: " << actual << endl;
	
	return ( expected != actual );
}