#pragma once
#include "core/utils/attributes.hpp"

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>

namespace redis {

/**
 * \brief An immutable string laid out as Redis's sds: one allocation holding a
 * header, the bytes and a terminating NUL, referred to by a pointer to the
 * bytes.
 *
 * The header is the smallest of Redis's five (sds.h) that holds the length:
 * - type 5, for lengths below 32: one flags byte, the type in its low 3 bits
 *   and the length in the high 5;
 * - types 8, 16, 32 and 64: len and alloc fields of that many bits, then the
 *   flags byte.
 * The flags byte always sits just before the bytes, so the pointer alone finds
 * the type, the length and the start of the allocation.
 *
 * So the object is one pointer (plus the allocator, when it has state): a key
 * in a hash table slot costs 8 bytes rather than std::string's 32, and its
 * allocation is sized to the string (a 16-byte key takes 18 bytes).
 *
 * Unlike Redis's sds this one never grows, since keys are never modified: there
 * is no append, and alloc always equals len.
 *
 * \tparam  A standard Allocator; it is rebound to char. Copies, assignment
 *         and swap follow the standard containers' allocator rules.
 */
template <typename Allocator = std::allocator<char>>
class BasicSimpleDynamicString {
	using allocator_traits = std::allocator_traits<Allocator>;
	using char_allocator =
		typename allocator_traits::template rebind_alloc<char>;
	using char_traits = std::allocator_traits<char_allocator>;

	static_assert(std::is_same_v<typename char_traits::pointer, char *>,
				  "fancy pointers are not supported");

public:
	using allocator_type = Allocator;

	explicit BasicSimpleDynamicString(std::string_view text,
									  const Allocator &alloc = Allocator())
		: alloc_(alloc), buf_(create(text)) {}

	BasicSimpleDynamicString(const BasicSimpleDynamicString &other)
		: BasicSimpleDynamicString(
			  other.view(),
			  allocator_traits::select_on_container_copy_construction(
				  other.alloc_)) {}

	BasicSimpleDynamicString(const BasicSimpleDynamicString &other,
							 const Allocator &alloc)
		: BasicSimpleDynamicString(other.view(), alloc) {}

	/// Takes the allocation over, so it never allocates.
	BasicSimpleDynamicString(BasicSimpleDynamicString &&other) noexcept
		: alloc_(std::move(other.alloc_)),
		  buf_(std::exchange(other.buf_, nullptr)) {}

	/// Takes the allocation over when \p alloc can free it; otherwise copies
	/// the bytes into storage from \p alloc.
	BasicSimpleDynamicString(BasicSimpleDynamicString &&other,
							 const Allocator &alloc)
		: alloc_(alloc) {
		if (allocator_traits::is_always_equal::value ||
			alloc_ == other.alloc_) {
			buf_ = std::exchange(other.buf_, nullptr);
		} else {
			buf_ = create(other.view());
			other.release();
		}
	}

	BasicSimpleDynamicString &operator=(const BasicSimpleDynamicString &other) {
		if (this == &other) return *this;
		constexpr bool propagate =
			allocator_traits::propagate_on_container_copy_assignment::value;
		BasicSimpleDynamicString copy(other, propagate ? other.alloc_ : alloc_);
		release();
		if constexpr (propagate) alloc_ = copy.alloc_;
		buf_ = std::exchange(copy.buf_, nullptr);
		return *this;
	}

	BasicSimpleDynamicString &
	operator=(BasicSimpleDynamicString &&other) noexcept(
		allocator_traits::propagate_on_container_move_assignment::value ||
		allocator_traits::is_always_equal::value) {
		if (this == &other) return *this;
		if constexpr (allocator_traits::propagate_on_container_move_assignment::
						  value) {
			release();
			alloc_ = std::move(other.alloc_);
			buf_   = std::exchange(other.buf_, nullptr);
		} else if (allocator_traits::is_always_equal::value ||
				   alloc_ == other.alloc_) {
			release();
			buf_ = std::exchange(other.buf_, nullptr);
		} else {
			BasicSimpleDynamicString moved(std::move(other), alloc_);
			release();
			buf_ = std::exchange(moved.buf_, nullptr);
		}
		return *this;
	}

	~BasicSimpleDynamicString() { release(); }

	/// \pre The allocators propagate on swap or compare equal.
	void swap(BasicSimpleDynamicString &other) noexcept {
		if constexpr (allocator_traits::propagate_on_container_swap::value) {
			using std::swap;
			swap(alloc_, other.alloc_);
		} else {
			assert(allocator_traits::is_always_equal::value ||
				   alloc_ == other.alloc_);
		}
		std::swap(buf_, other.buf_);
	}

	friend void swap(BasicSimpleDynamicString &a,
					 BasicSimpleDynamicString &b) noexcept {
		a.swap(b);
	}

	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return alloc_;
	}

	[[nodiscard]] std::size_t size() const noexcept {
		if (buf_ == nullptr) return 0;
		const Type type = type_of(buf_);
		if (type == TYPE_5) return flags_of(buf_) >> TYPE_BITS;
		return read_field(buf_ - header_size(type), field_width(type));
	}

	[[nodiscard]] bool empty() const noexcept { return size() == 0; }

	/// The bytes, followed by a NUL.
	[[nodiscard]] const char *c_str() const noexcept {
		return buf_ != nullptr ? buf_ : "";
	}

	[[nodiscard]] std::string_view view() const noexcept {
		return {c_str(), size()};
	}

	/// Implicit, so hash functions and comparisons written for string_view
	/// take a key as it is.
	operator std::string_view() const noexcept { // NOLINT
		return view();
	}

	friend bool operator==(const BasicSimpleDynamicString &a,
						   const BasicSimpleDynamicString &b) noexcept {
		return a.view() == b.view();
	}

	friend bool operator==(const BasicSimpleDynamicString &a,
						   std::string_view b) noexcept {
		return a.view() == b;
	}

	friend std::strong_ordering
	operator<=>(const BasicSimpleDynamicString &a,
				const BasicSimpleDynamicString &b) noexcept {
		return a.view() <=> b.view();
	}

	friend std::strong_ordering operator<=>(const BasicSimpleDynamicString &a,
											std::string_view b) noexcept {
		return a.view() <=> b;
	}

	/// The bytes of header before a string of \p length.
	[[nodiscard]] static constexpr std::size_t
	header_size(std::size_t length) noexcept {
		return header_size(type_for(length));
	}

private:
	// Redis's SDS_TYPE_* values.
	enum Type : uint8_t {
		TYPE_5  = 0,
		TYPE_8  = 1,
		TYPE_16 = 2,
		TYPE_32 = 3,
		TYPE_64 = 4,
	};

	static constexpr uint8_t TYPE_MASK = 7;
	static constexpr uint8_t TYPE_BITS = 3;

	/// The smallest header for \p length, as Redis's sdsReqType picks it.
	[[nodiscard]] static constexpr Type type_for(std::size_t length) noexcept {
		if (length < (std::size_t{1} << 5)) return TYPE_5;
		if (length < (std::size_t{1} << 8)) return TYPE_8;
		if (length < (std::size_t{1} << 16)) return TYPE_16;
		if (length < (std::uint64_t{1} << 32)) return TYPE_32;
		return TYPE_64;
	}

	/// Bytes in each of the len and alloc fields.
	[[nodiscard]] static constexpr std::size_t field_width(Type type) noexcept {
		constexpr std::size_t widths[] = {0, 1, 2, 4, 8};
		return widths[type];
	}

	[[nodiscard]] static constexpr std::size_t header_size(Type type) noexcept {
		return 2 * field_width(type) + 1;
	}

	[[nodiscard]] static uint8_t flags_of(const char *buf) noexcept {
		return static_cast<uint8_t>(buf[-1]);
	}

	[[nodiscard]] static Type type_of(const char *buf) noexcept {
		return static_cast<Type>(flags_of(buf) & TYPE_MASK);
	}

	static void write_field(char *at, std::size_t width,
							std::size_t value) noexcept {
		switch (width) {
		case 1: store(at, static_cast<uint8_t>(value)); break;
		case 2: store(at, static_cast<uint16_t>(value)); break;
		case 4: store(at, static_cast<uint32_t>(value)); break;
		default: store(at, static_cast<uint64_t>(value)); break;
		}
	}

	[[nodiscard]] static std::size_t read_field(const char *at,
												std::size_t width) noexcept {
		switch (width) {
		case 1: return load<uint8_t>(at);
		case 2: return load<uint16_t>(at);
		case 4: return load<uint32_t>(at);
		default: return static_cast<std::size_t>(load<uint64_t>(at));
		}
	}

	template <typename U>
	static void store(char *at, U value) noexcept {
		std::memcpy(at, &value, sizeof value);
	}

	template <typename U>
	[[nodiscard]] static U load(const char *at) noexcept {
		U value;
		std::memcpy(&value, at, sizeof value);
		return value;
	}

	/// Allocate header + bytes + NUL and fill them. \return The bytes.
	[[nodiscard]] char *create(std::string_view text) {
		const Type type          = type_for(text.size());
		const std::size_t header = header_size(type);
		char_allocator alloc(alloc_);
		char *block = char_traits::allocate(alloc, header + text.size() + 1);
		char *buf   = block + header;
		if (type == TYPE_5) {
			buf[-1] = static_cast<char>(TYPE_5 | (text.size() << TYPE_BITS));
		} else {
			const std::size_t width = field_width(type);
			write_field(block, width, text.size());         // len
			write_field(block + width, width, text.size()); // alloc
			buf[-1] = static_cast<char>(type);
		}
		if (!text.empty()) std::memcpy(buf, text.data(), text.size());
		buf[text.size()] = '\0';
		return buf;
	}

	void release() noexcept {
		if (buf_ == nullptr) return;
		const Type type          = type_of(buf_);
		const std::size_t header = header_size(type);
		const std::size_t alloc_bytes =
			type == TYPE_5 ? size()
						   : read_field(buf_ - header + field_width(type),
										field_width(type));
		char_allocator alloc(alloc_);
		char_traits::deallocate(alloc, buf_ - header, header + alloc_bytes + 1);
		buf_ = nullptr;
	}

	REDIS_NO_UNIQUE_ADDRESS Allocator alloc_;
	char *buf_ = nullptr;
};

} // namespace redis
