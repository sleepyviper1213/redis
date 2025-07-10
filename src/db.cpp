#include "db.hpp"

#include "utility.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <shared_mutex>
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
	last_access.erase(key_iter->first);
	return true;
}

std::expected<std::chrono::seconds, TTLError>
Db::cmdTTL(const std::string &key) {
	preCommand({key});

	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx);
	auto key_iter = keys_.find(key);
	if (key_iter == keys_.end())
		return std::unexpected(TTLError::KEY_ITER_NOTFOUND);
	if (!key_iter->second.ttl.has_value())
		return std::unexpected(TTLError::TTL_NOT_FOUND);
	auto ret = key_iter->second.ttl.value() - std::chrono::system_clock::now();
	return std::chrono::duration_cast<std::chrono::seconds>(ret);
}

void Db::preCommand(const std::vector<std::string> &keys_to_check,
					bool all_keys) {
	std::vector<key_type::iterator> expired_keys;

	{
		std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx);
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
		std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx);
		std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx);
		for (const auto &key : expired_keys) deleteKV(key->first);
	}
}

std::vector<std::string> Db::cmdKeys() {
	preCommand({}, true);
	std::shared_lock<std::shared_timed_mutex> _(keys_mtx);

	std::vector<std::string> keys;

	for (auto &[key, _] : keys_) keys.push_back(key);
	return keys;
}

std::expected<std::chrono::seconds, TTLError>
Db::cmdExpire(const std::string &key, std::chrono::seconds ttl) {
	preCommand({key});
	return setTTL(key, ttl);
}

std::expected<std::chrono::seconds, TTLError>
Db::setTTL(const std::string &key, std::chrono::seconds ttl) {
	if (ttl.count() < 0) return std::unexpected(TTLError::INVALID_ARGUMENT);
	std::unique_lock<std::shared_timed_mutex> lock_key(keys_mtx);
	auto key_iter = keys_.find(key);
	if (key_iter == keys_.end())
		return std::unexpected(TTLError::KEY_ITER_NOTFOUND);
	// TODO: why ttl < 0 again
	if (ttl.count() < 0)
		key_iter->second.ttl = std::chrono::system_clock::now();
	else key_iter->second.ttl = std::chrono::system_clock::now() + ttl;
	return std::chrono::duration_cast<std::chrono::seconds>(
		key_iter->second.ttl.value() - std::chrono::system_clock::now());
}

size_t Db::pushList(const std::string &key,
					const std::vector<std::string> &vals, Where where) {
	std::unique_lock<std::shared_timed_mutex> lock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> lock_val(vals_mtx);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) {
		// key does not exist, create a new list
		spdlog::debug("Key does not exist, creating new list");
		std::unique_lock<std::shared_timed_mutex> lock_la(last_access_mtx);
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
	std::unique_lock<std::shared_timed_mutex> lock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> lock_val(vals_mtx);
	const auto now = std::chrono::system_clock::now();
	auto process   = [this, &now](key_type::iterator key_iter) {
        if (key_iter == this->keys_.end()) return;
        this->last_access[key_iter->first] = now;
	};
	if (all_keys)
		for (auto it = this->keys_.begin(); it != this->keys_.end(); it++)
			process(it);
	else
		for (auto &key : keys) process(this->keys_.find(key));
}

size_t Db::cmdPush(const std::string &key, const std::vector<std::string> &vals,
				   Where where) {
	preCommand({key});
	auto ret = pushList(key, vals, where);
	postAccessCommand({key});
	return ret;
}

std::expected<Ret, ExecuteError> Db::execute(const Command &command) {
	const auto args = split_by_space(command.args());
	spdlog::info("{}", command);
	Ret ret;
	switch (command.type()) {
		using enum CommandType;
	case GKEYS:
		if (!args.empty())
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdKeys();
		break;
	case GDEL:
		if (args.size() != 1)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdDel(args.front());
		break;
	case GFLUSHDB:
		if (!args.empty())
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		cmdFlush();
		break;
	case GTTL:
		if (args.size() != 1)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdTTL(args.front()).value_or(std::chrono::seconds(0));
		break;

	case GEXPIRE:
		if (args.size() != 2)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdExpire(args[0], std::chrono::seconds(std::stoull(args[1])))
				  .value_or(std::chrono::seconds(0));
		break;
	case RPUSH:
		if (args.size() < 2)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdPush(args[0], {args.begin() + 1, args.end()}, Where::LBACK);
		break;
	case LPUSH:
		if (args.size() < 2)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdPush(args[0], {args.begin() + 1, args.end()}, Where::LFRONT);
		break;
	case LLEN:
		if (args.size() != 1)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdLlen(args.front()).value_or(0);
		break;
	case LPOP:
		if (args.size() != 1)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdPop(args[0], Where::LFRONT).value_or("Failed");
		break;
	case RPOP:
		if (args.size() != 1)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdPop(args[0], Where::LBACK).value_or("Failed");
		break;
	case LRANGE:
		if (args.size() != 3)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdLrange(args[0], std::stoi(args[1]), std::stoi(args[2]));
		break;

	case SADD:
		if (args.size() < 2)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdSadd(args[0], {args.begin() + 1, args.end()});
		break;
	case SREM:
		if (args.size() < 2)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdSrem(args[0], {args.begin() + 1, args.end()});
		break;
	case SCARD:
		if (args.size() != 1)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);

		ret = cmdScard(args[0]).value_or(0);
		break;
	case SMEMBERS:
		if (args.size() != 1)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);
		ret = cmdSmembers(args[0]);
		break;
	case SINTER:
		if (args.size() < 2)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);
		ret = cmdSinter({args.begin(), args.end()});
		break;
	case SGET: {
		if (args.size() != 1)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);
		ret = cmdGet(args[0]).value_or("Failed");
		break;
	}
	case SSET:
		if (args.size() != 2)
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);
		cmdSet(args[0], args[1]);
		break;
	default: return std::unexpected(ExecuteError::UNKNOWN);
	}
	return ret;
}

bool Db::cmdDel(const std::string &key) {
	preCommand({key});
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx);
	return deleteKV(key);
}

std::optional<size_t> Db::cmdLlen(const std::string &key) {
	preCommand({key});
	auto ret = getListLen(key);
	postAccessCommand({key});
	return ret;
}

std::optional<size_t> Db::getListLen(const std::string &key) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx);
	std::shared_lock<std::shared_timed_mutex> slock_val(vals_mtx);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return std::nullopt;
	return it.value()->getList().size();
}

std::optional<std::string> Db::cmdPop(const std::string &key, Where where) {
	preCommand({key});
	auto ret = popList(key, where);
	postAccessCommand({key});
	return ret;
}

std::optional<std::string> Db::popList(const std::string &key, Where where) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
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
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx);
		deleteKV(key);
	}

	return ret;
}

std::vector<std::string> Db::cmdLrange(const std::string &key, int start,
									   int stop) {
	preCommand({key});
	auto ret = rangeList(key, start, stop);
	postAccessCommand({key});
	return ret;
}

std::vector<std::string> Db::rangeList(const std::string &key, int start,
									   int stop) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
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
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) {
		// key does not exist, create a new set
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx);
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
	preCommand({key});
	auto ret = insertSet(key, vals);
	postAccessCommand({key});
	return ret;
}

size_t Db::cmdSrem(const std::string &key,
				   const std::vector<std::string> &vals) {
	preCommand({key});
	auto ret = removeSet(key, vals);
	postAccessCommand({key});
	return ret;
}

size_t Db::removeSet(const std::string &key,
					 const std::vector<std::string> &vals) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
	auto it = getValIterFromKey(key);
	if (!it.has_value()) return 0;

	size_t ret = 0;
	auto &set  = it.value()->getSet();
	for (auto &val : vals) ret += set.erase(val);
	if (set.empty()) {
		std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx);
		deleteKV(key);
	}

	return ret;
}

std::optional<size_t> Db::cmdScard(const std::string &key) {
	preCommand({key});
	auto ret = getSetLen(key);
	postAccessCommand({key});
	return ret;
}

std::optional<size_t> Db::getSetLen(const std::string &key) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return std::nullopt;

	return (*it)->getSet().size();
}

std::vector<std::string> Db::cmdSmembers(const std::string &key) {
	preCommand({key});
	auto ret = getSetMems(key);
	postAccessCommand({key});
	return ret;
}

std::vector<std::string> Db::getSetMems(const std::string &key) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
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
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx);
	std::shared_lock<std::shared_timed_mutex> slock_val(vals_mtx);
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
	preCommand({key});
	auto ret = getStr(key);
	postAccessCommand({key});
	return ret;
}

std::optional<std::string> Db::getStr(const std::string &key) {
	std::shared_lock<std::shared_timed_mutex> slock_key(keys_mtx);
	std::shared_lock<std::shared_timed_mutex> slock_val(vals_mtx);
	const auto it = getValIterFromKey(key);
	if (!it.has_value()) return std::nullopt;

	return (*it)->getString();
}

void Db::setStr(const std::string &key, const std::string &val) {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
	std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx);
	writeKV(key, val);
}

void Db::cmdSet(const std::string &key, const std::string &val) {
	preCommand({key});
	setStr(key, val);
	postAccessCommand({key});
}

void Db::cmdFlush() {
	std::unique_lock<std::shared_timed_mutex> ulock_key(keys_mtx);
	keys_.clear();

	std::unique_lock<std::shared_timed_mutex> ulock_val(vals_mtx);
	while (!values_.empty()) deleteVal(values_.begin());

	std::unique_lock<std::shared_timed_mutex> ulock_la(last_access_mtx);
	last_access.clear();
}
