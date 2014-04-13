#pragma once

#include <atomic>
#include <memory>
#include <vector>

namespace lock_free
{
	/**
	 * This is a lock free fifo, which can be used for multi-producer, multi-consumer
	 * type job queue
	 */
	template < typename Value, size_t InitialSize = 1024, typename Allocator = std::allocator< Value > >
	class fifo
	{
		public:
			
			typedef Value value_type;
			typedef Allocator allocator_type;
			typedef std::vector< value_type, allocator_type > storage_type;
			
			fifo() :
		storage_()
		{
			static_assert( InitialSize > 0, "fifo has to be initialized with a size of at least 1" );
			storage_.store( { 0, 0, 0, new storage_type( InitialSize ) } );
		}
		
			~fifo()
			{
				clear();
			}
			
			/**
			 * pushes an item into the job queue, may throw if allocation fails
			 * leaving the queue unchanged
			 */
			void push( const value_type &val )
			{
				const size_t id = increase_write();
				size_t storage_size = storage_.load().lookup->size();
				while ( id >= storage_size )
				{
					storage_size = resize_storage( storage_size * 2 );
				}
				(*storage_.load().lookup)[ id ] = val;
				increase_stored();
			}
		
			/**
			 * retrieves an item from the job queue.
			 * if no item was available, func is untouched and pop returns false
			 */
			bool pop( value_type &func )
			{
				auto assign = [ & ]( value_type &dst, value_type &src )
				{
					std::swap( dst, src );
				};
				return pop_generic( func, assign );
			}
		
			/**
			 * clears the job queue, storing all pending jobs in the supplied vector.
			 * the vector is also returned for convenience
			 */
			storage_type& pop_all( storage_type &unfinished )
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
				auto del = []( value_type&, value_type& ) {};
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
				storage s( storage_ );
				return s.read_ == s.stored_;
			}
			
		private:
			
			fifo( const fifo& );
			fifo& operator = ( const fifo& );
			
			template < typename Assign >
			bool pop_generic( value_type &value, Assign assign )
			{
				const size_t id = increase_read();
				
				if ( id >= storage_.load().stored_ )
				{
					decrease_read();

					try_cleanup();
					
					return false;
				}
				
				assign( value, (*storage_.load().lookup)[ id ] );
				
				return true;
			}
		
			void try_cleanup()
			{
				// not sure how to implement this safely..
#if 0
				storage expected = storage_;
				if ( expected.lookup->empty() ||
					expected.read_ != expected.write_ ||
					expected.read_ != expected.stored_ )
				{
					return;
				}
				
				storage desired = {
					0,
					0,
					0,
					new storage_type()
				};
				
				if ( !storage_.compare_exchange_strong( expected, desired ) )
				{
					delete expected.lookup;
				}
#endif
			}
		
			struct storage
			{
				size_t read_, write_, stored_;
				storage_type *lookup;
			};
			
			size_t increase_read()
			{
				auto inc_read = []( storage &value )
				{
					++value.read_;
				};
				
				auto ret_read = []( storage &value )
				{
					return value.read_;
				};
				
				return change_storage< size_t >( inc_read, ret_read );
			}
			
			size_t decrease_read()
			{
				auto inc_read = []( storage &value )
				{
					--value.read_;
				};
				
				auto ret_read = []( storage &value )
				{
					return value.read_;
				};
				
				return change_storage< size_t >( inc_read, ret_read );
			}
			
			size_t increase_write()
			{
				auto inc = []( storage &value )
				{
					++value.write_;
				};
				
				auto ret = []( storage &value )
				{
					return value.write_;
				};
				
				return change_storage< size_t >( inc, ret );
			}
			
			void increase_stored()
			{
				auto inc = []( storage &value )
				{
					++value.stored_;
				};
				
				auto ret = []( storage &value )
				{
					return value.stored_;
				};
				
				change_storage< size_t >( inc, ret );
			}
			
			size_t resize_storage( size_t new_size )
			{
				while ( lock_.test_and_set( std::memory_order_acquire ) )
				{
					// empty
				}
				
				storage expected = storage_;
				
				if ( expected.lookup->size() < new_size )
				{
					storage desired = expected;
					desired.lookup = new storage_type( *desired.lookup );
					desired.lookup->resize( new_size );
					
					while ( expected.lookup->size() < new_size )
					{
						if ( storage_.compare_exchange_weak( expected, desired ) )
						{
							lock_.clear( std::memory_order_release );
							delete expected.lookup;
							return new_size;
						}
					}
					
					delete desired.lookup;
				}
				
				lock_.clear( std::memory_order_release );
				
				return expected.lookup->size();
			}
			
			template < typename R, typename Adjust, typename Return >
			R change_storage( Adjust a, Return r )
			{
				for ( ;; )
				{
					storage expected = storage_;
					storage desired = expected;
					a( desired );
					if ( storage_.compare_exchange_weak( expected, desired ) )
					{
						return r( expected );
					}
				}
			}
			
			std::atomic_flag lock_;
			std::atomic< storage >	storage_;
	};
}
