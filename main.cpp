#include <iostream>
#include <functional>
#include <thread>
#include <sstream>

#include <atomic>
#include <vector>

#include "include/lock_free/fifo.h"

using namespace std;

typedef function< void() > function_type;

typedef lock_free::fifo< function_type > jobqueue;

template < typename T >
T to( const string &str )
{
	T result;
	stringstream( str ) >> result;
	return result;
}

template < typename  T >
function_type get_producer( T &&t )
{
	return get< 0 >( t );
}

template < typename  T >
function_type get_consumer( T &&t )
{
	return get< 1 >( t );
}

template < typename  T >
function_type get_result( T &&t )
{
	return get< 2 >( t );
}

int main( int argc, char *argv[] )
{
	auto create_producer_consumer_result = []()
	{
		struct data_type
		{
			data_type( size_t e ) :
				expected( e ),
				queue(),
				producer_count( 0 ),
				consumer_count( 0 ) { }
			const size_t expected;
			jobqueue queue;
			atomic_size_t producer_count;
			atomic_size_t consumer_count;
		};
		
		auto data = make_shared< data_type >( 1e6 );
		
		auto producer = [data]()
		{
			while ( data->producer_count++ < data->expected )
			{
				data->queue.push(
					[data]()
					{
						++data->consumer_count;
					}
				);
			}
		};
		
		auto consumer = [data]()
		{
			while ( data->consumer_count < data->expected )
			{
				function_type func;
				while ( data->queue.pop( func ) )
				{
					func();
				}
			}
		};
		
		auto result = [data]()
		{
			cout << "expected: " << data->expected << ", actual: " << data->consumer_count << endl;
		};
		
		return make_tuple( producer, consumer, result );
	};

	const auto thread_count = argc > 1 ? to< size_t >( argv[ 1 ] ) : 20;

	// single producer, single consumer
	if(1){
		auto pcr = create_producer_consumer_result();
		
		get_producer( pcr )();
		
		get_consumer( pcr )();
		
		get_result( pcr )();
	}
	
	// single producer, multi consumer
	if(1){
		auto pcr = create_producer_consumer_result();
		
		get_producer( pcr )();
		
		vector< thread > threads;
		size_t c = thread_count;
		while ( c-- )
		{
			threads.push_back( thread( get_consumer( pcr ) ) );
		}
		
		for ( auto &t : threads )
		{
			t.join();
		}
		
		get_result( pcr )();
	}
	
	// multi producer, single consumer
	if(1){
		auto pcr = create_producer_consumer_result();
		
		vector< thread > threads;
		size_t c = thread_count;
		while ( c-- )
		{
			threads.push_back( thread( get_producer( pcr ) ) );
		}
		
		for ( auto &t : threads )
		{
			t.join();
		}
		
		get_consumer( pcr )();
		
		get_result( pcr )();
	}
	
	// multi producer, multi consumer
	if(1){
		auto pcr = create_producer_consumer_result();
		
		vector< thread > threads;
		size_t c = thread_count / 2;
		while ( c-- )
		{
			threads.push_back( thread( get_producer( pcr ) ) );
			threads.push_back( thread( get_consumer( pcr ) ) );
		}
		
		for ( auto &t : threads )
		{
			t.join();
		}
		
		get_result( pcr )();
	}

	return 0;
}
