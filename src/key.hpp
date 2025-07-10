#pragma once


#include <chrono>
#include <list>
#include <optional>

class Value;

struct Key {
	std::string key;
	std::list<Value>::iterator val_iter;
	std::optional<std::chrono::time_point<std::chrono::system_clock>> ttl;
};
