#include "server/memory/database.hpp"

#include "core/error.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>

namespace redis {

Database::Database(int id) : id_(id) {}

bool Database::contains_key(const std::string &key) {
	erase_expired_key(key);
	return dict_.contains(key);
}

const Database::value_type &
Database::get_value_by_key(const Database::key_type &key) const {
	return dict_.at(key);
}

Database::value_type &
Database::get_value_by_key(const Database::key_type &key) {
	return dict_.at(key);
}

std::expected<DeadlineTimer, Database::ExpireError>
Database::deadline_timer_of(const std::string &key) {
	if (!dict_.contains(key)) FAILED(ExpireError::NO_SUCH_KEY);

	const auto expIt = expires_.find(key);
	if (expIt == expires_.end()) FAILED(ExpireError::NO_ASSOCIATE_EXPIRY);

	if (expIt->second.has_expired()) {
		erase_key(key);
		FAILED(ExpireError::NO_SUCH_KEY);
	}
	return expIt->second;
}

int Database::get_id() const { return id_; }

void Database::save() const { logger_->trace("DB saved"); }

size_t Database::erase_key(const key_type &key) {
	(void)expires_.erase(key);
	return dict_.erase(key);
}

void Database::set_value(const key_type &key, const value_type &value) {

	const auto [_, y] = dict_.insert_or_assign(key, value);
	if (y) logger_->trace("Insert key");
	else logger_->trace("Update key");
}

void Database::set_expire(const key_type &key, const DeadlineTimer &deadline) {
	expires_[key] = deadline;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	logger_->trace("Expire at : {}",
				   fmt::localtime(DeadlineTimer::clock_type::to_time_t(
					   deadline.deadline())));
#pragma GCC diagnostic pop
}

void Database::erase_expired_key(const std::string &key) {
	auto expIt = expires_.find(key);
	if (expIt == expires_.end()) return;

	if (expIt->second.has_expired()) {
		erase_key(key);
		logger_->trace("Erased key: {}", key);
	}
}
} // namespace redis

std::string_view as_error_string(redis::Database::ExpireError c) {
	std::string_view name;
	switch (c) {
		using enum redis::Database::ExpireError;
	case NO_SUCH_KEY: name = "-2"; break;
	case NO_ASSOCIATE_EXPIRY: name = "-1"; break;
	}
	return name;
}

