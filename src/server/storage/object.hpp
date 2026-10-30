#pragma once

#if defined(REDIS_KEYSPACE_MAP_ABSL)
#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#elif !defined(REDIS_KEYSPACE_MAP_STD)
#include <boost/unordered/unordered_flat_map.hpp>
#endif

#include "core/utils/attributes.hpp"
#include "core/utils/ring_buffer.hpp"
#include "server/storage/allocator.hpp"
#include "server/storage/simple_dynamic_string.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace redis {

/**
 * \brief Hash for String keys that also accepts std::string_view, so lookups
 * by a view do not allocate a temporary string.
 */
struct TransparentStringHash {
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view s) const noexcept {
		return std::hash<std::string_view>{}(s);
	}
};

#if defined(REDIS_KEYSPACE_MAP_ABSL)
struct AbslStringHash {
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view s) const noexcept {
		return absl::Hash<std::string_view>{}(s);
	}
};
#endif

/**
 * \brief The strings and containers a keyspace stores, for allocator \p Allocator.
 *
 * \p Allocator is any type meeting the standard Allocator requirements; its value
 * type does not matter, since every element and node type is rebound from it.
 */
template <typename Allocator>
struct BasicValueTypes {
	template <typename T>
	using rebound = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

	using String = std::basic_string<char, std::char_traits<char>, rebound<char>>;
	using List   = RingBuffer<String, rebound<String>>;
	using Set    = std::unordered_set<String, TransparentStringHash,
									  std::equal_to<>, rebound<String>>;

	template <typename V>
	using StringMap =
		std::unordered_map<String, V, TransparentStringHash, std::equal_to<>,
						   rebound<std::pair<const String, V>>>;

	using Hash = StringMap<String>;

	/// Keys of the keyspace: immutable, one pointer, as Redis's sds.
	using Key = BasicSimpleDynamicString<rebound<char>>;

	/**
	 * \brief Keyspace map, selected at configure time with
	 * -DREDIS_KEYSPACE_MAP=absl|boost|std (default absl).
	 *
	 * boost and absl are open-addressing (Swiss-table style): one flat array,
	 * SIMD group probing, no per-entry allocation. References and iterators
	 * are invalidated by any insertion, so callers must not hold an Object* (or
	 * a String*) across a later insert into the same Database. std keeps node
	 * stability and serves as the baseline. Set and Hash values always use the
	 * node-based containers because SPOP relies on the bucket interface.
	 */
#if defined(REDIS_KEYSPACE_MAP_ABSL)
	template <typename V>
	using KeyMap =
		absl::flat_hash_map<Key, V, AbslStringHash, std::equal_to<>,
							rebound<std::pair<const Key, V>>>;
#elif defined(REDIS_KEYSPACE_MAP_STD)
	template <typename V>
	using KeyMap =
		std::unordered_map<Key, V, TransparentStringHash, std::equal_to<>,
						   rebound<std::pair<const Key, V>>>;
#else
	template <typename V>
	using KeyMap =
		boost::unordered_flat_map<Key, V, TransparentStringHash,
								  std::equal_to<>,
								  rebound<std::pair<const Key, V>>>;
#endif
};

/// The type of a key's value. The order is the snapshot format's type byte.
enum class ObjectType : uint8_t { String, List, Set, Hash };

/**
 * \brief A key's value, laid out like Redis's robj: a type tag and one pointer
 * to the value, which lives in memory from the allocator.
 *
 * Strings of up to EMBEDDED_CAPACITY bytes are instead stored in the object
 * itself, where the pointer would be, as Redis's embstr encoding stores short
 * strings next to the robj header. So the common short value costs no
 * allocation. Because such a string has no String object, strings are read
 * with string(), replaced with assign_string() and edited in place through
 * mutable_string(), which moves an embedded string to the heap first, as
 * Redis converts an embstr to raw on APPEND.
 *
 * With a stateless allocator (std::allocator) the object is 16 bytes, so a
 * keyspace slot (key + value) is 48 bytes. A stateful allocator is stored too
 * and adds its size.
 *
 * The object owns its value and is allocator-aware the way the standard
 * containers are: a value is built by uses-allocator construction, so the
 * containers inside it allocate from the same allocator; copies ask
 * select_on_container_copy_construction; copy and move assignment and swap
 * follow the propagate_on_container_* traits.
 *
 * A move leaves a heap value's source empty: as() on it returns nullptr and
 * string() an empty view, and it must not be visited.
 */
template <typename Allocator>
class BasicObject {
	using Types  = BasicValueTypes<Allocator>;
	using traits = std::allocator_traits<Allocator>;

	template <typename T>
	using rebound = typename traits::template rebind_alloc<T>;

public:
	using allocator_type = Allocator;
	using String         = typename Types::String;
	using List           = typename Types::List;
	using Set            = typename Types::Set;
	using Hash           = typename Types::Hash;

	/// The longest string stored inside the object rather than on the heap.
	static constexpr std::size_t EMBEDDED_CAPACITY = 14;

	/// The tag of value type \p T.
	template <typename T>
	[[nodiscard]] static constexpr ObjectType type_of() noexcept {
		if constexpr (std::is_same_v<T, String>) return ObjectType::String;
		else if constexpr (std::is_same_v<T, List>) return ObjectType::List;
		else if constexpr (std::is_same_v<T, Set>) return ObjectType::Set;
		else {
			static_assert(std::is_same_v<T, Hash>, "not a value type");
			return ObjectType::Hash;
		}
	}

	/// An object holding a collection \p T built from \p args with storage
	/// from \p alloc.
	template <typename T, typename... Args>
	[[nodiscard]] static BasicObject make(const Allocator &alloc, Args &&...args) {
		static_assert(!std::is_same_v<T, String>, "use make_string()");
		BasicObject object(alloc);
		object.set_heap(type_of<T>(), object.template create<T>(
										  std::forward<Args>(args)...));
		return object;
	}

	/// An object holding a copy of \p text, embedded when it is short enough.
	[[nodiscard]] static BasicObject make_string(const Allocator &alloc,
												 std::string_view text) {
		BasicObject object(alloc);
		object.assign_string(text);
		return object;
	}

	/// Implicit, so a string can be passed wherever an object is expected. The
	/// object uses the string's allocator.
	BasicObject(String value) // NOLINT(google-explicit-constructor)
		: alloc_(value.get_allocator()) {
		if (value.size() <= EMBEDDED_CAPACITY) set_embedded(value);
		else set_heap(ObjectType::String, create<String>(std::move(value)));
	}

	BasicObject(const BasicObject &other)
		: BasicObject(other,
					  traits::select_on_container_copy_construction(
						  other.alloc_)) {}

	BasicObject(const BasicObject &other, const Allocator &alloc) : alloc_(alloc) {
		if (!other.on_heap()) repr_ = other.repr_;
		else set_heap(other.type(), clone(other));
	}

	/// Takes the value over, so it never allocates. The allocator moves with it.
	BasicObject(BasicObject &&other) noexcept
		: alloc_(std::move(other.alloc_)), repr_(other.repr_) {
		if (other.on_heap()) other.repr_.heap.ptr = nullptr;
	}

	/// Takes the value over when \p alloc can free it; otherwise moves it into
	/// storage from \p alloc.
	BasicObject(BasicObject &&other, const Allocator &alloc)
		: alloc_(alloc), repr_(other.repr_) {
		if (!other.on_heap()) return;
		if (traits::is_always_equal::value || alloc_ == other.alloc_) {
			other.repr_.heap.ptr = nullptr;
			return;
		}
		repr_.heap.ptr = nullptr;
		if (other.repr_.heap.ptr == nullptr) return;
		repr_.heap.ptr = move_across(other);
		other.destroy();
	}

	BasicObject &operator=(const BasicObject &other) {
		if (this == &other) return *this;
		// Build the copy with the allocator this object will end up with, then
		// take it over: strongly exception-safe.
		const Allocator &next = traits::propagate_on_container_copy_assignment::value
							? other.alloc_
							: alloc_;
		BasicObject copy(other, next);
		destroy();
		if constexpr (traits::propagate_on_container_copy_assignment::value)
			alloc_ = copy.alloc_;
		steal(copy);
		return *this;
	}

	BasicObject &operator=(BasicObject &&other) noexcept(
		traits::propagate_on_container_move_assignment::value ||
		traits::is_always_equal::value) {
		if (this == &other) return *this;
		if constexpr (traits::propagate_on_container_move_assignment::value) {
			destroy();
			alloc_ = std::move(other.alloc_);
			steal(other);
		} else if (traits::is_always_equal::value || alloc_ == other.alloc_) {
			destroy();
			steal(other);
		} else {
			// Our allocator cannot free other's value: move it across.
			BasicObject moved(std::move(other), alloc_);
			destroy();
			steal(moved);
		}
		return *this;
	}

	~BasicObject() { destroy(); }

	/// \pre The allocators propagate on swap or compare equal, as for the
	///      standard containers.
	void swap(BasicObject &other) noexcept {
		if constexpr (traits::propagate_on_container_swap::value) {
			using std::swap;
			swap(alloc_, other.alloc_);
		} else {
			assert(traits::is_always_equal::value || alloc_ == other.alloc_);
		}
		std::swap(repr_, other.repr_);
	}

	friend void swap(BasicObject &a, BasicObject &b) noexcept { a.swap(b); }

	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return alloc_;
	}

	[[nodiscard]] ObjectType type() const noexcept { return repr_.heap.type; }

	/// Whether the value is a string stored inside the object.
	[[nodiscard]] bool embedded() const noexcept { return !on_heap(); }

	/// \return The collection as a \p T, or nullptr if the object holds another
	///         type or was moved from.
	template <typename T>
	[[nodiscard]] T *as() noexcept {
		static_assert(!std::is_same_v<T, String>,
					  "a string may be embedded: use string()");
		if (type() != type_of<T>()) return nullptr;
		return static_cast<T *>(repr_.heap.ptr);
	}

	template <typename T>
	[[nodiscard]] const T *as() const noexcept {
		static_assert(!std::is_same_v<T, String>,
					  "a string may be embedded: use string()");
		if (type() != type_of<T>()) return nullptr;
		return static_cast<const T *>(repr_.heap.ptr);
	}

	/// The string. \pre type() is ObjectType::String.
	[[nodiscard]] std::string_view string() const noexcept {
		assert(type() == ObjectType::String);
		if (!on_heap()) return {repr_.embedded.chars, repr_.embedded.size};
		const auto *heap = static_cast<const String *>(repr_.heap.ptr);
		return heap ? std::string_view(*heap) : std::string_view{};
	}

	/// Make the value the string \p text, whatever it held before.
	void assign_string(std::string_view text) {
		if (text.size() <= EMBEDDED_CAPACITY) {
			destroy();
			set_embedded(text);
			return;
		}
		if (type() == ObjectType::String && on_heap() &&
			repr_.heap.ptr != nullptr) {
			// Reuse the heap string's buffer.
			static_cast<String *>(repr_.heap.ptr)->assign(text);
			return;
		}
		String *fresh = create<String>(text);
		destroy();
		set_heap(ObjectType::String, fresh);
	}

	/// The string as a heap String, for editing in place. An embedded string
	/// is moved to the heap first. \pre type() is ObjectType::String.
	[[nodiscard]] String &mutable_string() {
		assert(type() == ObjectType::String);
		if (!on_heap() || repr_.heap.ptr == nullptr) {
			String *heap = create<String>(string());
			set_heap(ObjectType::String, heap);
		}
		return *static_cast<String *>(repr_.heap.ptr);
	}

	/// Calls \p f with the value: a std::string_view for a string, the
	/// collection otherwise. \pre Not moved from.
	template <typename F>
	decltype(auto) visit(F &&f) const {
		switch (type()) {
		case ObjectType::String: return std::forward<F>(f)(string());
		case ObjectType::List:
			return std::forward<F>(f)(
				*static_cast<const List *>(repr_.heap.ptr));
		case ObjectType::Set:
			return std::forward<F>(f)(*static_cast<const Set *>(repr_.heap.ptr));
		case ObjectType::Hash:
			return std::forward<F>(f)(
				*static_cast<const Hash *>(repr_.heap.ptr));
		}
		std::unreachable();
	}

private:
	/// Marks the heap layout in the size byte; any other value is the length
	/// of an embedded string.
	static constexpr uint8_t HEAP = 0xFF;

	// Two layouts of the same 16 bytes. Both start with the type and the size
	// byte, so those can be read through either (a common initial sequence).
	struct Embedded {
		ObjectType type;
		uint8_t size;
		char chars[EMBEDDED_CAPACITY];
	};

	struct Heap {
		ObjectType type;
		uint8_t size; // always HEAP
		void *ptr;
	};

	union Repr {
		Embedded embedded;
		Heap heap;
	};

	static_assert(std::is_trivially_copyable_v<Repr>);
	static_assert(sizeof(Repr) == 16);

	/// An empty heap string: the state a constructor starts from.
	explicit BasicObject(const Allocator &alloc) noexcept : alloc_(alloc) {
		set_heap(ObjectType::String, nullptr);
	}

	[[nodiscard]] bool on_heap() const noexcept {
		return repr_.heap.size == HEAP;
	}

	void set_heap(ObjectType type, void *ptr) noexcept {
		repr_.heap = Heap{.type = type, .size = HEAP, .ptr = ptr};
	}

	void set_embedded(std::string_view text) noexcept {
		assert(text.size() <= EMBEDDED_CAPACITY);
		repr_.embedded = Embedded{.type  = ObjectType::String,
								  .size  = static_cast<uint8_t>(text.size()),
								  .chars = {}};
		if (!text.empty())
			std::memcpy(repr_.embedded.chars, text.data(), text.size());
	}

	/// Allocate a T and build it by uses-allocator construction, so a
	/// container gets this object's allocator.
	template <typename T, typename... Args>
	[[nodiscard]] T *create(Args &&...args) {
		using value_traits = std::allocator_traits<rebound<T>>;
		rebound<T> alloc(alloc_);
		T *place = value_traits::allocate(alloc, 1);
		try {
			std::uninitialized_construct_using_allocator(
				place, alloc, std::forward<Args>(args)...);
		} catch (...) {
			value_traits::deallocate(alloc, place, 1);
			throw;
		}
		return place;
	}

	template <typename T>
	void release(T *place) noexcept {
		using value_traits = std::allocator_traits<rebound<T>>;
		rebound<T> alloc(alloc_);
		value_traits::destroy(alloc, place);
		value_traits::deallocate(alloc, place, 1);
	}

	/// A copy of \p source's heap value in storage from this allocator.
	[[nodiscard]] void *clone(const BasicObject &source) {
		const void *ptr = source.repr_.heap.ptr;
		if (ptr == nullptr) return nullptr;
		switch (source.type()) {
		case ObjectType::String:
			return create<String>(*static_cast<const String *>(ptr));
		case ObjectType::List:
			return create<List>(*static_cast<const List *>(ptr));
		case ObjectType::Set:
			return create<Set>(*static_cast<const Set *>(ptr));
		case ObjectType::Hash:
			return create<Hash>(*static_cast<const Hash *>(ptr));
		}
		std::unreachable();
	}

	/// \p source's heap value, moved into storage from this allocator.
	[[nodiscard]] void *move_across(BasicObject &source) {
		void *ptr = source.repr_.heap.ptr;
		switch (source.type()) {
		case ObjectType::String:
			return create<String>(std::move(*static_cast<String *>(ptr)));
		case ObjectType::List:
			return create<List>(std::move(*static_cast<List *>(ptr)));
		case ObjectType::Set:
			return create<Set>(std::move(*static_cast<Set *>(ptr)));
		case ObjectType::Hash:
			return create<Hash>(std::move(*static_cast<Hash *>(ptr)));
		}
		std::unreachable();
	}

	void destroy() noexcept {
		if (!on_heap() || repr_.heap.ptr == nullptr) return;
		void *ptr = repr_.heap.ptr;
		switch (type()) {
		case ObjectType::String: release(static_cast<String *>(ptr)); break;
		case ObjectType::List: release(static_cast<List *>(ptr)); break;
		case ObjectType::Set: release(static_cast<Set *>(ptr)); break;
		case ObjectType::Hash: release(static_cast<Hash *>(ptr)); break;
		}
		repr_.heap.ptr = nullptr;
	}

	/// Take \p other's value. \pre This object holds no heap value, and its
	///      allocator can free \p other's.
	void steal(BasicObject &other) noexcept {
		repr_ = other.repr_;
		if (other.on_heap()) other.repr_.heap.ptr = nullptr;
	}

	REDIS_NO_UNIQUE_ADDRESS Allocator alloc_;
	Repr repr_;
};

/// An object holding the collection \p value, using \p value's allocator.
template <typename T>
[[nodiscard]] auto make_object(T value) {
	using Allocator = typename std::allocator_traits<
		typename T::allocator_type>::template rebind_alloc<char>;
	return BasicObject<Allocator>::template make<T>(Allocator(value.get_allocator()),
											 std::move(value));
}

/// \return The collection as a \p T, or nullptr if \p object holds another
///         type.
template <typename T, typename Allocator>
[[nodiscard]] T *object_as(BasicObject<Allocator> &object) noexcept {
	return object.template as<T>();
}

/**
 * \brief The name the TYPE command reports for \p object.
 */
template <typename Allocator>
[[nodiscard]] std::string_view type_name(const BasicObject<Allocator> &object) {
	constexpr std::string_view names[] = {"string", "list", "set", "hash"};
	return names[static_cast<std::size_t>(object.type())];
}

// The types the server uses, from DefaultAllocator.
using ValueTypes = BasicValueTypes<DefaultAllocator>;
using String     = ValueTypes::String;
using List       = ValueTypes::List;
using Set        = ValueTypes::Set;
using Hash       = ValueTypes::Hash;
using StringSet  = Set;
using Key        = ValueTypes::Key;
template <typename V>
using StringMap = ValueTypes::StringMap<V>;
using Object    = BasicObject<DefaultAllocator>;

static_assert(sizeof(BasicObject<std::allocator<char>>) == 16,
			  "a type tag and a pointer, as in robj");
static_assert(std::is_nothrow_move_constructible_v<Object>,
			  "a rehash would copy every key and value");

} // namespace redis
