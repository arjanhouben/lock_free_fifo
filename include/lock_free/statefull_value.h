#pragma once

#include <atomic>

namespace lock_free
{
	enum class value_state
	{
	    uninitialized = 0,
	    ready,
	    done,
	    in_use
	};

	template < typename Value >
	struct statefull_value
	{
		typedef Value value_type;
		typedef statefull_value< value_type > self;

		statefull_value( const value_type &v = value_type() ) :
			value( v ),
			state( value_state::uninitialized )
		{
		}

		statefull_value( const self &rhs ) :
			value( rhs.value ),
			state( rhs.state.load() )
		{
		}

		statefull_value &operator = ( const self &rhs )
		{
			value = rhs.value;
			state.store( rhs.state );
			return *this;
		}

		value_type value;
		std::atomic< value_state > state;
	};
}