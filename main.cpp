#include <iostream>
#include <atomic>
#include <functional>
#include <thread>

#include <vector>
#include <map>

using namespace std;

typedef function< void() > function_type;

template < typename Value >
class lock_free_list
{
	public:
	
		typedef Value value_type;
	
		lock_free_list() :
			start_(),
			end_() { }
		
		template < typename T >
		void add_job( T &&func )
		{
			node_ptr newnode( new node_type( forward< T >( func ) ) );
			
			node_ptr tmp = nullptr;
			start_.compare_exchange_strong( tmp, newnode );
			
			node_ptr prev_end = end_.exchange( newnode );
			if ( prev_end )
			{
				prev_end->next = newnode;
			}
		}
		
		bool take_job( value_type &func )
		{
			node_ptr tmp = start_;
			
			while ( tmp )
			{
				if ( start_.compare_exchange_weak( tmp, tmp->next ) )
				{
					func = tmp->func;
					
					delete tmp;
					
					return true;
				}
				// if we got here, tmp was set to the value of start_, so we try again
			}
			
			return false;
		}
	
	private:
		
		struct node_type;
		typedef node_type* node_ptr;
		typedef atomic< node_ptr > node;
	
		struct node_type
		{
			node_type( const value_type &original ) :
				func( original ),
				next( nullptr ) { }
			
			value_type func;
			node_ptr next;
		};
	
		node start_, end_;
};

function_type go;

typedef lock_free_list< function_type > jobqueue;

int main( int argc, char *argv[] )
{
	jobqueue queue;
	
	const auto expected = 1e7;
	
	atomic_size_t actual( 0 );
	for ( int i = 0; i < expected; ++i )
	{
		queue.add_job(
			[&]()
			{
				++actual;
			}
		);
	}
	
	go = [ &queue ]()
	{
		function_type func;
		while ( queue.take_job( func ) )
		{
			func();
		}
	};
	
	struct gothread : thread
	{
		gothread() : thread( go ) { }
	};
	
	vector< gothread > threads( 20 );
	
	for ( auto &t : threads )
	{
		t.join();
	}
	
	cout << "expected: " << expected << " got: " << actual << endl;
	
	return ( expected != actual );
}