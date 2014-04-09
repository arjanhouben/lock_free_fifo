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
			node_raw *newnode( new node_raw() );
			newnode->func = func;
			
			node_raw *tmp = nullptr;
			start_.compare_exchange_strong( tmp, newnode );
			
			node_raw *prev_end = end_.exchange( newnode );
			if ( prev_end )
			{
				prev_end->next = newnode;
			}
		}
		
		bool takeJob( function_type &func )
		{
			node_raw *tmp = start_;
			
			while ( tmp )
			{
				if ( start_.compare_exchange_weak( tmp, tmp->next ) )
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
		typedef atomic< node_raw* > node;
	
		struct node_raw
		{
			function_type func;
			node_raw *next;
		};
	
		node start_, end_;
};

function_type go;

int main( int argc, char *argv[] )
{
	job_list queue;
	
	auto count = 0;
	for ( int i = 0; i < 1e5; ++i )
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