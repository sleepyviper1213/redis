#pragma once
// The keyspace's value types, instantiated for the server's allocator. The
// templates they come from are in detail/.

#include "allocator.hpp"
#include "detail/basic_object.hpp"
#include "detail/basic_value_types.hpp"

#include <type_traits>

namespace redis {

using detail::make_object;
using detail::object_as;
using detail::ObjectType;
using detail::type_name;

using ValueTypes = detail::BasicValueTypes<DefaultAllocator>;
using String     = ValueTypes::String;
using List       = ValueTypes::List;
using Set        = ValueTypes::Set;
using Hash       = ValueTypes::Hash;
using StringSet  = Set;
using Key        = ValueTypes::Key;
template <typename V>
using StringMap = ValueTypes::StringMap<V>;

/// A key's value.
using Object = detail::BasicObject<DefaultAllocator>;

static_assert(std::is_nothrow_move_constructible_v<Object>,
			  "a rehash would copy every key and value");

} // namespace redis
