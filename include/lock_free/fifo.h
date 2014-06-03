#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <thread>
#include <algorithm>

#include <lock_free/statefull_value.h>
#include <lock_free/shared_mutex.h>

namespace lock_free
{
	template < typename Source, typename Destination >
	void assign( Destination &dst, const Source &src )
	{
		dst = src;
	}

	template < typename Source, typename Destination >
	void swap( Destination &dst, Source &src )
	{
		std::swap( src, dst );
	}

	/**
	 * This is a lock free fifo, which can be used for multi-producer, multi-consumer
	 * type job queue
	 */
	template < typename Value >
	class fifo
	{
		public:

			typedef Value value_type;
			typedef statefull_value< value_type > storage_type;
			typedef std::lock_guard< shared_mutex > mutex_guard;

			fifo( size_t size = 1024 ) :
				lock_(),
				read_( 0 ),
				write_( 0 ),
				size_( size ),
				storage_( size )
			{
			}

			/**
			 * pushes an item into the job queue, may throw if allocation fails
			 * leaving the queue unchanged
			 */
			void push( const value_type &value )
			{
				if ( write_ == std::numeric_limits< size_t >::max() )
				{
					throw std::logic_error( "fifo full, remove some jobs before adding new ones" );
				}

				const size_t id = write_++;

				if ( id >= size_ )
				{
					resize_storage( id );
				}

				shared_mutex::shared_guard lock( lock_ );
				
				storage_[ id ].value = value;
				
				storage_[ id ].state = value_state::ready;
			}

			/**
			 * retrieves an item from the job queue.
			 * if no item was available, func is untouched and pop returns false
			 */
			bool pop( value_type &func )
			{
				return pop_generic( func, swap< value_type, value_type > );
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
					unfinished.push_back( std::move( tmp ) );
				}
				return unfinished;
			}

			/**
			 * clears the job queue.
			 */
			void clear()
			{
				mutex_guard guard( lock_ );
				
				// we want an exclusive lock, so wait until we are the only user
				while ( lock_.use_count() )
				{
					std::this_thread::yield();
				}
				
				read_ = 0;
				write_ = 0;
			}

			/**
			 * returns true if there are no pending jobs
			 */
			bool empty() const
			{
				return read_ == write_;
			}

		private:

			fifo( const fifo& );
			fifo& operator = ( const fifo& );

#if _MSC_VER
			static size_t bits_per_section()
#else
			static constexpr size_t bits_per_section()
#endif
			{
				return sizeof( size_t ) * 8;
			}

			template < typename Assign >
			bool pop_generic( value_type &value, Assign assign )
			{
				shared_mutex::shared_guard lock( lock_ );

				size_t m = std::min( write_, size_ );
				for ( size_t id = read_; id < m; ++id )
				{
					value_state current( value_state::ready );
					if ( storage_[ id ].state.compare_exchange_strong( current, value_state::done ) )
					{
						try
						{
							assign( value, storage_[ id ].value );
						}
						catch ( ... )
						{
							storage_[ id ].state.store( current );
							
							throw;
						}
						
						increase_read( id );
						
						return true;
					}
				}

				return false;
			}

			void reset_counters()
			{
				/// lock
				mutex_guard guard( lock_ );

				// we want an exclusive lock, so wait until we are the only user
				while ( lock_.use_count() )
				{
					std::this_thread::yield();
				}

				// check from within the mutex if another job was added since the last check
				if ( read_ != write_ )
				{
					return;
				}

				read_ = 0;
				write_ = 0;
			}

			void resize_storage( size_t id )
			{
				while ( size_ <= id )
				{
					if ( id == size_ )
					{
						const size_t newsize = std::max< size_t >( 1, size_ * 2 );

						/// lock
						mutex_guard guard( lock_ );

						// we want an exclusive lock, so wait until we are the only user
						while ( lock_.use_count() )
						{
							std::this_thread::yield();
						}

						storage_.resize( newsize );

						size_ = storage_.size();
					}
					else
					{
						std::this_thread::yield();
					}
				}
			}

			void increase_read( size_t id )
			{
				if ( id != read_ ) return;

				value_state expected( value_state::done );
				while ( id < size_ && storage_[ id++ ].state.compare_exchange_strong( expected, value_state::uninitialized ) )
				{
					++read_;
				}

				if ( read_ == write_ )
				{
					lock_.unlock_shared();
					
					reset_counters();
					
					lock_.lock_shared();
				}
			}
		
			shared_mutex lock_;
			std::atomic_size_t read_, write_, size_;
			std::vector< storage_type > storage_;
	};
}
