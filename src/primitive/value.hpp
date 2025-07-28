#pragma once

#include <concepts>
#include <list>
#include <set>
#include <string>
#include <variant>
#include <utility>

/**
 * \brief Concept to constrain accepted types for Redis values.
 *
 * Allows:
 * - std::string
 * - std::set<std::string>
 * - std::list<std::string>
 */
template <typename T>
concept isRedisValue = std::convertible_to<T, std::string> ||
					   std::convertible_to<T, std::set<std::string>> ||
					   std::convertible_to<T, std::list<std::string>>;

/**
 * \brief Represents a Redis-stored value, which can be string, set, or list.
 */
class Value {
public:
	/**
	 * \brief Constructs a Value from an accepted Redis type.
	 */
	template <isRedisValue T>
	Value(T &&t) : val_(std::forward<T>(t)) {}

	/** \brief Gets the stored set. */
	[[nodiscard]] const std::set<std::string> &getSet() const;

	/** \brief Gets the stored set (mutable). */
	[[nodiscard]] std::set<std::string> &getSet();

	/** \brief Gets the stored list. */
	[[nodiscard]] const std::list<std::string> &getList() const;

	/** \brief Gets the stored list (mutable). */
	[[nodiscard]] std::list<std::string> &getList();

	/** \brief Gets the stored string. */
	[[nodiscard]] const std::string &getString() const;

	/** \brief Gets the stored string (mutable). */
	[[nodiscard]] std::string &getString();

	/** \brief Returns the raw variant value. */
	[[nodiscard]] const std::variant<std::string, std::set<std::string>,
									 std::list<std::string>> &
	getVal() const;

private:
	// Underlying variant holding the actual data.
	std::variant<std::string, std::set<std::string>, std::list<std::string>>
		val_;
};

/**
 * \brief Formats a Value into a printable string.
 */
std::string format_as(const Value &value);
