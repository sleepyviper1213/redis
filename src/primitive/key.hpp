#pragma once

#include <chrono>
#include <list>
#include <optional>

class Value;

/**
 * \brief Represents a key entry in the database.
 */
struct Key {
	std::string key; ///< The actual key string.
	std::list<Value>::iterator
		val_iter;    ///< Iterator to the value in the value list.
	std::optional<std::chrono::time_point<std::chrono::system_clock>>
		ttl;         ///< Optional expiration time.
};
