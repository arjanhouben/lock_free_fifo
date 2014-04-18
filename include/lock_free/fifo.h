#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <mutex>
#include <cassert>

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
	
	/**
	 * This is a lock free fifo, which can be used for multi-producer, multi-consumer
	 * type job queue
	 */
	template < typename Value >
	class fifo
	{
		public:
		
			typedef Value value_type;
			
			fifo( size_t size = 1024 ) :
				require_lock_( false ),
				lock_(),
				concurrent_users_( 0 ),
				read_( 0 ),
				write_( 0 ),
				size_( size ),
				lookup_( size ),
				bitflag_( new std::atomic_size_t[ std::max( size_t( 1 ), size / bits_per_section() ) ] )
			{
				fill_bitflags( 0 );
			}
		
			~fifo()
			{
				clear();
				delete [] bitflag_;
			}
			
			/**
			 * pushes an item into the job queue, may throw if allocation fails
			 * leaving the queue unchanged
			 */
			void push( const value_type &value )
			{
				std::lock_guard< use_count< std::atomic_size_t > > lock( concurrent_users_ );
				
				conditional_lock();
				
				const size_t id = write_++;
				if ( id >= size_ )
				{
					resize_storage( id );
				}
				
				assert( id < size_ );

				lookup_[ id ] = value;
				
				assert( lookup_[ id ] );
				
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
				std::lock_guard< use_count< std::atomic_size_t > > lock( concurrent_users_ );
				
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
				
				assert( lookup_[ id ] );
				
				assert( size_ > id );
				
				assign( value, lookup_[ id ] );
				
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
					std::lock_guard< std::mutex > guard( lock_ );
					
					while ( concurrent_users_() > 1 )
					{
						std::this_thread::yield();
					}
					
					write_ = 0;
					read_ = 0;
					fill_bitflags( 0 );
					
					lookup_.clear();
					
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
						
						std::lock_guard< std::mutex > guard( lock_ );
						
						while ( concurrent_users_() > 1 )
						{
							std::this_thread::yield();
						}
						
						const size_t bitflag_size = size_ / bits_per_section();
						
						lookup_.resize( std::max( size_t( 1 ), size_ * 2 ) );
						
						std::atomic_size_t *newbitflag = new std::atomic_size_t[ std::max( size_t( 1 ), bitflag_size * 2 ) ];
						std::atomic_size_t *start = newbitflag;
						const std::atomic_size_t *end = start + bitflag_size;
						const std::atomic_size_t *src = bitflag_;
						while ( start != end )
						{
							(start++)->store( *src++ );
						}
						end = newbitflag + bitflag_size * 2;
						while ( start != end )
						{
							(start++)->store( 0 );
						}
						delete [] bitflag_;
						bitflag_ = newbitflag;
						
						size_ = lookup_.size();
						
						require_lock_ = false;
					}
					else
					{
						conditional_lock();
					}
				}
			}
			
			static size_t mask_for_id( size_t id )
			{
				const size_t offset = id / bits_per_section();
				id -= offset * bits_per_section();
				return size_t( 1 ) << id;
			}
		
			void set_bitflag_( size_t id, size_t mask )
			{
//				static std::mutex das;
//				std::lock_guard< std::mutex > lok( das );

				bitflag_[ id / bits_per_section() ].fetch_or( mask );
			}
			
			bool unset_bitflag_( size_t id, size_t mask )
			{
//				static std::mutex das;
//				std::lock_guard< std::mutex > lok( das );
				
				const size_t old = bitflag_[ id / bits_per_section() ].fetch_and( ~mask );
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
		
			void fill_bitflags( size_t value )
			{
				std::atomic_size_t *start = bitflag_;
				const std::atomic_size_t *end = start + size_ / bits_per_section();
				while ( start != end )
				{
					(start++)->store( value );
				}
			}
		
			std::atomic_bool require_lock_;
			std::mutex lock_;
		
			use_count< std::atomic_size_t > concurrent_users_;
			std::atomic_size_t read_, write_, size_;
			std::vector< value_type > lookup_;
			std::atomic_size_t *bitflag_;
	};
}
