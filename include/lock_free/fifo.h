#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <array>

namespace lock_free
{
	/**
	 * This is a lock free fifo, which can be used for multi-producer, multi-consumer
	 * type job queue
	 */
	template < typename Value, size_t Size = 1024 >
	class fifo
	{
		public:
			
			typedef Value value_type;
			typedef std::array< value_type, Size > storage_type;
			
			fifo() :
				storage_()
			{
				static_assert( Size > 0, "fifo has to be initialized with a size of at least 1" );
				storage_.store( { 0, 0, 0, new storage_type() } );
			}
		
			~fifo()
			{
				clear();
				delete storage_.load().lookup;
			}
			
			/**
			 * pushes an item into the job queue, may throw if allocation fails
			 * leaving the queue unchanged
			 */
			void push( const value_type &val )
			{
				if ( storage_.load().write_ == Size )
				{
					throw std::logic_error( "fifo buffer full" );
				}
				const size_t id = increase_write();
				(*storage_.load().lookup).at( id ) = val;
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
			 * clears the job queue, storing all pending jobs in the supplied container.
			 * the container is also returned for convenience
			 */
			template < typename T >
			T& pop_all( T &unfinished )
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
				storage expected = storage_;
				if ( expected.read_ != expected.write_ ||
					expected.read_ != expected.stored_ )
				{
					return;
				}
				
				storage desired = {
					0,
					0,
					0,
					expected.lookup
				};
				
				storage_.compare_exchange_strong( expected, desired );
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
		
			std::atomic< storage >	storage_;
	};
}
