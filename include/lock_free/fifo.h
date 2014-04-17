#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <array>

#include <cassert>

template < typename T >
void printbits( T t )
{
	size_t kak( sizeof( T ) * 8 );
	std::cout << "0xb";
	for ( int i = 0; i < kak; ++i )
	{
		std::cout << ( ( t & ( size_t( 1 ) << ( kak - 1 - i ) ) ) ? '1' : '0' );
	}
	std::cout << std::endl;
}

namespace lock_free
{
	template < typename source, typename destination >
	void assign( source &&src, destination &&dst )
	{
		dst = src;
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
			
			fifo( size_t size = 1024 ) :
				require_lock_( 0 ),
				lock_(),
				read_( 0 ),
				write_( 0 ),
				concurrent_users_( 0 ),
				size_( size ),
				lookup( 0 ),
				bitflag( 0 )
			{
				// fix this so it doesn't leak when throw
				lookup = new value_type[ size ];
				bitflag = new std::atomic_size_t[ size / bits_per_section() ];
				std::fill( bitflag, bitflag + size / bits_per_section(), 0 );
			}
		
			~fifo()
			{
				clear();
				delete [] lookup;
				delete [] bitflag;
			}
			
			/**
			 * pushes an item into the job queue, may throw if allocation fails
			 * leaving the queue unchanged
			 */
			void push( const value_type &value )
			{
				conditional_lock();
			
				++concurrent_users_;
				const size_t id = write_++;
				if ( id >= size_ )
				{
					resize_storage( id );
				}
				
				assert( id < size_ );

				lookup[ id ] = value;
				
				set_bitflag( id );
				
				--concurrent_users_;
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
				conditional_lock();
				
//				static std::mutex koel;
//				std::lock_guard< std::mutex > bert( koel );
			
				++concurrent_users_;
				
				const size_t id = read_++;
//				static std::atomic_size_t kak( -1 );
//				assert( kak != id );
//				kak = id;
//				std::cout << id << '\t' << std::this_thread::get_id() << '\n';
				
				if ( id >= write_ )
				{
					--read_;

//					try_cleanup();
					
					--concurrent_users_;
					
					return false;
				}
				
				while ( !unset_bitflag( id ) )
				{
					std::this_thread::yield();
				}
				
				assert( size_ > id );
				
				assign( value, lookup[ id ] );
				
				assert( value );
				
				--concurrent_users_;
				
				return true;
			}
		
			void try_cleanup()
			{
#if 0
				storage expected = storage_;
				if ( expected.read_ != expected.write_ ||
					expected.read_ != expected.stored_ )
				{
					return;
				}
				
				expected.inuse = 1;
				
				storage desired = {
					0,
					0,
					0,
					expected.size,
					0,
					expected.lookup
				};
				
				while ( !storage_.compare_exchange_weak( expected, desired ) )
				{
					expected.inuse = 1;
				}
#endif
			}
	
			void resize_storage( size_t id )
			{
				if ( id == size_ )
				{
					require_lock_ = true;
					std::lock_guard< std::mutex > guard( lock_ );
					
					while ( concurrent_users_ > 1 )
					{
						std::this_thread::yield();
					}
					// prevent memory leak on throw
					const size_t newsize = size_ * 2;
					value_type *newlookup = new value_type[ newsize ];
					std::atomic_size_t *newbitflag = new std::atomic_size_t[ newsize / bits_per_section() ];
					std::copy( lookup, lookup + size_, newlookup );
					for ( size_t i = 0; i < ( size_ / bits_per_section() ); ++i )
					{
						newbitflag[ i ].store( bitflag[ i ] );
					}
//					std::copy( expected.bitflag, expected.bitflag + ( expected.size / bits_per_section() ), bitflag );
//					std::fill( bitflag + ( expected.size / bits_per_section() ), bitflag + newsize / bits_per_section(), 0 );
					
					delete [] lookup;
					delete [] bitflag;
					lookup = newlookup;
					bitflag = newbitflag;
					size_ = newsize;
					
					require_lock_ = false;
				}
				else
				{
					--concurrent_users_;
					while ( size_ <= id )
					{
						std::this_thread::yield();
					}
					++concurrent_users_;
				}
			}
			
			static size_t mask_for_id( size_t id )
			{
				const size_t offset = id / bits_per_section();
				id -= offset * bits_per_section();
				return size_t( 1 ) << id;
			}
		
			void set_bitflag( size_t id )
			{
				std::atomic_fetch_or( bitflag + id / bits_per_section(), mask_for_id( id ) );
				
			}
			
			bool unset_bitflag( size_t id )
			{
				const size_t mask = mask_for_id( id );
				const size_t old = std::atomic_fetch_and( bitflag + id / bits_per_section(), ~mask );
				return old & mask;
			}
			
			void conditional_lock()
			{
				if ( require_lock_ )
				{
					lock_.lock();
					lock_.unlock();
				}
			}
		
			std::atomic_bool require_lock_;
			std::mutex lock_;
		
		
			std::atomic_size_t read_, write_, concurrent_users_;
		
			std::atomic_size_t size_;
			value_type *lookup;
			std::atomic_size_t *bitflag;
	};
}
