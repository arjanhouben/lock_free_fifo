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
			void push( T &&val )
			{
				node_ptr newnode( new node_type( std::forward< T >( val ) ) );
				
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
					std::swap( value, ptr->value );
					delete ptr;
				};
				return pop_generic( func, assign );
			}
		
			/**
			 * clears the job queue, storing all pending jobs in the supplied vector.
			 * the vector is also returned for convenience
			 */
			vector_type& pop_all( vector_type &unfinished )
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
			
			/**
			 * returns true if there are no pending jobs
			 */
			bool empty() const
			{
				return start_ == nullptr;
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
					value( original ),
					next( nullptr ) { }
				
				value_type value;
				node_ptr next;
			};
			
			node start_, end_;
	};
}
