#pragma once

/*
 * Arjan Houben - 2014
 */

#include <atomic>
#include <memory>
#include <vector>
#include <thread>
#include <algorithm>
#include <utility>

#include <lock_free/shared_mutex.h>

namespace lock_free
{
	/**
	 * This is a lock free fifo, which can be used for multi-producer, multi-consumer
	 * type job queue
	 */
	template < typename Value >
	class fifo
	{
		public:

			enum class value_state : uint8_t
			{
				uninitialized = 0,
				ready,
				done
			};

			typedef Value value_type;
			struct storage_type
			{
				value_type value;
				std::atomic< value_state > state;

				storage_type() :
					value(),
					state( value_state::uninitialized ) {}

				storage_type( storage_type &&s ) :
					value( std::move( s.value ) ),
					state( s.state.load() )
				{
				}

				storage_type& operator = ( const storage_type &rhs )
				{
					value = rhs.value;
					state.store( rhs.state.load() );
					return *this;
				}
			};


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
			template < typename T >
			void push_back( T &&value )
			{
				if ( write_ == std::numeric_limits< size_t >::max() )
				{
					throw std::logic_error( "fifo full, remove some jobs before adding new ones" );
				}

				const size_t id = write_++;

				try
				{
					if ( id >= size_ )
					{
						resize_storage( id );
					}

					shared_mutex::shared_guard lock( lock_ );

					storage_[ id ].value = std::forward< T >( value );

					storage_[ id ].state = value_state::ready;
				}
				catch( ... )
				{
					shared_mutex::shared_guard lock( lock_ );
					
					if ( id < size_ )
					{
						// if some exception occured, make sure we don't use this id
						storage_[ id ].state = value_state::done;
					}

					throw;
				}
			}

			/**
			 * retrieves an item from the job queue.
			 * if no item was available, item is untouched and pop returns false
			 */
			bool pop( value_type &value )
			{
				shared_mutex::shared_guard lock( lock_ );

				for ( size_t id = read_, max = std::min( write_, size_ ); id < max; ++id )
				{
					value_state current( value_state::ready );

					if ( storage_[ id ].state.compare_exchange_strong( current, value_state::done ) )
					{
						try
						{
							std::swap( value, storage_[ id ].value );
						}
						catch ( ... )
						{
							if ( id == read_ )
							{
								increase_read( id );
							}

							throw;
						}

						if ( id == read_ )
						{
							increase_read( id );
						}
						else
						{
							// give thread with oldest job time to catchup
							std::this_thread::yield();
						}

						return true;
					}
				}

				return false;
			}

			/**
			 * clears the job queue, storing all pending jobs in the supplied container.
			 * the container is also returned for convenience
			 */
			template < typename T >
			T &pop_all( T &unfinished )
			{
				for ( value_type tmp; pop( tmp ); )
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
				lock_.exclusive(
					[&]()
					{
						read_ = 0;
						write_ = 0;
					}
				);
			}

			/**
			 * returns true if there are no pending jobs
			 */
			bool empty() const
			{
				return read_ == write_;
			}

		private:

#if _MSC_VER
			fifo( const fifo & );
			fifo &operator = ( const fifo & );
#else
			fifo( const fifo & ) = delete;
			fifo &operator = ( const fifo & ) = delete;
#endif

			void reset_counters()
			{
				lock_.exclusive(
					[&]()
					{
						// check from within the mutex if another job was added since the last check
						if ( read_ != write_ )
						{
							return;
						}

						read_ = 0;
						write_ = 0;
					}
				);
			}

			void resize_storage( size_t id )
			{
				while ( size_ <= id )
				{
					if ( id == size_ )
					{
						lock_.exclusive(
							[&]()
							{
								const size_t newsize = std::max< size_t >( 1, size_ * 2 );

								storage_.resize( newsize );

								size_ = storage_.size();
							}
						);
					}
					else
					{
						std::this_thread::yield();
					}
				}
			}

			void increase_read( size_t id )
			{
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
