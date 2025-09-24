#pragma once
#include "assertions.hpp"

#include <cassert>
#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace AK {
namespace detail {
struct EmptyExpected {};
} // namespace detail

template <typename T, typename E>
class [[nodiscard]] ErrorOr {
	template <typename T2, typename E2>
	friend class ErrorOr;

public:
	using T1 = T;
	using E1 = E;

	constexpr ErrorOr()
		requires (std::same_as<T, detail::EmptyExpected>)
		: m_value{}, m_is_error(false) {}

	constexpr ErrorOr(ErrorOr &&other)
		requires (!std::is_trivially_move_constructible_v<T> ||
				  !std::is_trivially_move_constructible_v<E>)
		: m_is_error(other.m_is_error) {
		if (other.is_error())
			std::construct_at<E>(&m_error, other.release_error());
		else std::construct_at<T>(&m_value, other.release_value());
	}

	constexpr ErrorOr(ErrorOr &&other) = default;

	constexpr ErrorOr &operator=(ErrorOr &&other)
		requires (!std::is_trivially_move_constructible_v<T> ||
				  !std::is_trivially_move_constructible_v<E>)
	{
		if (this == &other) return *this;

		if (m_is_error) m_error.~E1();
		else m_value.~T();

		if (other.is_error())
			std::construct_at<E>(&m_error, other.release_error());
		else std::construct_at<T>(&m_value, other.release_value());

		m_is_error = other.m_is_error;
		return *this;
	}

	constexpr ErrorOr &operator=(ErrorOr &&other) = default;

	ErrorOr(const ErrorOr &)            = delete;
	ErrorOr &operator=(const ErrorOr &) = delete;

	template <typename T2>
	constexpr ErrorOr(ErrorOr<T2, E1> &&value)
		requires (std::is_convertible_v<T2, T>)
		: m_is_error(value.is_error()) {
		if (value.is_error())
			std::construct_at<E>(&m_error, value.release_error());
		else std::construct_at<T>(&m_value, value.release_value());
	}

	template <typename T2>
		requires std::constructible_from<T2, T> &&
					 (!std::is_same_v<std::remove_cvref_t<T2>, E1>)
	constexpr ErrorOr(T2 &&value)
		: m_value(std::forward<T2>(value)), m_is_error(false) {}

	template <typename E2>
		requires std::constructible_from<E1, std::remove_cvref_t<E2>> &&
					 (!std::same_as<E2, T>)
	constexpr ErrorOr(E2 &&error)
		: m_error(std::forward<E2>(error)), m_is_error(true) {}

#ifdef AK_OS_SERENITY
	constexpr ErrorOr(ErrnoCode code)
		: m_error(Error::from_errno(code)), m_is_error(true) {}
#endif

	constexpr ErrorOr<const T &, const E &> as_ref() const & {
		if (is_error()) return error(); // converts to ErrorOr<const E&>
		return value();                 // converts to ErrorOr<const T&>
	}

	template <typename Self>
	constexpr auto &&value(this Self &&self) {
		VERIFY(!self.is_error());
		return std::forward<Self>(self).m_value;
	}

	template <typename Self>
	constexpr auto &&error(this Self &&self) {
		return std::forward<Self>(self).m_error;
	}

	constexpr bool is_error() const { return m_is_error; }

	constexpr T &&release_value() { return std::move(value()); }

	constexpr E1 &&release_error() { return std::move(error()); }

	~ErrorOr()
		requires (!std::is_destructible_v<T> || !std::is_destructible_v<E1>)
	= delete;

	constexpr ~ErrorOr()
		requires (std::is_trivially_constructible_v<T> &&
				  std::is_trivially_constructible_v<E1>)
	= default;

	constexpr ~ErrorOr() {
		if (m_is_error) m_error.~E1();
		else m_value.~T();
	}

private:
	union {
		T m_value;
		E1 m_error;
	};

	bool m_is_error;
};

template <typename T1, typename E1, typename T2, typename E2>
constexpr auto operator&&(const ErrorOr<T1, E1> &o1,
						  const ErrorOr<T2, E2> &o2) {
	if (!o1.has_value()) return o2;
	if (!o2.has_value()) return o1;
	return detail::EmptyExpected{};
}

template <typename T1, typename E1, typename T2, typename E2>
constexpr auto operator||(const ErrorOr<T1, E1> &o1,
						  const ErrorOr<T2, E2> &o2) {
	if (o1.has_value()) return o1;
	if (o2.has_value()) return o2;
	return detail::EmptyExpected{};
}

template <typename ErrorType>
class [[nodiscard]]
ErrorOr<void, ErrorType> : public ErrorOr<detail::EmptyExpected, ErrorType> {
public:
	using ResultType = void;
	using ErrorOr<detail::EmptyExpected, ErrorType>::ErrorOr;
};

template <typename T, typename E>
class [[nodiscard]] ErrorOr<const T &, const E &> {
public:
	using ValueType = const T &;
	using ErrorType = const E &;

	/// Construct an error view
	constexpr ErrorOr(const E &err) : t(nullptr), e(&err) {}

	/// Construct a value view
	constexpr ErrorOr(const T &val) : t(&val), e(nullptr) {}

	/// Is this holding an error?
	constexpr bool is_error() const noexcept { return e != nullptr; }

	constexpr explicit operator bool() const noexcept { return !is_error(); }

	/// Access the value (precondition: !is_error())
	constexpr const T &value() const {
		VERIFY(!is_error());
		return *t;
	}

	/// Access the error (precondition: is_error())
	constexpr const E &error() const {
		VERIFY(is_error());
		return *e;
	}

private:
	const T *t{nullptr};
	const E *e{nullptr};
};


} // namespace AK
