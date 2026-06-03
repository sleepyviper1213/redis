#pragma once
#include "assertions.hpp"
#include "forward.hpp"
#include "macro.hpp"
#include "specilization_of.hpp"
#include "try.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace AK {
namespace detail {
struct EmptyOptional {};
} // namespace detail
template <typename T>
class Optional;

template <typename T>
	requires (!std::is_reference_v<T>)
class [[nodiscard]] Optional<T> {
	template <typename U>
	friend class Optional;

public:
	using ValueType = T;

	// FIXME: Not sure why this constructor is needed,
	//        as it essentially does the same thing as the default constructor,
	//        as the union already has an initialized member,
	//        but for some reason compilers assert having default constructor
	//        on the value type
	constexpr Optional()
		requires (!std::is_constructible_v<T>)
	{}

	constexpr Optional() = default;

	template <std::same_as<detail::EmptyOptional> V>
	constexpr Optional(V) {}

	template <std::same_as<detail::EmptyOptional> V>
	constexpr Optional &operator=(V) {
		clear();
		return *this;
	}

	Optional(const Optional &other)
		requires (!std::is_copy_constructible_v<T>)
	= delete;

	Optional &operator=(const Optional &)
		requires (!std::is_copy_constructible_v<T> ||
				  !std::is_destructible_v<T>)
	= delete;

	constexpr Optional(const Optional &other)
		requires (!std::is_trivially_constructible_v<T>)
		: m_has_value(other.m_has_value) {
		if (other.has_value())
			std::construct_at<std::remove_const_t<T>>(&m_storage,
													  other.value());
	}

	constexpr Optional &operator=(const Optional &other)
		requires (!std::is_trivially_copy_assignable_v<T> ||
				  !std::is_trivially_destructible_v<T>)
	{
		if (this == &other) return *this;

		clear();
		m_has_value = other.m_has_value;
		if (other.has_value())
			std::construct_at<std::remove_const_t<T>>(&m_storage,
													  other.value());
		return *this;
	}

	constexpr Optional(const Optional &other)       = default;
	constexpr Optional &operator=(const Optional &) = default;

	Optional(Optional &&other)
		requires (!std::is_move_constructible_v<T>)
	= delete;

	constexpr Optional(Optional &&other)
		requires (std::is_move_constructible_v<T> &&
				  !std::is_trivially_move_constructible_v<T>)
		: m_has_value(other.m_has_value) {
		if (other.has_value())
			std::construct_at<std::remove_const_t<T>>(&m_storage,
													  other.release_value());
	}

	// Note: The _move_construcitble_v only versions are to allow for non-move
	// assignable types,
	//       such as types containing a reference.
	//       Move assigning into an Optional still makes sense in these cases,
	//       as to replace its contents or to allow for find-like patterns
	//       through iterator helpers.

	Optional &operator=(Optional &&other)
		requires (!std::is_move_constructible_v<T> ||
				  !std::is_destructible_v<T>)
	= delete;

	constexpr Optional &operator=(Optional &&other)
		requires (std::is_move_assignable_v<T> &&
				  std::is_move_constructible_v<T> &&
				  (!std::is_trivially_move_assignable_v<T> ||
				   !std::is_trivially_move_constructible_v<T> ||
				   !std::is_trivially_destructible_v<T>))
	{
		if (this == &other) return *this;

		if (m_has_value && other.m_has_value) {
			value() = other.release_value();
		} else if (m_has_value) {
			std::destroy_at(&value());

			m_has_value = false;
		} else if (other.m_has_value) {
			std::construct_at<std::remove_const_t<T>>(&m_storage,
													  other.release_value());
			m_has_value = true;
		}
		return *this;
	}

	constexpr Optional &operator=(Optional &&other)
		requires (std::is_move_constructible_v<T> &&
				  !std::is_move_assignable_v<T> &&
				  (!std::is_trivially_move_constructible_v<T> ||
				   !std::is_trivially_destructible_v<T>))
	{
		clear();

		m_has_value = other.m_has_value;
		if (other.has_value())
			std::construct_at<std::remove_const_t<T>>(&m_storage,
													  other.release_value());

		return *this;
	}

	// Note: These versions are not allowing scalar types, as those would mess
	// with the `= {}`
	//       clearing pattern, they still work through an implicit conversion to
	//       Optional<T> and the regular move-assignment operator.
	template <class U = T>
		requires (
			!one_of<std::remove_cvref_t<U>, Optional, detail::EmptyOptional> &&
			!(std::is_same_v<U, T> && std::is_scalar_v<U>))
	constexpr Optional<T> &operator=(U &&value)
		requires (requires(T &t, U &&u) { t = std::forward<U>(u); } &&
				  std::is_constructible_v<T, U &&>)
	{
		if (m_has_value) m_storage = std::forward<U>(value);
		else
			std::construct_at<std::remove_const_t<T>>(&m_storage,
													  std::forward<U>(value));
		m_has_value = true;
		return *this;
	}

	template <class U = T>
		requires (
			!one_of<std::remove_cvref_t<U>, Optional, detail::EmptyOptional> &&
			!(std::same_as<U, T> && std::is_scalar_v<U>))
	constexpr Optional<T> &operator=(U &&value)
		requires (!(requires(T &t, U &&u) { t = std::forward<U>(u); }) &&
				  std::is_constructible_v<T, U &&>)
	{
		// Note: This one is needed, as it is a common pattern to assign to an
		// Optional to set or replace its contents
		clear();
		std::construct_at<std::remove_const_t<T>>(&m_storage,
												  std::forward<U>(value));
		m_has_value = true;
		return *this;
	}

	constexpr Optional(Optional &&other)            = default;
	constexpr Optional &operator=(Optional &&other) = default;

	~Optional()
		requires (!std::is_destructible_v<T>)
	= delete;

	constexpr ~Optional()
		requires (std::is_destructible_v<T> &&
				  !std::is_trivially_destructible_v<T>)
	{
		clear();
	}

	constexpr ~Optional() = default;

	template <typename U>
		requires (std::is_constructible_v<T, const U &> &&
				  !is_specialization_of<T, Optional> &&
				  !is_specialization_of<U, Optional> &&
				  !std::is_lvalue_reference_v<U>)
	constexpr explicit Optional(const Optional<U> &other)
		: m_has_value(other.m_has_value) {
		if (other.has_value())
			std::construct_at<std::remove_const_t<T>>(&m_storage,
													  other.value());
	}

	template <typename U>
		requires (std::is_constructible_v<T, U &&> &&
				  !is_specialization_of<T, Optional> &&
				  !is_specialization_of<U, Optional> &&
				  !std::is_lvalue_reference_v<U>)
	constexpr explicit Optional(Optional<U> &&other)
		: m_has_value(other.m_has_value) {
		if (other.has_value())
			construct_at<std::remove_const_t<T>>(&m_storage,
												 other.release_value());
	}

	template <typename U = T>
		requires (!std::same_as<detail::EmptyOptional, std::remove_cvref_t<U>>)
	constexpr explicit(!std::convertible_to<U &&, T>) Optional(U &&value)
		requires (!std::same_as<std::remove_cvref_t<U>, Optional<T>> &&
				  std::is_constructible_v<T, U &&>)
		: m_has_value(true) {
		std::construct_at<std::remove_const_t<T>>(&m_storage,
												  std::forward<U>(value));
	}

	template <typename O>
	constexpr bool operator==(const Optional<O> &other) const {
		return has_value() == other.has_value() &&
			   (!has_value() || value() == other.value());
	}

	template <typename O>
	constexpr bool operator==(const O &other) const {
		return has_value() && value() == other;
	}

	constexpr void clear() {
		if (!m_has_value) return;

		std::destroy_at(&value());
		m_has_value = false;
	}

	template <typename... Args>
	constexpr void emplace(Args &&...parameters) {
		clear();
		m_has_value = true;
		std::construct_at<std::remove_const_t<T>>(
			&m_storage,
			std::forward<Args>(parameters)...);
	}

	template <class U, class... Args>
	void emplace(std::initializer_list<U> il, Args &&...args) {
		clear();
		std::construct_at(
			reinterpret_cast<std::remove_const_t<T> *>(&m_storage),
			il,
			std::forward<Args>(args)...);
		m_has_value = true;
	}

	template <typename Callable>
	constexpr void lazy_emplace(Callable callable) {
		clear();
		m_has_value = true;
		std::construct_at<std::remove_const_t<T>>(&m_storage, callable());
	}

	[[nodiscard]] constexpr bool has_value() const { return m_has_value; }

	template <typename Self>
	[[nodiscard]] constexpr auto &&value(this Self &&self) {
		VERIFY(self.m_has_value);
		return std::forward<Self>(self).m_storage;
	}

	[[nodiscard]] constexpr T release_value() {
		VERIFY(m_has_value);
		T released_value = std::move(value());
		std::destroy_at(&value());
		m_has_value = false;
		return released_value;
	}

	[[nodiscard]] constexpr T value_or(const T &default_value) const & {
		if (m_has_value) return value();
		return default_value;
	}

	[[nodiscard]] constexpr T value_or(T &&default_value) && {
		if (m_has_value) return std::move(value());
		return std::move(default_value);
	}

	template <typename Self, typename Callback>
	[[nodiscard]] constexpr auto value_or_else(this Self &&self,
											   Callback callback) {
		if (self.m_has_value) return std::forward<Self>(self).value();
		return callback();
	}

	template <typename Self, typename Callback>
	[[nodiscard]] constexpr Optional<T> value_or_else(this Self &&self,
													  Callback callback) {
		if (self.m_has_value) return std::forward<Self>(self).value();
		return callback();
	}

	template <typename Self, typename Callback>
	[[nodiscard]] constexpr ErrorOr<T> ok_or_else(this Self &&self,
												  Callback callback) {
		if (self.m_has_value) return std::forward<Self>(self).value();
		return TRY(callback());
	}

	template <class Self>
	constexpr auto &&operator*(this Self &&self) {
		return std::forward<Self>(self).value();
	}

	template <class Self>
	constexpr auto &&operator->(this Self &&self) {
		return &std::forward<Self>(self.value());
	}

	template <typename Self, typename F>
	constexpr auto map(this Self &&self, F &&mapper) {
		using MappedType = std::invoke_result_t<F, T &>;
		if constexpr (is_specialization_of<MappedType, ErrorOr>) {
			using OptionalType = Optional<typename MappedType::ValueType>;
			using ReturnType   = ErrorOr<OptionalType>;
			if (!self.has_value()) return ReturnType{OptionalType{}};
			return ReturnType{TRY(mapper(std::forward<Self>(self).value()))};
		} else {
			using OptionalType = Optional<MappedType>;
			if (!self.has_value()) return OptionalType{};
			return OptionalType{mapper(std::forward<Self>(self).value())};
		}
	}

private:
	union {
		alignas(T) std::byte m_null[sizeof(T)]{};
		std::remove_cv_t<T> m_storage;
	};

	bool m_has_value{false};
};

template <typename T>
	requires (std::is_lvalue_reference_v<T>)
class [[nodiscard]] Optional<T> {
	template <typename>
	friend class Optional;

	template <typename U>
	constexpr inline static bool CanBePlacedInOptional =
		std::same_as<std::remove_reference_t<T>,
					 std::remove_reference_t<AddConstToReferencedType<U>>> &&
		(std::derived_from<std::remove_cvref_t<U>, std::remove_cvref_t<T>> ||
		 std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<U>>);

public:
	using ValueType = T;

	constexpr Optional() = default;

	template <std::same_as<detail::EmptyOptional> V>
	constexpr Optional(V) {}

	template <std::same_as<detail::EmptyOptional> V>
	constexpr Optional &operator=(V) {
		clear();
		return *this;
	}

	template <typename U = T>
	constexpr Optional(U &value)
		requires (CanBePlacedInOptional<U &>)
		: m_pointer(&value) {}

	constexpr Optional(std::remove_reference_t<T> &value) : m_pointer(&value) {}

	constexpr Optional(const Optional &other) = default;

	constexpr Optional(Optional &&other) : m_pointer(other.m_pointer) {
		other.m_pointer = nullptr;
	}

	template <typename U>
	constexpr Optional(const Optional<U> &other)
		requires (CanBePlacedInOptional<U>)
		: m_pointer(other.m_pointer) {}

	template <typename U>
	constexpr Optional(Optional<U> &&other)
		requires (CanBePlacedInOptional<U>)
	{
		m_pointer = std::exchange(other.m_pointer, nullptr);
	}

	constexpr Optional &operator=(const Optional &other) = default;

	constexpr Optional &operator=(Optional &&other) {
		m_pointer = std::exchange(other.m_pointer, nullptr);
		return *this;
	}

	template <typename U>
	constexpr Optional &operator=(const Optional<U> &other)
		requires (CanBePlacedInOptional<U>)
	{
		m_pointer = other.m_pointer;
		return *this;
	}

	template <typename U>
	constexpr Optional &operator=(Optional<U> &&other)
		requires (CanBePlacedInOptional<U>)
	{
		m_pointer = std::exchange(other.m_pointer, nullptr);
		return *this;
	}

	// Note: Disallows assignment from a temporary as this does not do any
	// lifetime extension.
	template <typename U>
		requires (!std::same_as<detail::EmptyOptional, std::remove_cvref_t<U>>)
	constexpr Optional &operator=(U &&value)
		requires (CanBePlacedInOptional<U> && std::is_lvalue_reference_v<U>)
	{
		m_pointer = &value;
		return *this;
	}

	constexpr void clear() { m_pointer = nullptr; }

	explicit constexpr operator bool() const { return m_pointer != nullptr; }

	[[nodiscard]] constexpr bool has_value() const {
		return m_pointer != nullptr;
	}

	template <typename Self>
	[[nodiscard]] constexpr auto &&value(this Self &&self) {
		VERIFY(self.m_pointer);
		return *std::forward<Self>(self).m_pointer;
	}

	template <typename U>
		requires (std::is_base_of_v<std::remove_cvref_t<T>, U>)
	[[nodiscard]]
	constexpr AddConstToReferencedType<T> value_or(U &fallback) const {
		if (m_pointer) return value();
		return fallback;
	}

	// Note that this ends up copying the value.
	[[nodiscard]] constexpr std::remove_cvref_t<T>
	value_or(std::remove_cvref_t<T> fallback) const {
		if (m_pointer) return value();
		return fallback;
	}

	[[nodiscard]] constexpr T release_value() {
		return *std::exchange(m_pointer, nullptr);
	}

	template <typename U>
	constexpr bool operator==(const Optional<U> &other) const {
		return has_value() == other.has_value() &&
			   (!has_value() || value() == other.value());
	}

	template <typename U>
	constexpr bool operator==(const U &other) const {
		return has_value() && value() == other;
	}

	constexpr std::add_pointer_t<AddConstToReferencedType<T>>
	operator->() const {
		return &value();
	}

	constexpr std::add_pointer_t<std::remove_reference_t<T>> operator->() {
		return &value();
	}

	// Conversion operators from Optional<T&> -> Optional<T>
	constexpr explicit operator Optional<std::remove_cvref_t<T>>() const {
		if (has_value()) return Optional<std::remove_cvref_t<T>>(value());
		return {};
	}

	constexpr Optional<std::remove_cvref_t<T>> copy() const {
		return static_cast<Optional<std::remove_cvref_t<T>>>(*this);
	}

	template <typename Callback>
	[[nodiscard]] constexpr T value_or_else(Callback callback) const {
		if (m_pointer != nullptr) return value();
		return callback();
	}

	template <typename Callback>
	[[nodiscard]] constexpr Optional<T>
	value_or_lazy_evaluated_optional(Callback callback) const {
		if (m_pointer != nullptr) return value();
		return callback();
	}

	template <typename Callback>
	[[nodiscard]] constexpr ErrorOr<T> ok_or_else(Callback callback) const {
		if (m_pointer != nullptr) return value();
		return TRY(callback());
	}

	template <typename Callback>
	[[nodiscard]] constexpr ErrorOr<Optional<T>>
	try_value_or_lazy_evaluated_optional(Callback callback) const {
		if (m_pointer != nullptr) return value();
		return TRY(callback());
	}

	template <typename Self, typename F>
		requires std::invocable<F, T &>
	constexpr auto map(this Self &&self, F &&mapper) {
		using MappedType         = std::invoke_result_t<F, T &>;
		constexpr bool IsErrorOr = is_specialization_of<MappedType, ErrorOr>;

		using OptionalType = Optional<std::conditional_t<
			IsErrorOr,
			typename MappedType::ValueType, // unwrap ErrorOr<T,E>
			MappedType>>;

		using ReturnType =
			std::conditional_t<IsErrorOr, ErrorOr<OptionalType>, OptionalType>;

		if (!self.m_pointer) return ReturnType{OptionalType{}};

		if constexpr (IsErrorOr)
			return ReturnType{TRY(mapper(std::forward<Self>(self).value()))};
		else return ReturnType{mapper(std::forward<Self>(self).value())};
	}

private:
	std::remove_reference_t<T> *m_pointer = nullptr;
};

template <class T>
Optional(T) -> Optional<T>;

template <typename T1, typename T2>
constexpr auto operator&&(const Optional<T1> &o1, const Optional<T2> &o2) {
	if (!o1.has_value()) return o2;
	if (!o2.has_value()) return o1;
	return detail::EmptyOptional{};
}

template <typename T1, typename T2>
constexpr auto operator||(const Optional<T1> &o1, const Optional<T2> &o2) {
	if (o1.has_value()) return o1;
	if (o2.has_value()) return o2;
	return detail::EmptyOptional{};
}
} // namespace AK
