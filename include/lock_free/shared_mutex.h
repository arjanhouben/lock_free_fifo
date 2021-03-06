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

			static const size_t locked = size_t( 1 ) << ( sizeof( size_t ) * 8 - 1 );

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
				while ( ++lock_required_ & locked )
				{
					--lock_required_;

					wait_for_non_exclusive();
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
				return ( lock_required_ & locked ) != 0;
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
					shared_guard& operator = ( const shared_guard& );
					shared_mutex &m_;
			};
		
			class exclusive_guard
			{
				public:
					exclusive_guard( shared_mutex &m ) :
						m_( m )
					{
						m_.lock();
						m_.wait_single_user();
					}
					
					~exclusive_guard()
					{
						m_.unlock();
					}
				private:
					exclusive_guard& operator = ( const exclusive_guard& );
					shared_mutex &m_;
			};

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

	private:

			std::atomic_size_t lock_required_;
	};
}
