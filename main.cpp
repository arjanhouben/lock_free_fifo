#include <iostream>
#include <functional>
#include <thread>
#include <sstream>

#include <atomic>
#include <vector>
#include <chrono>
#include <mutex>
#include <map>

#include <lock_free/fifo.h>

using namespace std;
using namespace chrono;

typedef function< void() >function_type;

#ifdef USE_BOOST
#include <boost/asio/io_service.hpp>
#include <boost/lockfree/queue.hpp>
using namespace boost::asio;

template < typename T >
struct boostlockfree
{
	boostlockfree( size_t r = 1024 ) :
	jobs_( r ) {}
	
	inline void push_back( T t )
	{
		jobs_.push( t );
	}
	
	inline bool pop( T &t )
	{
		return jobs_.pop( t );
	}
	
	boost::lockfree::queue< T > jobs_;
};

template < typename T >
struct boostasio
{
	boostasio( size_t = 1024 ) {}
	
	inline void push_back( T t )
	{
		service_.post( *t );
	}
	
	inline bool pop( T &t )
	{
		static function_type tmp = [](){};
		t = &tmp;
		return service_.run_one() > 0;
	}
	
	io_service service_;
};
#endif

#ifdef USE_TBB
#include "tbb/concurrent_queue.h"

template < typename T >
struct inteltbb
{
	inteltbb( size_t = 1024 ) :
	jobs_() {}
	
	inline void push_back( T t )
	{
		jobs_.push( t );
	}
	
	inline bool pop( T &t )
	{
		return jobs_.try_pop( t );
	}
	
	tbb::concurrent_queue< T > jobs_;
};
#endif

template < typename T >
struct mutex_queue
{
	mutex_queue( size_t r = 1024 ) :
		lock_(),
		index_( 0 ),
		data_( r )
	{
		data_.clear();
	}

	inline void push_back( const T &t )
	{
		lock_guard< mutex > guard( lock_ );
		data_.push_back( t );
	}

	inline bool pop( T &t )
	{
		lock_guard< mutex > guard( lock_ );

		if ( index_ == data_.size() ) { return false; }

		t = data_[ index_++ ];
		return true;
	}

	mutex lock_;
	size_t index_;
	vector< T > data_;
};

enum
{
	Producer = 0,
	Consumer = 1,
	Result = 2
};

template < typename Q >
void test( const string &testname, size_t count, size_t threadcount )
{
	auto create_producer_consumer_result = [=]( const string &name )
	{
		high_resolution_clock::time_point t1 = high_resolution_clock::now();

		auto data = make_shared< Q >( count );
		
		auto tmp = new function_type(
			[data]()
			{
				++data->consumer_count;
			}
		);
		
		function_type producer = [data,tmp]()
		{
			while ( data->producer_count++ < data->expected )
			{
				data->queue.push_back( tmp );
			}

			if ( data->producer_count >= data->expected )
			{
				--data->producer_count;
			}
		};

		function_type consumer = [data]()
		{
			while ( data->consumer_count < data->expected )
			{
				function_type *func;

				while ( data->queue.pop( func ) )
				{
					(*func)();
				}
			}
		};

		function_type result = [=]()
		{
			high_resolution_clock::time_point t2 = high_resolution_clock::now();

			duration< double > time_span = duration_cast< duration< double > >( t2 - t1 );

			if ( data->expected != data->consumer_count )
			{
				cout << "\texpected: " << data->expected << ", actual: " << data->consumer_count << endl;
			}

			cout << '\t' << name << " took: " << time_span.count() << " seconds" << endl;
			
			delete tmp;
		};

		return make_tuple( producer, consumer, result );
	};

	high_resolution_clock::time_point teststart = high_resolution_clock::now();

	cout << testname << ":\n{\n";

	// single producer, single consumer
	{
		auto pcr = create_producer_consumer_result( "single producer, single consumer" );

		get< Producer >( pcr )();

		get< Consumer >( pcr )();

		get< Result >( pcr )();
	}

	// single producer, multi consumer
	{
		auto pcr = create_producer_consumer_result( "single producer, multi consumer" );

		get< Producer >( pcr )();

		vector< thread > threads;
		size_t c = threadcount;

		while ( c-- )
		{
			threads.push_back( thread( get< Consumer >( pcr ) ) );
		}

		for ( auto &t : threads )
		{
			t.join();
		}

		get< Result >( pcr )();
	}

	// multi producer, single consumer
	{
		auto pcr = create_producer_consumer_result( "multi producer, single consumer" );

		vector< thread > threads;
		size_t c = threadcount;

		while ( c-- )
		{
			threads.push_back( thread( get< Producer >( pcr ) ) );
		}

		for ( auto &t : threads )
		{
			t.join();
		}

		get< Consumer >( pcr )();

		get< Result >( pcr )();
	}

	// multi producer, multi consumer
	{
		auto pcr = create_producer_consumer_result( "multi producer, multi consumer" );

		vector< thread > threads;
		size_t c = threadcount / 2;

		while ( c-- )
		{
			threads.push_back( thread( get< Producer >( pcr ) ) );
			threads.push_back( thread( get< Consumer >( pcr ) ) );
		}

		for ( auto &t : threads )
		{
			t.join();
		}

		get< Result >( pcr )();
	}

	duration< double > time_span = duration_cast< duration< double > >( high_resolution_clock::now() - teststart );
	cout << "\ttotal: " << time_span.count() << " seconds\n}" << endl;
}

template < typename T >
struct test_data
{
	test_data( size_t e ) :
	expected( e ),
	queue(),
	producer_count( 0 ),
	consumer_count( 0 ) { }

	const size_t expected;
	T queue;
	atomic_size_t producer_count;
	atomic_size_t consumer_count;
};

int main( int argc, char *argv[] )
{
	const auto test_count = 1e6;

	const auto thread_count = 16;

	map< string, function_type > tests
	{
		{
			"0",
			[=]()
			{
				test< test_data< lock_free::fifo< function_type* > > >( "lock_free::fifo", test_count, thread_count );
			},
		},
		{
			"1",
			[=]()
			{
				test< test_data< mutex_queue< function_type* > > >( "mutex_queue", test_count, thread_count );
			}
		}
#ifdef USE_BOOST
		,
		{
			"2",
			[=]()
			{
				test< test_data< boostlockfree< function_type* > > >( "boostlockfree", test_count, thread_count );
			}
		},
		{
			"3",
			[=]()
			{
				test< test_data< boostasio< function_type* > > >( "boostasio", test_count, thread_count );
			},
		}
#endif
#ifdef USE_TBB
		,
		{
			"4",
			[=]()
			{
				test< test_data< inteltbb< function_type* > > >( "inteltbb", test_count, thread_count );
			}
		}
#endif
	};
	
	if ( argc == 1 )
	{
		for( auto &test : tests )
		{
			test.second();
		}
	}
	else
	{
		for ( auto c = 1; c < argc; ++c )
		{
			if ( auto test = tests[ argv[ c ] ] )
			{
				test();
			}
		}
	}

	return 0;
}
