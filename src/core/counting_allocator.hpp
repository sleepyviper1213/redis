#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

namespace redis {

namespace detail {
inline std::atomic<std::size_t> in_use{0};
} // namespace detail

/**
 * \brief std::allocator that adds up the bytes it has handed out and not yet
 * got back, for INFO's used_memory.
 *
 * Stateless, so every instance (and rebind) is equal, containers keep their
 * simple move semantics and nothing is stored per allocator. The total is one
 * counter for the process: the keyspace is the only user of this allocator
 * (see DefaultAllocator), and every Database in the process shares it. The
 * counter is bumped with a relaxed atomic add, which next to the malloc it
 * accompanies is not measurable.
 */
template <typename T>
class CountingAllocator {
public:
	using value_type = T;

	constexpr CountingAllocator() noexcept = default;

	template <typename U>
	constexpr CountingAllocator( // NOLINT(google-explicit-constructor)
		const CountingAllocator<U> & /*other*/) noexcept {}

	[[nodiscard]] T *allocate(std::size_t count) {
		T *block = std::allocator<T>{}.allocate(count);
		in_use_.fetch_add(count * sizeof(T), std::memory_order_relaxed);
		return block;
	}

	void deallocate(T *block, std::size_t count) noexcept {
		std::allocator<T>{}.deallocate(block, count);
		in_use_.fetch_sub(count * sizeof(T), std::memory_order_relaxed);
	}

	template <typename U>
	constexpr bool
	operator==(const CountingAllocator<U> & /*other*/) const noexcept {
		return true;
	}

	/// Bytes allocated through any CountingAllocator and not yet freed. This is
	/// what was asked for; the allocator's own bookkeeping is not included.
	[[nodiscard]] static std::size_t bytes_in_use() noexcept {
		return in_use_.load(std::memory_order_relaxed);
	}

private:
	// Shared by every T: a rebound allocator adds to the same total.
	static inline std::atomic<std::size_t> &in_use_ = detail::in_use;
};

} // namespace redis