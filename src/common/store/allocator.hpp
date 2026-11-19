#pragma once

#ifdef REDIS_HAS_JEMALLOC
#include "core/jemalloc_allocator.hpp"
#else
#include "core/counting_allocator.hpp"
#endif

namespace redis {

/**
 * \brief The allocator the keyspace is built with.
 *
 * The store types (BasicObject, BasicDatabase) take any allocator that meets
 * the standard Allocator requirements and rebind it for every element and node
 * they hold. This alias picks the one the server instantiates them with; point
 * it at another std-style allocator to move the whole keyspace onto it.
 */
#ifdef REDIS_HAS_JEMALLOC
using DefaultAllocator = JemallocAllocator<char>;
#else
using DefaultAllocator = CountingAllocator<char>;
#endif

} // namespace redis
