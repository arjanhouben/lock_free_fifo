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
				storage_()
			{
				// fix this so it doesn't leak when throw
				value_type *lookup = new value_type[ size ];
				std::atomic_size_t *bitflag = new std::atomic_size_t[ size / bits_per_section() ];
				std::fill( bitflag, bitflag + size / bits_per_section(), 0 );
				storage_.store( { 0, 0, 0, size, 0, lookup, bitflag } );
			}
		
			~fifo()
			{
				clear();
				delete [] storage_.load().lookup;
				delete [] storage_.load().bitflag;
			}
			
			/**
			 * pushes an item into the job queue, may throw if allocation fails
			 * leaving the queue unchanged
			 */
			void push( const value_type &val )
			{
				claim_use();
				const size_t id = increase_write();
				storage tmp( storage_ );
				if ( id >= tmp.size )
				{
					tmp = resize_storage( id );
				}
				assert( id < tmp.size );
				tmp.lookup[ id ] = val;
				increase_stored();
				set_bitflag( id );
				release_use();
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
		
		static constexpr size_t bits_per_section()
		{
			return sizeof( size_t ) * 8;
		}
			
			template < typename Assign >
			bool pop_generic( value_type &value, Assign assign )
			{
				claim_use();
				
				const size_t id = increase_read();
				
				storage tmp( storage_ );
				
				if ( id >= tmp.write_ )
				{
					decrease_read();

//					try_cleanup();
					
					release_use();
					
					return false;
				}
				
				assert( tmp.size > id );
				
				while ( !unset_bitflag( id ) )
				{
					std::this_thread::yield();
				}
				
				assign( value, tmp.lookup[ id ] );
				
				assert( value );
				
				release_use();
				
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
			}
		
			struct storage
			{
				size_t read_, write_, stored_, size, inuse;
				value_type *lookup;
				std::atomic_size_t *bitflag;
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
		
			storage resize_storage( size_t id )
			{
				storage expected( storage_ );
				if ( id == expected.size )
				{
					require_lock_ = true;
					std::lock_guard< std::mutex > guard( lock_ );
					
					while ( expected.inuse > 1 )
					{
						std::this_thread::yield();
						expected = storage_;
					}
					// prevent memory leak on throw
					const size_t newsize = expected.size * 2;
					value_type *lookup = new value_type[ newsize ];
					std::atomic_size_t *bitflag = new std::atomic_size_t[ newsize / bits_per_section() ];
					std::copy( expected.lookup, expected.lookup + expected.size, lookup );
					for ( size_t i = 0; i < ( expected.size / bits_per_section() ); ++i )
					{
						bitflag[ i ].store( expected.bitflag[ i ] );
					}
//					std::copy( expected.bitflag, expected.bitflag + ( expected.size / bits_per_section() ), bitflag );
//					std::fill( bitflag + ( expected.size / bits_per_section() ), bitflag + newsize / bits_per_section(), 0 );
					
					storage desired;
//					do
					{
						expected.inuse = 1;
						desired = expected;
						desired.lookup = lookup;
						desired.bitflag = bitflag;
						desired.size = newsize;
					}
					if ( !storage_.compare_exchange_strong( expected, desired ) )
					{
						assert( false );
					}
					delete [] expected.lookup;
					delete [] expected.bitflag;
					require_lock_ = false;
				}
				else
				{
					release_use();
					do
					{
						std::this_thread::yield();
						expected = storage_;
					}
					while ( expected.size <= id );
					claim_use();
				}
				return storage_;
			}
		
			void claim_use()
			{
				if ( require_lock_ )
				{
					lock_.lock();
					lock_.unlock();
				}
				
				auto inc = []( storage &value )
				{
					++value.inuse;
				};
				
				auto ret = []( storage &value )
				{
					return value.inuse;
				};
				
				change_storage< size_t >( inc, ret );
			}
			
			void release_use()
			{
				auto inc = []( storage &value )
				{
					--value.inuse;
				};
				
				auto ret = []( storage &value )
				{
					return value.inuse;
				};
				
				change_storage< size_t >( inc, ret );
			}
		
		void set_bitflag( size_t id )
		{
			const size_t offset = id / bits_per_section();
			id -= offset * bits_per_section();
			const size_t mask = size_t( 1 ) << id;

			auto inc = [&]( storage &value )
			{
				std::atomic_fetch_or( value.bitflag + offset, mask );
			};
			
			auto ret = [&]( storage &value )
			{
			};
			
			change_storage< void >( inc, ret );
			
		}
		
		bool unset_bitflag( size_t id )
		{
			const size_t offset = id / bits_per_section();
			id -= offset * bits_per_section();
			const size_t mask = size_t( 1 ) << id;
			
			bool result = false;
			
			auto inc = [&]( storage &value )
			{
				size_t old = std::atomic_fetch_and( &value.bitflag[ offset ], ~mask );
				result = old & mask;
			};
			
			auto ret = [&]( storage &value )
			{
				return result;
			};
			
			return change_storage< bool >( inc, ret );
		}
			
			template < typename R, typename Adjust, typename Return >
			R change_storage( Adjust a, Return r )
			{
				storage expected = storage_;
				for ( ;; )
				{
					storage desired = expected;
					a( desired );
					if ( storage_.compare_exchange_weak( expected, desired ) )
					{
						return r( expected );
					}
				}
			}
		
			std::atomic_bool require_lock_;
			std::mutex lock_;
			std::atomic< storage >	storage_;
	};
}
