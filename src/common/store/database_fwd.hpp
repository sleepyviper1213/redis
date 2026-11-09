#pragma once
#include "allocator.hpp"

namespace redis {

namespace detail {
template <typename Allocator>
class BasicDatabase;
} // namespace detail

/// The keyspace the server runs on.
using Database = detail::BasicDatabase<DefaultAllocator>;


} // namespace redis
