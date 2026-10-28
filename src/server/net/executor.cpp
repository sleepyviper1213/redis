#include "executor.hpp"

namespace redis {

Executor::Executor(bool threaded) {
	if (threaded) pool_.emplace(1);
}

Executor::~Executor() {
	stop();
}

void Executor::stop() {
	// The pool runs tasks in order on one thread, so once an empty task has
	// run, every earlier one has too. The pool itself is joined on destruction.
	if (pool_) stdexec::sync_wait(stdexec::schedule(pool_->get_scheduler()));
}

} // namespace redis
