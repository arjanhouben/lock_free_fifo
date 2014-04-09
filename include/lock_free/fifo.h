#pragma once

#include <atomic>

namespace lock_free
{
	/**
	 * This is a lock free fifo, which can be used for multi-producer, multi-consumer
	 * type job queue
	 */
	template < typename Value, typename Allocator = std::allocator< Value > >
	class fifo
	{
		public:
			
			typedef Value value_type;
			typedef Allocator allocator_type;
			
			fifo() :
				start_(),
				end_() {}
			
			/**
			 * pushes an item into the job queue, may throw if allocation fails
			 * leaving the queue unchanged
			 */
			template < typename T >
			void push( T &&func )
			{
				node_ptr newnode( new node_type( std::forward< T >( func ) ) );
				
				node_ptr tmp = nullptr;
				start_.compare_exchange_strong( tmp, newnode );
				
				node_ptr prev_end = end_.exchange( newnode );
				if ( prev_end )
				{
					prev_end->next = newnode;
				}
			}
		
			/**
			 * retrieves an item from the job queue.
			 * if no item was available, func is untouched and pop returns false
			 */
			bool pop( value_type &func )
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
		
			fifo( const fifo& );
			fifo& operator = ( const fifo& );
			
			struct node_type;
			typedef node_type* node_ptr;
			typedef std::atomic< node_ptr > node;
			
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
}
