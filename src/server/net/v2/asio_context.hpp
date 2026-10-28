#pragma once
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/post.hpp>
#include <exec/thread_pool_base.hpp>
#include <stdexec/execution.hpp>

#include <cstdint>
#include <utility>

namespace redis {
namespace net = boost::asio;

/**
 * \brief An Asio executor (a strand, an io_context) as a stdexec scheduler.
 *
 * stdexec ships the other direction (Asio operations as senders) but no
 * scheduler over an Asio executor. This one posts each scheduled task to the
 * executor, so `continues_on(context.get_scheduler())` moves a sender chain
 * back onto, for example, a session's strand after it ran on another thread.
 *
 * Must outlive every sender scheduled on it. Follows the shape of stdexec's
 * own asio_thread_pool.
 */
class AsioContext : public exec::thread_pool_base<AsioContext> {
public:
	explicit AsioContext(net::any_io_executor executor)
		: executor_(std::move(executor)) {}

	[[nodiscard]] std::uint32_t available_parallelism() const noexcept {
		return 1;
	}

private:
	friend struct ::exec::thread_pool_base<AsioContext>;

	template <class PoolType, class Receiver>
	friend struct exec::_pool_::opstate;

	[[nodiscard]] static constexpr auto forward_progress_guarantee()
		-> stdexec::forward_progress_guarantee {
		return stdexec::forward_progress_guarantee::parallel;
	}

	void enqueue(exec::_pool_::task_base *task, std::uint32_t tid = 0) noexcept {
		net::post(executor_, [task, tid] { task->execute_(task, tid); });
	}

	net::any_io_executor executor_;
};

} // namespace redis
