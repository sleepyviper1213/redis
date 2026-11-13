#include "executor.hpp"

#include <stdexec/execution.hpp>

#include <cassert>
#include <concepts>
#include <optional>
#include <thread>
#include <tuple>

namespace redis {

Executor::Executor(bool threaded) {
	if (!threaded) return;
	pool_.emplace(1);
#ifndef NDEBUG
	// Learn the worker's id by asking it, so stop() can tell its own thread.
	// NOLINTBEGIN(bugprone-unchecked-optional-access): pool_ was emplaced above
	const std::optional<std::tuple<std::thread::id>> reported =
		stdexec::sync_wait(
			stdexec::schedule(pool_->get_scheduler()) |
			stdexec::then([] { return std::this_thread::get_id(); }));
	pool_thread_ = std::get<0>(reported.value());
	// NOLINTEND(bugprone-unchecked-optional-access)
#endif
}

Executor::~Executor() { stop(); }

void Executor::stop() {
	// Called on the worker itself, this would wait for a task that only that
	// worker can run. It also ends ~Executor, so the last owner of the Services
	// must never be released from a task running on the keyspace thread.
	assert(std::this_thread::get_id() != pool_thread_ &&
		   "Executor::stop() on the keyspace thread would wait for itself");
	// The pool runs tasks in order on one thread, so once an empty task has
	// run, every earlier one has too. The pool itself is joined on destruction.
	if (pool_) stdexec::sync_wait(stdexec::schedule(pool_->get_scheduler()));
}

} // namespace redis
