#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>

namespace lock_free
{
	class shared_mutex
	{
		public:
		
			static const size_t locked = size_t( 1 ) << ( sizeof( size_t ) * 8 - 1 );
		
			void lock()
			{
				lock_required_.fetch_or( locked );
				
				mutex_.lock();
			}
			
			void unlock()
			{
				mutex_.unlock();
				
				lock_required_.fetch_and( ~locked );
			}
		
			void lock_shared()
			{
				if ( ++lock_required_ & locked )
				{
					--lock_required_;
					mutex_.lock();
					mutex_.unlock();
					++lock_required_;
				}
			}
		
			void unlock_shared()
			{
				--lock_required_;
			}
		
			size_t use_count() const
			{
				return lock_required_ & ( ~locked );
			}
		
		private:
		
			std::atomic_size_t lock_required_;
			std::mutex mutex_;
	};
	
	class shared_lock_guard
	{
		public:
			shared_lock_guard( shared_mutex &m ) :
				m_( m )
			{
				m_.lock_shared();
			}
			
			~shared_lock_guard()
			{
				m_.unlock_shared();
			}
		private:
			shared_mutex &m_;
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
	
	template < typename Source, typename Destination >
	void ignore( Destination, Source )
	{
	}
	
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
			typedef std::lock_guard< shared_mutex > mutex_guard;
			typedef std::unique_ptr< std::atomic_size_t[] > atomic_array;
			
			fifo( size_t size = 1024 ) :
				lock_(),
				read_( 0 ),
				write_( 0 ),
				size_( size ),
				cleanup_( 0 ),
				storage_( size ),
				bitflag_()
			{
				const size_t bitflag_size = std::max< size_t >( 1, offset_for_id( size ) );

				bitflag_.reset( new std::atomic_size_t[ bitflag_size ] );
			
				fill_atomic_array( bitflag_.get(), bitflag_.get() + bitflag_size, 0 );
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

				shared_lock_guard lock( lock_ );
				
				storage_[ id ] = value;
				
				set_bitflag( id, mask_for_id( id ) );
			}
		
			/**
			 * retrieves an item from the job queue.
			 * if no item was available, func is untouched and pop returns false
			 */
			bool pop( value_type &func )
			{
				return pop_generic( func, assign< value_type, value_type > );
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
				// todo
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
			bool pop_generic( value_type &value, Assign assign = ignore )
			{
				size_t id;
				
				// mutex scope
				{
					shared_lock_guard lock( lock_ );
					
					id = read_++;
					
					if ( id >= write_ )
					{
						--read_;
						
						return false;
					}
					
					const size_t mask = mask_for_id( id );
					while ( !unset_bitflag( id, mask ) )
					{
						lock_.unlock_shared();
						std::this_thread::yield();
						lock_.lock_shared();
					}
					
					assign( value, storage_[ id ] );
				}
				
				size_t clean_jobs = ++cleanup_;
				// this check is to prevent needless locking and is retested from within a lock
				if ( clean_jobs == write_ )
				{
					reset_counters( clean_jobs );
				}
			
				return true;
			}
		
			void reset_counters( size_t id )
			{
				/// lock
				mutex_guard guard( lock_ );
				
				// check from with the mutex if another job was added since the last check
				if ( id != write_ )
				{
					return;
				}
				
				// we want an exclusive lock, so wait until we are the only user
				while ( lock_.use_count() )
				{
					std::this_thread::yield();
				}
				
				read_ -= id;
				write_ -= id;
			}
	
			void resize_storage( size_t id )
			{
				while ( size_ <= id )
				{
					if ( id == size_ )
					{
						const size_t newsize = std::max< size_t >( 1, size_ * 2 );
						
						const size_t bitflag_size = std::max< size_t >( 1, offset_for_id( size_ ) );
						
						const size_t new_bitflag_size = std::max< size_t >( 1, offset_for_id( newsize ) );
						
						atomic_array newbitflag( new std::atomic_size_t[ new_bitflag_size ] );
						
						fill_atomic_array( newbitflag.get() + bitflag_size, newbitflag.get() + new_bitflag_size, 0 );
						
						/// lock
						mutex_guard guard( lock_ );
						
						// we want an exclusive lock, so wait until we are the only user
						while ( lock_.use_count() )
						{
							std::this_thread::yield();
						}
						
						storage_.resize( newsize );
						
						copy_atomic_array( bitflag_.get(), bitflag_.get() + bitflag_size, newbitflag.get() );
						
						bitflag_.swap( newbitflag );
						
						size_ = storage_.size();
					}
					else
					{
						std::this_thread::yield();
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
		
			void set_bitflag( size_t id, size_t mask )
			{
				bitflag_[ offset_for_id( id ) ].fetch_or( mask );
			}
			
			bool unset_bitflag( size_t id, size_t mask )
			{
				const size_t old = bitflag_[ offset_for_id( id ) ].fetch_and( ~mask );
				return ( old & mask ) == mask;
			}
		
			bool job_available_before( size_t id )
			{
				size_t count = std::max< size_t >( 1, offset_for_id( id ) ) - 1;
				for ( size_t i = 0; i < count; ++i )
				{
					if ( bitflag_[ i ] )
					{
						return true;
					}
				}
				return bitflag_[ count ] & mask_for_id( id );
			}
		
			shared_mutex lock_;
			
			std::atomic_size_t read_, write_, size_, cleanup_;
			std::vector< value_type > storage_;
			atomic_array bitflag_;
	};
}
