#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace redis {
/**
 * @brief Represents an error consisting of a context message and an error code.
 *
 * This class is designed to avoid common pitfalls with temporary strings,
 * such as accidentally binding a `std::string_view` to a temporary
 * `std::string`.
 */
class [[nodiscard]] SimpleMessage {
public:
	/**
	 * @brief Constructs an Error from a string view and error code.
	 *
	 * @param message A descriptive error message (must outlive the Error).
	 * @param code An error code from `std::errc`.
	 */
	SimpleMessage(std::string_view message, std::errc code);

	/**
	 * @brief Deleted constructor to prevent accidental binding to temporaries
	 * like std::string.
	 *
	 * Prevents:
	 * @code
	 * Error(fmt::format("..."), std::errc::invalid_argument); // ERROR
	 * @endcode
	 *
	 * Such usage would bind `std::string_view` to a temporary of std::string,
	 * which is almost always dynamically allocated, resulting in a dangling
	 * pointer.
	 */
	template <typename T>
		requires std::is_same_v<std::remove_cvref_t<T>, std::string>
	SimpleMessage(T message, std::errc code) = delete;

	/**
	 * @brief Default three-way comparison operator.
	 */
	auto operator<=>(const SimpleMessage &other) const = default;

	/**
	 * @brief Gets the error code.
	 * @return The stored `std::errc` error code.
	 */
	[[nodiscard]] std::errc code() const;

	/**
	 * @brief Gets the context message.
	 * @return The stored error message as a `std::string_view`.
	 */
	[[nodiscard]] std::string_view context_message() const;

	/**
	 * @brief Checks whether the error message contains a given substring.
	 * @param msg The substring to check for.
	 * @return True if `msg` is a substring of the error message.
	 */
	[[nodiscard]] bool contains_error_message(std::string_view msg) const;

private:
	std::string_view
		context_message_; ///< View of the error message (must be long-lived).
	std::errc code_;      ///< Error code value.
};

/**
 * @brief Formats the Error for output via fmtlib
 * @param e The error to format.
 * @return A string representation of the error.
 */
std::string format_as(const SimpleMessage &e);

template <typename T>
using ErrorOr = std::expected<T, SimpleMessage>;
} // namespace redis
