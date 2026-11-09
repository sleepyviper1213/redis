#pragma once
#include "core/chrono/elapsed_timer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace redis {

/// What INFO commandstats reports about one command.
struct CommandStat {
	uint64_t calls = 0;
	uint64_t nanos = 0; ///< total time spent running it
};

/**
 * \brief Counters INFO reports about the commands the server has run.
 *
 * Only the thread that executes commands writes, so a counter is bumped with a
 * plain load and store instead of an atomic read-modify-write (no lock prefix on
 * the hot path); any thread may read the totals. The per-command table is
 * not synchronised: read it (INFO does) on the thread that executes commands.
 */
class ServerStats {
public:
	/// A command of the given (upper case, static) name finished after \p took.
	void command_ran(std::string_view name,
					 std::chrono::nanoseconds took) noexcept {
		commands_.store(commands_.load(std::memory_order_relaxed) + 1,
						std::memory_order_relaxed);
		// Never rehashes after the first call of each command: the table holds
		// one entry per command name, which is a small fixed set.
		CommandStat &stat = per_command_[name];
		++stat.calls;
		stat.nanos += static_cast<uint64_t>(took.count());
	}

	[[nodiscard]] uint64_t commands_processed() const noexcept {
		return commands_.load(std::memory_order_relaxed);
	}

	/// The per-command figures, by name.
	[[nodiscard]] std::vector<std::pair<std::string_view, CommandStat>>
	by_command() const {
		std::vector<std::pair<std::string_view, CommandStat>> all(
			per_command_.begin(), per_command_.end());
		std::ranges::sort(all, std::less<>{}, &decltype(all)::value_type::first);
		return all;
	}

	/**
	 * \brief Refresh instantaneous_ops_per_sec: the commands run since the
	 * previous call, over the time since then. Call it on a steady tick.
	 */
	void sample() noexcept {
		const auto now   = commands_processed();
		const auto nanos = sampled_.nsecs_elapsed();
		if (nanos <= 0) return;
		const auto per_sec =
			static_cast<uint64_t>(static_cast<double>(now - sampled_commands_) *
								  1e9 / static_cast<double>(nanos));
		ops_per_sec_.store(per_sec, std::memory_order_relaxed);
		sampled_commands_ = now;
		sampled_.start();
	}

	[[nodiscard]] uint64_t ops_per_sec() const noexcept {
		return ops_per_sec_.load(std::memory_order_relaxed);
	}

	[[nodiscard]] std::chrono::seconds uptime() const noexcept {
		return std::chrono::duration_cast<std::chrono::seconds>(
			started_.duration_elapsed());
	}

private:
	std::atomic<uint64_t> commands_{0};
	std::atomic<uint64_t> ops_per_sec_{0};
	std::unordered_map<std::string_view, CommandStat> per_command_;
	ElapsedTimer started_;
	ElapsedTimer sampled_;           ///< when sample() last ran
	uint64_t sampled_commands_ = 0;  ///< commands_ at that time
};

} // namespace redis
