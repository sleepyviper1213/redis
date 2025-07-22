#pragma once

#include "error.hpp"
#include "error/error_or.hpp"
#include "primitive.hpp"

#include <chrono>
#include <concepts>
#include <expected>
#include <optional>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

// TODO: REname
enum class Where { LBACK = 1, LFRONT = 0 };

class Db {
public:
	using key_type   = std::unordered_map<std::string, Key>;
	using value_type = std::list<Value>;

	ErrorOr<std::vector<std::string>>
	cmdLrange(const std::vector<std::string> &args);

	ErrorOr<size_t> cmdSadd(const std::vector<std::string> &args);


	ErrorOr<size_t> cmdSrem(const std::vector<std::string> &args);

	ErrorOr<size_t> cmdScard(const std::vector<std::string> &args);

	ErrorOr<std::vector<std::string>>
	cmdSinter(const std::vector<std::string> &keys);

	ErrorOr<std::vector<std::string>>
	cmdSmembers(const std::vector<std::string> &args);

	ErrorOr<std::string> cmdGet(const std::vector<std::string> &args);

	ErrorOr<void> cmdSet(const std::vector<std::string> &args);

	ErrorOr<std::vector<std::string>>
	cmdKeys(const std::vector<std::string> &args);

	ErrorOr<size_t> cmdPush(const std::vector<std::string> &args, Where where);

	ErrorOr<std::chrono::seconds> cmdTTL(const std::vector<std::string> &args);

	ErrorOr<void> cmdFlush(const std::vector<std::string> &args);

	ErrorOr<std::chrono::seconds>
	cmdExpire(const std::vector<std::string> &args);

	ErrorOr<bool> cmdDel(const std::vector<std::string> &args);

	ErrorOr<size_t> cmdLlen(const std::vector<std::string> &args);


	ErrorOr<std::string> cmdPop(const std::vector<std::string> &args,
								Where where);

protected:
	[[nodiscard]] bool isExpired(key_type::iterator key_iter) const;

	[[nodiscard]] std::optional<value_type::iterator>
	getValIterFromKey(const std::string &key) const;

	void deleteVal(value_type::iterator val_iter);

	template <isRedisValue T>
	std::pair<key_type::iterator, value_type::iterator>
	writeKV(const std::string &key, const T &val) {
		// TODO
		auto it = getValIterFromKey(key);
		if (it.has_value()) deleteVal(*it);
		Key new_key{.key      = key,
					.val_iter = values_.insert(values_.end(), Value{val})};
		keys_[key]        = new_key;
		last_access_[key] = std::chrono::system_clock::now();
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


	void preCommand(const std::vector<std::string> &keys,
					bool all_keys = false);
	void preCommand(const std::string &key_to_check, bool all_keys = false);

	ErrorOr<long long> getTTL(const std::string &key);


	size_t pushList(const std::string &key,
					const std::vector<std::string> &vals, Where where);

	void postAccessCommand(const std::string &key, bool all_keys = false);
	void postAccessCommand(const std::vector<std::string> &keys,
						   bool all_keys = false);


	std::optional<size_t> getListLen(const std::string &key);

	std::optional<std::string> popList(const std::string &key, Where where);

	size_t insertSet(const std::string &key, std::span<const std::string> vals);
	size_t removeSet(const std::string &key, std::span<const std::string> vals);
	std::optional<size_t> getSetLen(const std::string &key);
	std::optional<std::vector<std::string>> getSetMems(const std::string &key);

	std::vector<std::string> getSetInter(const std::vector<std::string> &keys);


	std::optional<std::string> getStr(const std::string &key);

	void setStr(const std::string &key, const std::string &val);

	ErrorOr<std::chrono::seconds> setTTL(const std::string &key,
										 std::chrono::seconds ttl);

private:
	key_type keys_;

	value_type values_;
	std::unordered_map<std::string, std::chrono::system_clock::time_point>
		last_access_;
	std::shared_timed_mutex vals_mtx_, keys_mtx_, last_access_mtx_;
};
