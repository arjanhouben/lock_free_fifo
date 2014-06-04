#pragma once

#include <atomic>
#include <mutex>

namespace lock_free
{
	class shared_mutex
	{
		public:

			static constexpr size_t locked = size_t( 1 ) << ( sizeof( size_t ) * 8 - 1 );

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

			std::atomic_size_t lock_required_;
			std::mutex mutex_;
	};
}
