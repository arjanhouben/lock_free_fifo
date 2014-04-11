#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <map>

namespace lock_free
{
	/**
	 * This is a lock free fifo, which can be used for multi-producer, multi-consumer
	 * type job queue
	 */
	
	template < typename Value >
	struct fifo_node_type
	{
//		fifo_node_type( const Value &original ) :
//			value( new Value( original ) ),
//			next( nullptr ) { }
		
		Value *value;
		fifo_node_type *next;
	};
	
	template < typename Value, typename Allocator = std::allocator< fifo_node_type< Value > > >
	class fifo
	{
		public:
			
			typedef Value value_type;
			typedef Allocator allocator_type;
			typedef std::vector< value_type, allocator_type > vector_type;
			typedef std::map< size_t, value_type > storage_type;
			
			fifo() :
				read_( 0 ),
				write_( 0 ),
				stored_( 0 ),
		storage_()
		{
			storage_.store( { false, new storage_type() } );
		}
		
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
				hold();
				size_t id = write_++;
				(*storage_.load().lookup)[ id ] = val;
				++stored_;
				release();
			}
		
			/**
			 * retrieves an item from the job queue.
			 * if no item was available, func is untouched and pop returns false
			 */
			bool pop( value_type &func )
			{
				auto assign = [ & ]( node_ptr ptr, value_type &value)
				{
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
				auto del = [ & ]( node_ptr ptr, value_type& )
				{
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
				return false;
			}
			
		private:
			
			typedef fifo_node_type< value_type > node_type;
			typedef node_type* node_ptr;
			typedef std::atomic< node_ptr > node;
		
			fifo( const fifo& );
			fifo& operator = ( const fifo& );
			
			template < typename Assign >
			bool pop_generic( value_type &value, Assign assign )
			{
				hold();
				
				const size_t id = read_++;
				
				if ( id >= stored_ )
				{
					--read_;
					
					release();

					try_cleanup();
					
					return false;
				}
				
				value = (*storage_.load().lookup)[ id ];
				
				release();
				
				return true;
			}
		
			void try_cleanup()
			{
				storage tmp = { false, storage_.load().lookup };
				storage clean = { false, new storage_type() };
				if ( storage_.compare_exchange_strong( tmp, clean ) )
				{
					delete tmp.lookup;
				}
				else
				{
					delete clean.lookup;
				}
			}
		
			void hold()
			{
				storage_.exchange( { true, storage_.load().lookup } );
			}
			
			void release()
			{
				storage_.exchange( { false, storage_.load().lookup } );
			}
	
			std::atomic_size_t read_, write_, stored_;
		
			struct storage
			{
				bool inuse_;
				storage_type *lookup;
			};
		
		std::atomic< storage >	storage_;
	};
}
