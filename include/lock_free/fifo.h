#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>

#include <cassert>

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

    enum value_state
    {
        uninitialized = 0,
        ready = 1,
        done = 2
    };

    template < typename T >
    struct statefull_value
    {
        statefull_value( const T &t = T() ) :
            value( t ),
            state( uninitialized )
        {
        }

        statefull_value( const statefull_value< T > &rhs ) :
            value( rhs.value ),
            state( rhs.state.load() )
        {
        }

        statefull_value& operator = ( const statefull_value< T > &rhs )
        {
            value = rhs.value;
            state.store( rhs.state );
            return *this;
        }

        T value;
        std::atomic< value_state > state;
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
            typedef statefull_value< value_type > storage_type;
            typedef std::lock_guard< shared_mutex > mutex_guard;
            typedef std::unique_ptr< std::atomic_size_t[] > atomic_array;

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

                shared_lock_guard lock( lock_ );
                assert( id < storage_.size() );
                storage_[ id ].value = value;
                assert( storage_[ id ].state == uninitialized );
                storage_[ id ].state = ready;
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

            static size_t bits_per_section()
            {
                return sizeof( size_t ) * 8;
            }

            template < typename Assign >
            bool pop_generic( value_type &value, Assign assign = ignore )
            {
                shared_lock_guard lock( lock_ );

                size_t m = std::min( write_, size_ );
                for ( size_t id = read_; id < m; ++id )
                {
                    value_state current( ready );
                    if ( storage_[ id ].state.compare_exchange_strong( current, done ) )
                    {
                        assign( value, storage_[ id ].value );
                        increase_read( id );
                        return true;
                    }
                }

                return false;
            }

            void reset_counters( size_t id )
            {
                /// lock
                mutex_guard guard( lock_ );

                // we want an exclusive lock, so wait until we are the only user
                while ( lock_.use_count() )
                {
                    std::this_thread::yield();
                }

                // check from with the mutex if another job was added since the last check
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

                value_state expected( done );
                while ( id < size_ && storage_[ id++ ].state.compare_exchange_strong( expected, uninitialized ) )
                {
                    ++read_;
                }

                if ( read_ == write_ )
                {
                    lock_.unlock_shared();
                    reset_counters( read_ );
                    lock_.lock_shared();
                }
            }

            shared_mutex lock_;
            std::atomic_size_t read_, write_, size_;
            std::vector< storage_type > storage_;
    };
}
