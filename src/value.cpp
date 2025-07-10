#include "value.hpp"

#include <cassert>

const std::set<std::string> &Value::getSet() const {
	assert(std::holds_alternative<std::set<std::string>>(val_));
	return std::get<std::set<std::string>>(val_);
}

const std::list<std::string> &Value::getList() const {
	assert(std::holds_alternative<std::list<std::string>>(val_));
	return std::get<std::list<std::string>>(val_);
}

const std::string &Value::getString() const {
	assert(std::holds_alternative<std::string>(val_));
	return std::get<std::string>(val_);
}

std::set<std::string> &Value::getSet() {
	assert(std::holds_alternative<std::set<std::string>>(val_));
	return std::get<std::set<std::string>>(val_);
}

std::list<std::string> &Value::getList() {
	assert(std::holds_alternative<std::list<std::string>>(val_));
	return std::get<std::list<std::string>>(val_);
}

std::string &Value::getString() {
	assert(std::holds_alternative<std::string>(val_));
	return std::get<std::string>(val_);
}
