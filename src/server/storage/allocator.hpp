#pragma once

#include <memory>

namespace redis {

/**
 * \brief The allocator the server's keyspace is built with.
 *
 * The storage types (BasicObject, BasicDatabase) take any allocator that meets
 * the standard Allocator requirements and rebind it for every element and node
 * they hold. This alias picks the one the server instantiates them with; point
 * it at another std-style allocator to move the whole keyspace onto it.
 */
using DefaultAllocator = std::allocator<char>;

} // namespace redis
