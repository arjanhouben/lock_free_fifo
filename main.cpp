#include <iostream>
#include <functional>
#include <thread>
#include <sstream>

#include <atomic>
#include <vector>
#include <chrono>
#include <mutex>

#include <lock_free/fifo.h>
#include <lock_free/shared_mutex.h>

#include <boost/asio/io_service.hpp>
#include <boost/lockfree/queue.hpp>


using namespace std;
using namespace chrono;
using namespace boost::asio;

typedef function< void() >function_type;

template < typename T >
struct boostlockfree
{
	boostlockfree( size_t r = 1024 ) :
		jobs_( r ) {}

	void push_back( T t )
	{
		jobs_.push( new T( t ) );
	}

	bool pop( T &t )
	{
		T *tmp = nullptr;
		if ( jobs_.pop( tmp ) )
		{
			t = *tmp;
			delete tmp;
			return true;
		}
		return false;
	}

	boost::lockfree::queue< T* > jobs_;
};

template < typename T >
struct boostasio
{
	boostasio( size_t = 1024 ) {}

	void push_back( T t )
	{
		service_.post( t );
	}

	bool pop( T &t )
	{
		t = [](){};
		return service_.run_one() > 0;
	}

	io_service service_;
};

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

	void push_back( const T &t )
	{
		lock_guard< mutex > guard( lock_ );
		data_.push_back( t );
	}

	bool pop( T &t )
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

template < typename Q >
void test( const string &testname, size_t count, size_t threadcount )
{
	auto create_producer_consumer_result = [=]( const string &name )
	{
		high_resolution_clock::time_point t1 = high_resolution_clock::now();

		auto data = make_shared< Q >( count );

		function_type producer = [data]()
		{
			while ( data->producer_count++ < data->expected )
			{
				data->queue.push_back(
					[data]()
					{
						++data->consumer_count;
					}
				);
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
				function_type func;

				while ( data->queue.pop( func ) )
				{
					func();
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
		};

		return make_tuple( producer, consumer, result );
	};

	high_resolution_clock::time_point teststart = high_resolution_clock::now();

	cout << testname << ":\n{\n";

	// single producer, single consumer
	{
		auto pcr = create_producer_consumer_result( "single producer, single consumer" );

		get_producer( pcr )();

		get_consumer( pcr )();

		get_result( pcr )();
	}

	// single producer, multi consumer
	{
		auto pcr = create_producer_consumer_result( "single producer, multi consumer" );

		get_producer( pcr )();

		vector< thread > threads;
		size_t c = threadcount;

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
	{
		auto pcr = create_producer_consumer_result( "multi producer, single consumer" );

		vector< thread > threads;
		size_t c = threadcount;

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
	{
		auto pcr = create_producer_consumer_result( "multi producer, multi consumer" );

		vector< thread > threads;
		size_t c = threadcount / 2;

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

	const auto thread_count = argc > 1 ? to< size_t >( argv[ 1 ] ) : 16;

	test< test_data< boostlockfree< function_type > > >( "boostlockfree", test_count, thread_count );
	test< test_data< boostasio< function_type > > >( "boostasio", test_count, thread_count );
	test< test_data< lock_free::fifo< function_type > > >( "lock_free::fifo", test_count, thread_count );
	test< test_data< mutex_queue< function_type > > >( "mutex_queue", test_count, thread_count );

	return 0;
}
