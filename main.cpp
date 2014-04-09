#include <iostream>
#include <atomic>
#include <functional>
#include <thread>

#include <vector>
#include <map>

using namespace std;

typedef function< void() > function_type;

class job_list
{
	public:
	
		job_list() :
			start_(),
			end_() { }
		
		void addJob( const function_type &func )
		{
			node newnode = make_shared< node_raw >();
			newnode->func = func;
			
			node tmp;
			atomic_compare_exchange_strong( &start_, &tmp, newnode );
			
			node prev_end = atomic_exchange( &end_, newnode );
			if ( prev_end )
			{
				prev_end->next = newnode;
			}
		}
		
		bool takeJob( function_type &func )
		{
			node tmp = atomic_load( &start_ );
			
			while ( tmp )
			{
				if ( atomic_compare_exchange_weak( &start_, &tmp, tmp->next ) )
				{
					func = tmp->func;
					
					return true;
				}
				// if we got here, tmp was set to the value of start_, so we try again
			}
			
			return false;
		}
	
	private:
		
		struct node_raw;
		typedef shared_ptr< node_raw > node;
	
		struct node_raw
		{
			function_type func;
			node next;
		};
	
		node start_, end_;
};

function_type go;

int main( int argc, char *argv[] )
{
	job_list queue;
	
	auto count = 0;
	for ( int i = 0; i < 1e3; ++i )
	{
		queue.addJob(
			[&]()
			{
				++count;
			}
		);
	}
	
	go = [ &queue ]()
	{
		function_type func;
		while ( queue.takeJob( func ) )
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
	
	cout << count << endl;
	
	return 0;
}