#pragma once
#include "interface.hpp"

#include <string>
#include <unordered_map>

class StdHashMap : public OptimizedHashMap<std::string, std::string> {
private:
	std::unordered_map<std::string, std::string> map_;

public:
	bool insert(const std::string &key, const std::string &value) override {
		return map_.insert_or_assign(key, value).second;
	}

	std::string *find(const std::string &key) override {
		auto it = map_.find(key);
		return it != map_.end() ? &it->second : nullptr;
	}

	void reserve(size_t size) override { map_.reserve(size); }
};
