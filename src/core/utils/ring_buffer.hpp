#pragma once

#include "attributes.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace redis {

/**
 * \brief A double-ended queue in one contiguous, growable array.
 *
 * What a Redis list needs: O(1) push and pop at both ends and O(1) indexing,
 * like std::deque, but with two properties std::deque lacks in libstdc++:
 * - it is 32 bytes, not 80;
 * - moving it never allocates, so the move constructor is noexcept. Hash
 *   tables that grow by move_if_noexcept (boost's flat maps) then move a
 *   value holding one instead of copying it.
 *
 * Elements live in a power-of-two array used circularly from \c head_.
 * Growing doubles the array and moves the elements, so references and
 * iterators are invalidated by any push (std::deque keeps references on
 * push_front/back; callers must not rely on that here).
 *
 * \tparam Allocator Where the array comes from, honoured the way the standard
 *         containers honour one: copies ask
 *         select_on_container_copy_construction, and copy/move assignment and
 *         swap follow the propagate_on_container_* traits. An empty allocator
 *         (std::allocator) takes no space.
 */
template <typename T, typename Allocator = std::allocator<T>>
class RingBuffer {
	using traits = std::allocator_traits<Allocator>;

	static_assert(std::is_nothrow_move_constructible_v<T>,
				  "growing moves elements; a throwing move could lose them");
	static_assert(std::is_same_v<typename traits::value_type, T>,
				  "the allocator must allocate T");
	static_assert(std::is_same_v<typename traits::pointer, T *>,
				  "fancy pointers are not supported");

public:
	using value_type      = T;
	using allocator_type  = Allocator;
	using size_type       = std::size_t;
	using reference       = T &;
	using const_reference = const T &;

	template <bool Const>
	class Iterator {
		using Owner = std::conditional_t<Const, const RingBuffer, RingBuffer>;

	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type        = T;
		using difference_type   = std::ptrdiff_t;
		using reference         = std::conditional_t<Const, const T &, T &>;
		using pointer           = std::conditional_t<Const, const T *, T *>;

		Iterator() = default;
		Iterator(Owner *owner, size_type index)
			: owner_(owner), index_(index) {}

		reference operator*() const { return (*owner_)[index_]; }
		pointer operator->() const { return &(*owner_)[index_]; }

		Iterator &operator++() {
			++index_;
			return *this;
		}
		Iterator operator++(int) {
			Iterator before = *this;
			++index_;
			return before;
		}

		friend bool operator==(const Iterator &, const Iterator &) = default;

	private:
		Owner *owner_    = nullptr;
		size_type index_ = 0;
	};

	using iterator       = Iterator<false>;
	using const_iterator = Iterator<true>;

	RingBuffer() noexcept(std::is_nothrow_default_constructible_v<Allocator>) =
		default;

	explicit RingBuffer(const Allocator &alloc) noexcept : alloc_(alloc) {}

	RingBuffer(const RingBuffer &other)
		: RingBuffer(other,
					 traits::select_on_container_copy_construction(
						 other.alloc_)) {}

	RingBuffer(const RingBuffer &other, const Allocator &alloc)
		: alloc_(alloc) {
		reserve(other.size_);
		for (const T &element : other) emplace_back(element);
	}

	/// Steals the array, so it never allocates. The allocator moves with it.
	RingBuffer(RingBuffer &&other) noexcept
		: alloc_(std::move(other.alloc_)),
		  data_(std::exchange(other.data_, nullptr)),
		  capacity_(std::exchange(other.capacity_, 0)),
		  head_(std::exchange(other.head_, 0)),
		  size_(std::exchange(other.size_, 0)) {}

	/// Steals the array when \p alloc can free it; otherwise moves the
	/// elements one by one into storage from \p alloc.
	RingBuffer(RingBuffer &&other, const Allocator &alloc) : alloc_(alloc) {
		if (traits::is_always_equal::value || alloc_ == other.alloc_) {
			steal(other);
			return;
		}
		reserve(other.size_);
		for (T &element : other) emplace_back(std::move(element));
		other.clear();
	}

	RingBuffer &operator=(const RingBuffer &other) {
		if (this == &other) return *this;
		// Build the copy with the allocator this buffer will end up with,
		// then take it over: strongly exception-safe.
		const Allocator &next =
			traits::propagate_on_container_copy_assignment::value
				? other.alloc_
				: alloc_;
		RingBuffer copy(other, next);
		release();
		if constexpr (traits::propagate_on_container_copy_assignment::value)
			alloc_ = copy.alloc_;
		steal(copy);
		return *this;
	}

	RingBuffer &operator=(RingBuffer &&other) noexcept(
		traits::propagate_on_container_move_assignment::value ||
		traits::is_always_equal::value) {
		if (this == &other) return *this;
		if constexpr (traits::propagate_on_container_move_assignment::value) {
			release();
			alloc_ = std::move(other.alloc_);
			steal(other);
		} else if (traits::is_always_equal::value || alloc_ == other.alloc_) {
			release();
			steal(other);
		} else {
			// Our allocator cannot free other's array: move the elements.
			RingBuffer moved(std::move(other), alloc_);
			release();
			steal(moved);
		}
		return *this;
	}

	~RingBuffer() { release(); }

	/// \pre The allocators propagate on swap or compare equal, as for the
	///      standard containers.
	void swap(RingBuffer &other) noexcept {
		if constexpr (traits::propagate_on_container_swap::value) {
			using std::swap;
			swap(alloc_, other.alloc_);
		} else {
			assert(traits::is_always_equal::value || alloc_ == other.alloc_);
		}
		std::swap(data_, other.data_);
		std::swap(capacity_, other.capacity_);
		std::swap(head_, other.head_);
		std::swap(size_, other.size_);
	}

	friend void swap(RingBuffer &a, RingBuffer &b) noexcept { a.swap(b); }

	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return alloc_;
	}

	[[nodiscard]] size_type size() const noexcept { return size_; }
	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] size_type capacity() const noexcept { return capacity_; }

	/// \pre \p i < size().
	[[nodiscard]] T &operator[](size_type i) noexcept {
		assert(i < size_);
		return data_[slot(i)];
	}
	[[nodiscard]] const T &operator[](size_type i) const noexcept {
		assert(i < size_);
		return data_[slot(i)];
	}

	[[nodiscard]] T &front() noexcept { return (*this)[0]; }
	[[nodiscard]] const T &front() const noexcept { return (*this)[0]; }
	[[nodiscard]] T &back() noexcept { return (*this)[size_ - 1]; }
	[[nodiscard]] const T &back() const noexcept { return (*this)[size_ - 1]; }

	template <typename... Args>
	T &emplace_back(Args &&...args) {
		if (size_ == capacity_) grow();
		T *place = data_ + slot(size_);
		traits::construct(alloc_, place, std::forward<Args>(args)...);
		++size_;
		return *place;
	}

	template <typename... Args>
	T &emplace_front(Args &&...args) {
		if (size_ == capacity_) grow();
		const size_type before = (head_ + capacity_ - 1) & (capacity_ - 1);
		T *place               = data_ + before;
		traits::construct(alloc_, place, std::forward<Args>(args)...);
		head_ = before;
		++size_;
		return *place;
	}

	void push_back(const T &value) { emplace_back(value); }
	void push_back(T &&value) { emplace_back(std::move(value)); }
	void push_front(const T &value) { emplace_front(value); }
	void push_front(T &&value) { emplace_front(std::move(value)); }

	/// \pre !empty().
	void pop_front() noexcept {
		assert(size_ > 0);
		traits::destroy(alloc_, data_ + head_);
		head_ = (head_ + 1) & (capacity_ - 1);
		--size_;
	}

	/// \pre !empty().
	void pop_back() noexcept {
		assert(size_ > 0);
		traits::destroy(alloc_, data_ + slot(size_ - 1));
		--size_;
	}

	/// Destroys every element; keeps the array for reuse.
	void clear() noexcept {
		for (size_type i = 0; i < size_; ++i)
			traits::destroy(alloc_, data_ + slot(i));
		head_ = 0;
		size_ = 0;
	}

	/// Make room for at least \p n elements without further growth.
	void reserve(size_type n) {
		if (n > capacity_) reallocate(std::bit_ceil(n));
	}

	[[nodiscard]] iterator begin() noexcept { return {this, 0}; }
	[[nodiscard]] iterator end() noexcept { return {this, size_}; }
	[[nodiscard]] const_iterator begin() const noexcept { return {this, 0}; }
	[[nodiscard]] const_iterator end() const noexcept { return {this, size_}; }

	friend bool operator==(const RingBuffer &a, const RingBuffer &b) {
		return a.size_ == b.size_ && std::equal(a.begin(), a.end(), b.begin());
	}

private:
	static constexpr size_type MIN_CAPACITY = 4;

	REDIS_NO_UNIQUE_ADDRESS Allocator alloc_{};
	T *data_            = nullptr;
	size_type capacity_ = 0; // zero or a power of two
	size_type head_     = 0;
	size_type size_     = 0;

	/// Destroy the elements and free the array, leaving an empty buffer.
	void release() noexcept {
		clear();
		if (data_ != nullptr) traits::deallocate(alloc_, data_, capacity_);
		data_     = nullptr;
		capacity_ = 0;
	}

	/// Take \p other's array. \pre This buffer holds none, and its allocator
	///      can free \p other's.
	void steal(RingBuffer &other) noexcept {
		data_     = std::exchange(other.data_, nullptr);
		capacity_ = std::exchange(other.capacity_, 0);
		head_     = std::exchange(other.head_, 0);
		size_     = std::exchange(other.size_, 0);
	}

	[[nodiscard]] size_type slot(size_type i) const noexcept {
		return (head_ + i) & (capacity_ - 1);
	}

	void grow() { reallocate(capacity_ == 0 ? MIN_CAPACITY : capacity_ * 2); }

	// Moves the elements, in order, to the start of a new array.
	void reallocate(size_type capacity) {
		T *fresh = traits::allocate(alloc_, capacity);
		for (size_type i = 0; i < size_; ++i) {
			T &old = data_[slot(i)];
			traits::construct(alloc_, fresh + i, std::move(old));
			traits::destroy(alloc_, &old);
		}
		if (data_ != nullptr) traits::deallocate(alloc_, data_, capacity_);
		data_     = fresh;
		capacity_ = capacity;
		head_     = 0;
	}
};

} // namespace redis
