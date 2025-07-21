#include "db.hpp"

#include "commands/cmd.hpp"
#include "error/error.hpp"
#include "error/error_or.hpp"
#include "primitive/ret.hpp"
#include "utils/split_by_space.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

bool Db::isExpired(key_type::iterator key_iter) const {
	if (key_iter == keys_.end()) return false;

	const auto ttl = key_iter->second.ttl;
	if (ttl.has_value()) return false;

	return std::chrono::system_clock::now() >= *ttl;
}

std::optional<Db::value_type::iterator>
Db::getValIterFromKey(const std::string &key) const {
	const auto key_iter = keys_.find(key);
	if (key_iter == keys_.end()) return std::nullopt;
	return key_iter->second.val_iter;
}

void Db::deleteVal(value_type::iterator val_iter) { values_.erase(val_iter); }

bool Db::deleteKV(const std::string &key) {
	auto key_iter = keys_.find(key);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return false;
	deleteVal(*it);

	keys_.erase(key_iter);
	last_access_.erase(key_iter->first);
	return true;
}

ErrorOr<std::chrono::seconds> Db::cmdTTL(const Command &command) {
	const auto args = split_by_space(command.args());
	if (args.size() != 1) return failed("", std::errc::invalid_argument);

	preCommand({args.front()});

	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx_);
	auto key_iter = keys_.find(args.front());
	if (key_iter == keys_.end())
		return failed("Key iter not found", std::errc::invalid_argument);
	if (!key_iter->second.ttl.has_value())
		return failed("TTL not found", std::errc::invalid_argument);
	auto ret = key_iter->second.ttl.value() - std::chrono::system_clock::now();
	return std::chrono::duration_cast<std::chrono::seconds>(ret);
}

void Db::preCommand(const std::string &key_to_check, bool all_keys) {
	std::vector<key_type::iterator> expired_keys;

	{
		std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx_);
		if (all_keys) {
			for (auto it = keys_.begin(); it != keys_.end(); it++)
				if (isExpired(it)) expired_keys.push_back(it);
		} else {
			auto x = keys_.find(key_to_check);
			if (isExpired(x)) expired_keys.push_back(x);
		}
	}
	if (!expired_keys.empty()) {
		std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
		std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx_);
		for (const auto &key : expired_keys) deleteKV(key->first);
	}
}

void Db::preCommand(const std::vector<std::string> &keys_to_check,
					bool all_keys) {
	std::vector<key_type::iterator> expired_keys;

	{
		std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx_);
		if (all_keys) {
			for (auto it = keys_.begin(); it != keys_.end(); it++)
				if (isExpired(it)) expired_keys.push_back(it);
		} else {
			for (const auto &key : keys_to_check) {
				auto x = keys_.find(key);
				if (isExpired(x)) expired_keys.push_back(x);
			}
		}
	}
	if (!expired_keys.empty()) {
		std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
		std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx_);
		for (const auto &key : expired_keys) deleteKV(key->first);
	}
}

ErrorOr<std::vector<std::string>> Db::cmdKeys(const Command &command) {
	const auto args = split_by_space(command.args());
	if (!args.empty()) return failed("", std::errc::invalid_argument);

	spdlog::info("{}", command);

	preCommand(args, true);
	std::shared_lock<std::shared_timed_mutex> _(keys_mtx_);

	std::vector<std::string> keys;

	for (auto &[key, _] : keys_) keys.push_back(key);
	return keys;
}

ErrorOr<std::chrono::seconds> Db::cmdExpire(const Command &command) {
	const auto args = split_by_space(command.args());
	if (args.size() != 2) return failed("", std::errc::invalid_argument);

	const auto key = args[0];
	const auto ttl = std::chrono::seconds(std::stoull(args[1]));
	preCommand(key);
	return setTTL(key, ttl);
}

ErrorOr<std::chrono::seconds> Db::setTTL(const std::string &key,
										 std::chrono::seconds ttl) {
	if (ttl.count() < 0)
		return failed("invalid time to leave", std::errc::invalid_argument);
	std::unique_lock<std::shared_timed_mutex> lock_key(keys_mtx_);
	auto key_iter = keys_.find(key);
	if (key_iter == keys_.end())
		return failed("TTL not found", std::errc::invalid_argument);
	// TODO: why ttl < 0 again
	if (ttl.count() < 0)
		key_iter->second.ttl = std::chrono::system_clock::now();
	else key_iter->second.ttl = std::chrono::system_clock::now() + ttl;
	return std::chrono::duration_cast<std::chrono::seconds>(
		key_iter->second.ttl.value() - std::chrono::system_clock::now());
}

size_t Db::pushList(const std::string &key,
					const std::vector<std::string> &vals, Where where) {
	std::unique_lock<std::shared_timed_mutex> lock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> lock_val(vals_mtx_);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) {
		// key does not exist, create a new list
		spdlog::debug("Key does not exist, creating new list");
		std::unique_lock<std::shared_timed_mutex> lock_la(last_access_mtx_);
		auto [_, val_iter] =
			writeKV(key,
					where == Where::LBACK
						? std::list<std::string>{vals.begin(), vals.end()}
						: std::list<std::string>{vals.rbegin(), vals.rend()});
		return val_iter->getList().size();
	}

	auto &list = it.value()->getList();
	switch (where) {
	case Where::LFRONT:
		list.insert(list.begin(), vals.rbegin(), vals.rend());
		break;
	case Where::LBACK: list.insert(list.end(), vals.begin(), vals.end()); break;
	default: std::unreachable();
	}

	return list.size();
}

void Db::postAccessCommand(const std::vector<std::string> &keys,
						   bool all_keys) {
	std::unique_lock<std::shared_timed_mutex> lock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> lock_val(vals_mtx_);
	const auto now = std::chrono::system_clock::now();
	auto process   = [this, &now](key_type::iterator key_iter) {
        if (key_iter == this->keys_.end()) return;
        this->last_access_[key_iter->first] = now;
	};
	if (all_keys)
		for (auto it = this->keys_.begin(); it != this->keys_.end(); it++)
			process(it);
	else
		for (auto &key : keys) process(this->keys_.find(key));
}

ErrorOr<size_t> Db::cmdPush(const Command &command, Where where) {
	const auto args = split_by_space(command.args());

	if (args.size() < 2) return failed("", std::errc::invalid_argument);
	const auto key = args[0];
	preCommand(key);
	const std::vector<std::string> vals{args.begin() + 1, args.end()};
	auto ret = pushList(key, vals, where);
	postAccessCommand({key});
	return ret;
}

ErrorOr<Ret> Db::execute(const Command &command) {}

ErrorOr<bool> Db::cmdDel(const Command &command) {
	const auto args = split_by_space(command.args());
	if (args.size() != 1) return failed("", std::errc::invalid_argument);

	spdlog::info("{}", command);
	preCommand(args);
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx_);
	return deleteKV(args.front());
}

ErrorOr<size_t> Db::cmdLlen(const Command &command) {
	const auto args = split_by_space(command.args());
	if (args.size() != 1) return failed("", std::errc::invalid_argument);

	const auto key = args.front();
	preCommand(key);
	auto ret = getListLen(key);
	postAccessCommand({key});
	return ok_or(ret, "No element in list", std::errc::operation_canceled);
}

std::optional<size_t> Db::getListLen(const std::string &key) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx_);
	std::shared_lock<std::shared_timed_mutex> slock_val(vals_mtx_);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return std::nullopt;
	return it.value()->getList().size();
}

std::optional<std::string> Db::cmdPop(const std::string &key, Where where) {
	preCommand(key);
	auto ret = popList(key, where);
	postAccessCommand({key});
	return ret;
}

std::optional<std::string> Db::popList(const std::string &key, Where where) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return std::nullopt;

	std::string ret;
	auto &list = it.value()->getList();
	if (list.empty()) return "";
	switch (where) {
	case Where::LFRONT:
		ret = list.front();
		list.pop_front();
		break;
	case Where::LBACK:
		ret = list.back();
		list.pop_back();
		break;
	default: std::unreachable();
	}
	if (list.empty()) {
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx_);
		deleteKV(key);
	}

	return ret;
}

std::vector<std::string> Db::cmdLrange(const std::string &key, int start,
									   int stop) {
	preCommand(key);
	auto ret = rangeList(key, start, stop);
	postAccessCommand({key});
	return ret;
}

std::vector<std::string> Db::rangeList(const std::string &key, int start,
									   int stop) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return {};

	auto list = (*it)->getList();
	if (start < 0) start += list.size();
	if (start < 0) start = 0;
	if (stop < 0) stop += list.size();
	if (stop >= list.size()) stop = list.size() - 1;
	if (start >= list.size()) return {};
	if (start > stop) return {};

	std::vector<std::string> ret;
	auto it2 = std::next(list.begin(), start);
	for (int i = start; i <= stop && it2 != list.end(); i++, it2++)
		ret.push_back(*it2);

	return ret;
}

size_t Db::insertSet(const std::string &key,
					 const std::vector<std::string> &vals) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) {
		// key does not exist, create a new set
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx_);
		auto [_, val_iter] =
			writeKV(key, std::set<std::string>{vals.begin(), vals.end()});
		return val_iter->getSet().size();
	}

	size_t ret = 0;
	auto &set  = it.value()->getSet();
	ret        = set.size();
	set.insert_range(vals);
	ret = set.size() - ret;

	return ret;
}

size_t Db::cmdSadd(const std::string &key,
				   const std::vector<std::string> &vals) {
	preCommand(key);
	auto ret = insertSet(key, vals);
	postAccessCommand({key});
	return ret;
}

size_t Db::cmdSrem(const std::string &key,
				   const std::vector<std::string> &vals) {
	preCommand(key);
	auto ret = removeSet(key, vals);
	postAccessCommand({key});
	return ret;
}

size_t Db::removeSet(const std::string &key,
					 const std::vector<std::string> &vals) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	auto it = getValIterFromKey(key);
	if (!it.has_value()) return 0;

	size_t ret = 0;
	auto &set  = it.value()->getSet();
	for (auto &val : vals) ret += set.erase(val);
	if (set.empty()) {
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx_);
		deleteKV(key);
	}

	return ret;
}

std::optional<size_t> Db::cmdScard(const std::string &key) {
	preCommand(key);
	auto ret = getSetLen(key);
	postAccessCommand({key});
	return ret;
}

std::optional<size_t> Db::getSetLen(const std::string &key) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return std::nullopt;

	return (*it)->getSet().size();
}

std::vector<std::string> Db::cmdSmembers(const std::string &key) {
	preCommand(key);
	auto ret = getSetMems(key);
	postAccessCommand({key});
	return ret;
}

std::vector<std::string> Db::getSetMems(const std::string &key) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return {};

	const auto &set = (*it)->getSet();
	return {set.begin(), set.end()};
}

std::vector<std::string> Db::cmdSinter(const std::vector<std::string> &keys) {
	preCommand(keys);
	auto ret = getSetInter(keys);
	postAccessCommand(keys);
	return ret;
}

std::vector<std::string> Db::getSetInter(const std::vector<std::string> &keys) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx_);
	std::shared_lock<std::shared_timed_mutex> slock_val(vals_mtx_);
	std::vector<std::set<std::string>> sets;
	for (auto &key : keys) {
		const auto it = getValIterFromKey(key);
		if (it == values_.end()) return {};
		sets.push_back((*it)->getSet());
	}

	if (sets.empty()) return {};

	std::vector<std::string> ret(sets[0].begin(), sets[0].end());
	for (int i = 1; i < sets.size(); i++) {
		std::vector<std::string> tmp;
		std::ranges::set_intersection(ret, sets[i], std::back_inserter(tmp));
		ret = std::move(tmp);
	}

	return {ret.begin(), ret.end()};
}

std::optional<std::string> Db::cmdGet(const std::string &key) {
	preCommand(key);
	auto ret = getStr(key);
	postAccessCommand({key});
	return ret;
}

std::optional<std::string> Db::getStr(const std::string &key) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx_);
	std::shared_lock<std::shared_timed_mutex> slock_val(vals_mtx_);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return std::nullopt;

	return (*it)->getString();
}

void Db::setStr(const std::string &key, const std::string &val) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx_);
	writeKV(key, val);
}

void Db::cmdSet(const std::string &key, const std::string &val) {
	preCommand(key);
	setStr(key, val);
	postAccessCommand({key});
}

ErrorOr<void> Db::cmdFlush(const Command &command) {
	if (!command.args().empty()) return failed("", std::errc::invalid_argument);

	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx_);
	keys_.clear();

	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx_);
	while (!values_.empty()) deleteVal(values_.begin());

	std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx_);
	last_access_.clear();
}
