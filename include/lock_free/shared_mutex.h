#pragma once

/*
 * Arjan Houben - 2014
 */

#include <atomic>

namespace lock_free
{
	class shared_mutex
	{
		public:

			static constexpr size_t locked = size_t( 1 ) << ( sizeof( size_t ) * 8 - 1 );

			void lock()
			{
				while ( lock_required_.fetch_or( locked ) & locked )
				{
                    std::this_thread::yield();
				}

				wait_single_user();
			}

			void unlock()
			{
				lock_required_.fetch_and( ~locked );
			}

			void lock_shared()
			{
				if ( ++lock_required_ & locked )
				{
					--lock_required_;
					
					wait_for_non_exclusive();
					
					++lock_required_;
				}
			}

			inline void unlock_shared()
			{
				--lock_required_;
			}
		
			inline size_t use_count() const
			{
				return lock_required_ & ( ~locked );
			}
			
			inline bool exclusive_lock() const
			{
				return lock_required_ & locked;
			}
		
			template < typename F >
			void exclusive( F f )
			{
				std::lock_guard< shared_mutex > guard( *this );
				
				wait_single_user();
				
				f();
			}

			class shared_guard
			{
				public:
					shared_guard( shared_mutex &m ) :
						m_( m )
					{
						m_.lock_shared();
					}

					~shared_guard()
					{
						m_.unlock_shared();
					}
				private:
					shared_mutex &m_;
			};

		private:
		
			inline void wait_single_user() const
			{
				while ( use_count() )
				{
					std::this_thread::yield();
				}
			}
		
			inline void wait_for_non_exclusive() const
			{
				while ( lock_required_ & locked )
				{
					std::this_thread::yield();
				}
			}

			std::atomic_size_t lock_required_;
	};
}
