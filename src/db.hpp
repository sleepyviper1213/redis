#pragma once

#include "commands/cmd.hpp"
#include "error/execute_error.hpp"
#include "error/ttl_error.hpp"
#include "key.hpp"
#include "ret.hpp"
#include "value.hpp"

#include <chrono>
#include <concepts>
#include <expected>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

// TODO: REname
enum class Where { LBACK = 1, LFRONT = 0 };

class Db {
public:
	using key_type   = std::unordered_map<std::string, Key>;
	using value_type = std::list<Value>;

	[[nodiscard]] bool isExpired(key_type::iterator key_iter) const;

	[[nodiscard]] std::optional<value_type::iterator>
	getValIterFromKey(const std::string &key) const;

	void deleteVal(value_type::iterator val_iter);

	template <isRedisValue T>
	std::pair<key_type::iterator, value_type::iterator>
	writeKV(const std::string &key, const T &val) {
		// TODO
		auto it = getValIterFromKey(key);
		if (!it.has_value()) deleteVal(*it);
		Key new_key{.key      = key,
					.val_iter = values_.insert(values_.end(), Value{val})};
		keys_[key]       = new_key;
		last_access[key] = std::chrono::system_clock::now();
		return {keys_.find(key), new_key.val_iter};
	}

	std::optional<value_type::iterator>
	modifyVal(key_type::iterator key_iter,
			  std::invocable<Value &> auto modifier) {
		if (key_iter == keys_.end()) return std::nullopt;
		modifier(*key_iter->second.val_iter);
		return key_iter->second.val_iter;
	}

	bool deleteKV(const std::string &key);

	std::expected<std::chrono::seconds, TTLError>
	cmdTTL(const std::string &key);


	void preCommand(const std::vector<std::string> &keys,
					bool all_keys = false);


	std::expected<long long, TTLError> getTTL(const std::string &key);


	std::vector<std::string> cmdKeys();

	void cmdFlush();

	std::expected<std::chrono::seconds, TTLError>
	cmdExpire(const std::string &key, std::chrono::seconds ttl);

	std::expected<std::chrono::seconds, TTLError>
	setTTL(const std::string &key, std::chrono::seconds ttl);

	size_t pushList(const std::string &key,
					const std::vector<std::string> &vals, Where where);

	void postAccessCommand(const std::vector<std::string> &keys,
						   bool all_keys = false);

	size_t cmdPush(const std::string &key, const std::vector<std::string> &vals,
				   Where where);

	std::expected<Ret, ExecuteError> execute(const Command &command);

	bool cmdDel(const std::string &key);

	std::optional<size_t> cmdLlen(const std::string &key);

	std::optional<size_t> getListLen(const std::string &key);

	std::optional<std::string> cmdPop(const std::string &key, Where where);

	std::optional<std::string> popList(const std::string &key, Where where);

	std::vector<std::string> cmdLrange(const std::string &key, int start,
									   int stop);

	std::vector<std::string> rangeList(const std::string &key, int start,
									   int stop);

	size_t cmdSadd(const std::string &key,
				   const std::vector<std::string> &vals);

	size_t insertSet(const std::string &key,
					 const std::vector<std::string> &vals);

	size_t cmdSrem(const std::string &key,
				   const std::vector<std::string> &vals);
	size_t removeSet(const std::string &key,
					 const std::vector<std::string> &vals);
	std::optional<size_t> cmdScard(const std::string &key);
	std::optional<size_t> getSetLen(const std::string &key);
	std::vector<std::string> cmdSmembers(const std::string &key);
	std::vector<std::string> getSetMems(const std::string &key);

	std::vector<std::string> cmdSinter(const std::vector<std::string> &keys);

	std::vector<std::string> getSetInter(const std::vector<std::string> &keys);

	std::optional<std::string> cmdGet(const std::string &key);

	std::optional<std::string> getStr(const std::string &key);

	void setStr(const std::string &key, const std::string &val);

	void cmdSet(const std::string &key, const std::string &val);

private:
	key_type keys_;

	value_type values_;
	std::unordered_map<std::string, std::chrono::system_clock::time_point>
		last_access;
	std::shared_timed_mutex vals_mtx, keys_mtx, last_access_mtx;
};
