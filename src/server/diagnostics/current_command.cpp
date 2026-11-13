#include "current_command.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace redis::diagnostics {
namespace {

/// Longer than any command name; a longer (unknown) name is cut short.
constexpr std::size_t MAX_NAME = 32;

// Globals rather than thread_locals: the crash may happen on another thread
// than the one running the command (an I/O thread, the logger's worker).
// Relaxed atomics: a crash handler needs no ordering, only no torn sizes.
std::array<char, MAX_NAME> name_buf{};
std::atomic<std::size_t> name_len{0};
std::atomic<std::size_t> arg_count{0};

// For the watchdog, which reads while commands run, so name_buf (plain chars)
// is off limits to it. The name is kept as a pointer instead of a copy.
std::atomic<const char *> name_ptr{nullptr};
/// When the command started, in TickClock ticks; 0 when none runs.
std::atomic<TickClock::rep> started_ticks{0};
std::atomic<std::uint64_t> generation{0};

} // namespace

void set_current_command(std::string_view name, std::size_t argc,
						 TickClock::time_point started) noexcept {
	const auto len = std::min(name.size(), MAX_NAME);
	std::copy_n(name.data(), len, name_buf.data());
	name_ptr.store(name.data(), std::memory_order_relaxed);
	started_ticks.store(started.time_since_epoch().count(),
						std::memory_order_relaxed);
	arg_count.store(argc, std::memory_order_relaxed);
	// One writer, so a plain store does: no read-modify-write needed.
	generation.store(generation.load(std::memory_order_relaxed) + 1,
					 std::memory_order_relaxed);
	name_len.store(len, std::memory_order_release);
}

void clear_current_command() noexcept {
	started_ticks.store(0, std::memory_order_relaxed);
	name_len.store(0, std::memory_order_release);
}

CurrentCommand current_command() noexcept {
	const auto len = name_len.load(std::memory_order_acquire);
	return {.name = {name_buf.data(), len},
			.argc = arg_count.load(std::memory_order_relaxed)};
}

std::optional<Heartbeat> heartbeat() noexcept {
	const auto len   = name_len.load(std::memory_order_acquire);
	const auto ticks = started_ticks.load(std::memory_order_relaxed);
	const char *name = name_ptr.load(std::memory_order_relaxed);
	if (len == 0 || ticks == 0 || name == nullptr) return std::nullopt;
	return Heartbeat{
		.generation = generation.load(std::memory_order_relaxed),
		.started    = TickClock::time_point{TickClock::duration{ticks}},
		.command    = {.name = {name, len},
					   .argc = arg_count.load(std::memory_order_relaxed)},
	};
}

} // namespace redis::diagnostics
