#pragma once

// Only built with REDIS_USE_JEMALLOC (which defines REDIS_HAS_JEMALLOC); see
// common/store/allocator.hpp for where the choice is made.
#include <jemalloc/jemalloc.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

namespace redis {

namespace detail {
inline std::atomic<std::size_t> jemalloc_in_use{0};
} // namespace detail

/**
 * \brief Standard allocator over jemalloc's extended API, adding up the bytes
 * it holds for INFO's used_memory. The same contract as CountingAllocator, so
 * either can be the DefaultAllocator.
 *
 * Stateless, so every instance and rebind is equal. The total is the usable
 * size jemalloc reserved (nallocx), not the size asked for: what the process
 * really holds, which is what a fragmentation figure needs. Frees are sized
 * (sdallocx), using the count deallocate() is given anyway.
 */
template <typename T>
class JemallocAllocator {
public:
	using value_type = T;

	constexpr JemallocAllocator() noexcept = default;

	template <typename U>
	constexpr JemallocAllocator( // NOLINT(google-explicit-constructor)
		const JemallocAllocator<U> & /*other*/) noexcept {}

	[[nodiscard]] T *allocate(std::size_t count) {
		if (count > max_count()) throw std::bad_array_new_length();
		const std::size_t size = count * sizeof(T);
		// jemalloc cannot allocate zero bytes; one is what std::allocator
		// would reserve for a request it must still answer with a pointer.
		const std::size_t asked = size == 0 ? 1 : size;
		void *block             = ::mallocx(asked, flags());
		if (block == nullptr) throw std::bad_alloc();
		in_use_.fetch_add(::nallocx(asked, flags()), std::memory_order_relaxed);
		return static_cast<T *>(block);
	}

	void deallocate(T *block, std::size_t count) noexcept {
		const std::size_t size  = count * sizeof(T);
		const std::size_t asked = size == 0 ? 1 : size;
		in_use_.fetch_sub(::nallocx(asked, flags()), std::memory_order_relaxed);
		::sdallocx(block, asked, flags());
	}

	template <typename U>
	constexpr bool
	operator==(const JemallocAllocator<U> & /*other*/) const noexcept {
		return true;
	}

	/// Bytes jemalloc reserved through any JemallocAllocator and not yet got
	/// back, by usable size.
	[[nodiscard]] static std::size_t bytes_in_use() noexcept {
		return in_use_.load(std::memory_order_relaxed);
	}

private:
	static constexpr std::size_t max_count() noexcept {
		return static_cast<std::size_t>(PTRDIFF_MAX) / sizeof(T);
	}

	// jemalloc aligns to 16 bytes already; ask for more only when T needs it.
	// The same flags go to nallocx and sdallocx, which must match mallocx.
	static constexpr int flags() noexcept {
		return alignof(T) > alignof(std::max_align_t)
				   ? MALLOCX_ALIGN(alignof(T))
				   : 0;
	}

	// Shared by every T: a rebound allocator adds to the same total.
	static inline std::atomic<std::size_t> &in_use_ = detail::jemalloc_in_use;
};

} // namespace redis
