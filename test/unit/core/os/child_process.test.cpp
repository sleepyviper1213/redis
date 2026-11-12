// fork_and_run() from a multithreaded process: what the child inherits, and
// that it can allocate while other threads hold the allocator busy.
#ifndef _WIN32
#include "core/os/child_process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace os = redis::os;

/// Reap \p child, killing it if it has not ended within \p limit: a child
/// that deadlocked must fail the test, not hang it.
os::ChildResult reap_within(const os::Child &child, std::chrono::seconds limit) {
	const auto deadline = std::chrono::steady_clock::now() + limit;
	while (std::chrono::steady_clock::now() < deadline) {
		if (auto done = os::reap(child, false)) return *done;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	os::kill_child(child.pid());
	(void)os::reap(child, true);
	return {.success = false, .error = "the child hung"};
}

bool is_open(int fd) { return ::fcntl(fd, F_GETFD) != -1; }
} // namespace

TEST_CASE("a child keeps the descriptors it is told to, and no others",
		  "[fork][os]") {
	const int kept    = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
	const int dropped = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
	REQUIRE(kept >= 0);
	REQUIRE(dropped >= 0);
	os::keep_open_in_children(kept);

	auto child = os::fork_and_run([&]() -> std::expected<void, std::string> {
		if (!is_open(kept)) return std::unexpected("the kept descriptor was closed");
		if (is_open(dropped))
			return std::unexpected("an inherited descriptor stayed open");
		return {};
	});
	REQUIRE(child);
	const auto result = reap_within(*child, std::chrono::seconds(10));
	CHECK(result.error == "");
	CHECK(result.success);
	// The parent's copies are untouched.
	CHECK(is_open(kept));
	CHECK(is_open(dropped));
	::close(kept);
	::close(dropped);
}

TEST_CASE("a child allocates freely though other threads were allocating at "
		  "the fork",
		  "[fork][os]") {
	// The child has only the forking thread; if another one held an
	// allocator lock at the fork, an allocator without fork handlers would
	// leave it locked forever in the child. glibc's and macOS' have them;
	// this guards a future custom allocator.
	std::atomic<bool> stop{false};
	std::vector<std::thread> busy;
	for (int i = 0; i < 4; ++i)
		busy.emplace_back([&stop] {
			while (!stop.load(std::memory_order_relaxed)) {
				std::vector<std::unique_ptr<std::string>> junk;
				for (int n = 0; n < 64; ++n)
					junk.push_back(std::make_unique<std::string>(n * 37, 'x'));
			}
		});

	std::optional<os::ChildResult> failure;
	for (int round = 0; round < 20 && !failure; ++round) {
		auto child = os::fork_and_run([]() -> std::expected<void, std::string> {
			std::vector<std::string> work;
			for (int n = 0; n < 10000; ++n) work.emplace_back(n % 300, 'y');
			return {};
		});
		REQUIRE(child);
		if (auto result = reap_within(*child, std::chrono::seconds(10));
			!result.success)
			failure = std::move(result);
	}
	stop = true;
	for (auto &t : busy) t.join();
	if (failure) FAIL("a child failed: " << failure->error);
}
#endif
