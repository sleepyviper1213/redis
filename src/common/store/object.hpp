#pragma once
// The keyspace's value types, instantiated for the server's allocator. The
// templates they come from are in detail/.

#include "allocator.hpp"
#include "detail/basic_object.hpp"
#include "detail/basic_value_types.hpp"

#include <type_traits>

namespace redis {

/// An object holding the collection \p value, using \p value's allocator.
template <typename T>
[[nodiscard]] auto make_object(T value) { // NOLINT(performance-unnecessary-value-param)
	using Allocator = std::allocator_traits<
		typename T::allocator_type>::template rebind_alloc<char>;
	return detail::BasicObject<Allocator>::template make<T>(
		Allocator(value.get_allocator()),
		std::move(value));
}

/// \return The collection as a \p T, or nullptr if \p object holds another
///         type.
template <typename T, typename Allocator>
[[nodiscard]] T *object_as(detail::BasicObject<Allocator> &object) noexcept {
	return object.template as<T>();
}

/**
 * \brief The name the TYPE command reports for \p object.
 */
template <typename Allocator>
[[nodiscard]] std::string_view
type_name(const detail::BasicObject<Allocator> &object) {
	constexpr std::string_view names[] = {"string", "list", "set", "hash"};
	return names[static_cast<std::size_t>(object.type())];
}

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
