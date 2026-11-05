#pragma once

#include <atomic>
#include <cassert>
#include <thread>

namespace redis {

#ifdef NDEBUG

/// Compiles to nothing: see the debug version.
class ThreadOwner {
public:
	void claim() noexcept {}

	void release() noexcept {}

	void check() const noexcept {}
};

#else

/**
 * \brief Debug-only check that one thread owns some unsynchronized state.
 *
 * The first thread to claim() becomes the owner. From then on check() asserts
 * that it runs on that thread, and so does a later claim(). Nothing is checked
 * before the first claim, so setup code and tests that never claim are
 * unaffected. Copying or moving the guarded state does not carry ownership.
 */
class ThreadOwner {
public:
	ThreadOwner() = default;

	ThreadOwner(const ThreadOwner &) noexcept {}

	ThreadOwner(ThreadOwner &&) noexcept {}

	ThreadOwner &operator=(const ThreadOwner &) noexcept { return *this; }

	ThreadOwner &operator=(ThreadOwner &&) noexcept { return *this; }

	~ThreadOwner() = default;

	void claim() noexcept {
		auto expected = std::thread::id{};
		if (!owner_.compare_exchange_strong(expected,
											std::this_thread::get_id()))
			assert(expected == std::this_thread::get_id() &&
				   "state claimed by one thread is used from another");
	}

	/// Forget the owner, so the next claim() may come from another thread.
	/// For state set up on one thread (loading at startup) and then handed to
	/// the thread that will run on it.
	void release() noexcept { owner_.store(std::thread::id{}); }

	void check() const noexcept {
		const auto owner = owner_.load();
		assert((owner == std::thread::id{} ||
				owner == std::this_thread::get_id()) &&
			   "state owned by one thread is used from another");
	}

private:
	std::atomic<std::thread::id> owner_{};
};

#endif

} // namespace redis
