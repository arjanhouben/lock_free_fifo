#pragma once

#include <array>
#include <vector>
#include <atomic>

#include <list>

#include <lock_free/statefull_value.h>

namespace lock_free
{
	template < typename Value, size_t ConcurrentUsers = 255 >
	class multibin
	{
		public:
		
			typedef Value value_type;
			typedef statefull_value< std::list< value_type > > storage_type;
			
			multibin( size_t reserve = 1024 ) :
				jobs_( 0 )
			{
				
			}
			
			void push( const value_type &value )
			{
				for ( auto &bin : bins_ )
				{
					value_state empty( value_state::uninitialized );
					
					if ( bin.state.compare_exchange_strong( empty, value_state::in_use ) )
					{
						bin.value.push_back( value );
						
						bin.state = value_state::uninitialized;
						
						++jobs_;
						
						return;
					}
				}
				
				throw std::logic_error( "maximum concurrent users reached" );
			}
			
			bool pop( value_type &value )
			{
				while ( jobs_ )
				{
					for ( auto &bin : bins_ )
					{
						if ( bin.value.empty() )
						{
							continue;
						}
						
						value_state empty( value_state::uninitialized );
						
						if ( bin.state.compare_exchange_strong( empty, value_state::in_use ) )
						{
							if ( bin.value.empty() )
							{
								bin.state = value_state::uninitialized;
								
								continue;
							}
							
							value = bin.value.front();
							bin.value.erase( bin.value.begin() );
							
							--jobs_;
							
							bin.state = value_state::uninitialized;
							
							return true;
						}
					}
				}
				
				return false;
			}
		
		private:
		
			std::atomic_size_t jobs_;
			std::array< storage_type, ConcurrentUsers > bins_;
	};
}