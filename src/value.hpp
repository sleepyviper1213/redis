#pragma once

#include <concepts>
#include <list>
#include <set>
#include <string>
#include <variant>

template <typename T>
concept isRedisValue = std::convertible_to<T, std::string> ||
					   std::same_as<T, std::set<std::string>> ||
					   std::same_as<T, std::list<std::string>>;

class Value {
public:
	template <isRedisValue T>
	Value(T t) : val_(std::move(t)) {}

	[[nodiscard]] const std::set<std::string> &getSet() const;
	[[nodiscard]] std::set<std::string> &getSet();

	[[nodiscard]] const std::list<std::string> &getList() const;
	[[nodiscard]] std::list<std::string> &getList();

	[[nodiscard]] const std::string &getString() const;
	[[nodiscard]] std::string &getString();

private:
	std::variant<std::string, std::set<std::string>, std::list<std::string>>
		val_;
};
