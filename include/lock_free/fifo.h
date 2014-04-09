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
			typedef std::vector< value_type, allocator_type > vector_type;
			
			fifo() :
				start_(),
				end_() {}
		
			~fifo()
			{
				clear();
			}
			
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
				auto assign = []( node_ptr ptr, value_type &value)
				{
					std::swap( value, ptr->func );
					delete ptr;
				};
				return pop_generic( func, assign );
			}
		
			/**
			 * clears the job queue.
			 * if there where items left unhandled, they are placed inside the unfinished argument
			 * which is also returned for convenience
			 */
			vector_type& clear( vector_type &unfinished )
			{
				value_type tmp;
				while ( pop( tmp ) )
				{
					unfinished.push_back( tmp );
				}
				return unfinished;
			}
		
			/**
			 * clears the job queue.
			 */
			void clear()
			{
				auto del = []( node_ptr ptr, value_type& )
				{
					delete ptr;
				};
				value_type tmp;
				while ( pop_generic( tmp, del ) )
				{
					// empty
				}
			}
			
		private:
			
			struct node_type;
			typedef node_type* node_ptr;
			typedef std::atomic< node_ptr > node;
		
			fifo( const fifo& );
			fifo& operator = ( const fifo& );
			
			template < typename Assign >
			bool pop_generic( value_type &func, Assign assign )
			{
				node_ptr tmp = start_;
				
				while ( tmp )
				{
					if ( start_.compare_exchange_weak( tmp, tmp->next ) )
					{
						assign( tmp, func );
						
						return true;
					}
					// if we got here, tmp was set to the value of start_, so we try again
				}
				
				return false;
			}
			
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
