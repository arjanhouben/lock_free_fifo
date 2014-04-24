#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>

namespace lock_free
{
	/**
	 * this class is used so we're able to use the RAII mechanism for locking
	 */
	template < typename T >
	class use_count
	{
		public:
			
			template < typename V >
			use_count( V &&v ) :
			data_( std::forward< V >( v ) ) { }
			
			const T& operator()() const { return data_; }
			
			void lock() { ++data_; }
			
			void unlock() { --data_; }
			
		private:
			
			use_count( const use_count& );
			
			use_count& operator = ( const use_count& );
			
			T data_;
	};
	
	void fill_atomic_array( std::atomic_size_t *start, std::atomic_size_t *end, size_t value )
	{
		while ( start != end )
		{
			(start++)->store( value );
		}
	}
	
	void copy_atomic_array( const std::atomic_size_t *start, const std::atomic_size_t *end, std::atomic_size_t *dst )
	{
		while ( start != end )
		{
			(dst++)->store( *start++ );
		}
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
			typedef std::lock_guard< use_count< std::atomic_size_t > > use_guard;
			typedef std::lock_guard< std::mutex > mutex_guard;
			typedef std::unique_ptr< std::atomic_size_t[] > atomic_array;
			
			fifo( size_t size = 1024 ) :
				require_lock_( false ),
				lock_(),
				concurrent_users_( 0 ),
				read_( 0 ),
				write_( 0 ),
				size_( size ),
				storage_( size ),
				bitflag_()
			{
				const size_t bitflag_size = std::max( size_t( 1 ), size / bits_per_section() );
			
				bitflag_ = atomic_array( new std::atomic_size_t[ bitflag_size ] );
			
				fill_atomic_array( bitflag_.get(), bitflag_.get() + bitflag_size, 0 );
			}
			
			/**
			 * pushes an item into the job queue, may throw if allocation fails
			 * leaving the queue unchanged
			 */
			void push( const value_type &value )
			{
				use_guard lock( concurrent_users_ );
				
				conditional_lock();
				
				if ( write_ == std::numeric_limits< size_t >::max() )
				{
					throw std::logic_error( "fifo full, remove some jobs before adding new ones" );
				}
				
				const size_t id = write_++;
				if ( id >= size_ )
				{
					resize_storage( id );
				}

				storage_[ id ] = value;
				
				set_bitflag_( id, mask_for_id( id ) );
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
				return read_ == write_;
			}
			
		private:
			
			fifo( const fifo& );
			fifo& operator = ( const fifo& );
		
			static constexpr size_t bits_per_section()
			{
				return sizeof( size_t ) * 8;
			}
			
			template < typename Assign >
			bool pop_generic( value_type &value, Assign assign )
			{
				use_guard lock( concurrent_users_ );
				
				conditional_lock();
				
				const size_t id = read_++;
				
				if ( id >= write_ )
				{
					--read_;

					try_cleanup();
					
					return false;
				}

				const size_t mask = mask_for_id( id );
				while ( !unset_bitflag_( id, mask ) )
				{
					std::this_thread::yield();
				}
				
				assign( value, storage_[ id ] );
				
				return true;
			}
		
			void try_cleanup()
			{
				if ( !write_ || read_ != write_ || require_lock_ )
				{
					// early exit, avoids needless locking
					return;
				}
				
				bool expected( false );
				if ( require_lock_.compare_exchange_strong( expected, true ) )
				{
					mutex_guard guard( lock_ );
					
					while ( concurrent_users_() > 1 )
					{
						std::this_thread::yield();
					}
					
					write_ = 0;
					read_ = 0;
					fill_atomic_array( bitflag_.get(), bitflag_.get() + size_ / bits_per_section(), 0 );
					
					require_lock_ = false;
				}
			}
	
			void resize_storage( size_t id )
			{
				while ( size_ <= id )
				{
					if ( id == size_ )
					{
						require_lock_ = true;
						
						mutex_guard guard( lock_ );
						
						while ( concurrent_users_() > 1 )
						{
							std::this_thread::yield();
						}
						
						const size_t bitflag_size = size_ / bits_per_section();
						
						storage_.resize( std::max( size_t( 1 ), size_ * 2 ) );
						
						atomic_array newbitflag( new std::atomic_size_t[ std::max( size_t( 1 ), bitflag_size * 2 ) ] );
						
						copy_atomic_array( bitflag_.get(), bitflag_.get() + bitflag_size, newbitflag.get() );
						
						fill_atomic_array( newbitflag.get() + bitflag_size, newbitflag.get() + bitflag_size * 2, 0 );
						
						std::swap( bitflag_, newbitflag );
						
						size_ = storage_.size();
						
						require_lock_ = false;
					}
					else
					{
						conditional_lock();
					}
				}
			}
		
			static size_t offset_for_id( size_t id )
			{
				return id / bits_per_section();
			}
			
			static size_t mask_for_id( size_t id )
			{
				id -= offset_for_id( id ) * bits_per_section();
				return size_t( 1 ) << id;
			}
		
			void set_bitflag_( size_t id, size_t mask )
			{
				bitflag_[ offset_for_id( id ) ].fetch_or( mask );
			}
			
			bool unset_bitflag_( size_t id, size_t mask )
			{
				const size_t old = bitflag_[ offset_for_id( id ) ].fetch_and( ~mask );
				return ( old & mask ) == mask;
			}
			
			void conditional_lock()
			{
				if ( require_lock_ )
				{
					concurrent_users_.unlock();
					lock_.lock();
					lock_.unlock();
					concurrent_users_.lock();
				}
			}
		
			std::atomic_bool require_lock_;
			std::mutex lock_;
		
			use_count< std::atomic_size_t > concurrent_users_;
			std::atomic_size_t read_, write_, size_;
			std::vector< value_type > storage_;
			atomic_array bitflag_;
	};
}
