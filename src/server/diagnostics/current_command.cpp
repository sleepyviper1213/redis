#include "current_command.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
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

} // namespace

void set_current_command(std::string_view name, std::size_t argc) noexcept {
	const auto len = std::min(name.size(), MAX_NAME);
	std::copy_n(name.data(), len, name_buf.data());
	arg_count.store(argc, std::memory_order_relaxed);
	name_len.store(len, std::memory_order_release);
}

void clear_current_command() noexcept {
	name_len.store(0, std::memory_order_release);
}

CurrentCommand current_command() noexcept {
	const auto len = name_len.load(std::memory_order_acquire);
	return {.name = {name_buf.data(), len},
			.argc = arg_count.load(std::memory_order_relaxed)};
}

} // namespace redis::diagnostics
