#pragma once

#include "core/counting_allocator.hpp"

namespace redis {

/**
 * \brief The allocator the keyspace is built with.
 *
 * The store types (BasicObject, BasicDatabase) take any allocator that meets
 * the standard Allocator requirements and rebind it for every element and node
 * they hold. This alias picks the one the server instantiates them with; point
 * it at another std-style allocator to move the whole keyspace onto it.
 */
using DefaultAllocator = CountingAllocator<char>;

} // namespace redis
