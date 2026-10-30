#pragma once
#include "server/storage/allocator.hpp"

namespace redis {

template <typename Allocator>
class BasicDatabase;

/// The keyspace the server runs on.
using Database = BasicDatabase<DefaultAllocator>;

} // namespace redis
