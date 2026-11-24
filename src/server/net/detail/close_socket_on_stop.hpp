#pragma once
#include <boost/asio/ip/tcp.hpp>
#include <stdexec/stop_token.hpp>

#include <mutex>

/**
 * \brief Closes a socket once a stop is requested, so that every operation
 * pending on it completes.
 *
 * A stop request alone asks Asio to cancel them, and Windows does not complete
 * a cancelled send to a peer that is not reading: draining the sessions at
 * shutdown would wait for it forever. On a closed socket every pending
 * operation completes. The close is posted to the socket's executor (its
 * strand, when it has one), so it never overlaps the session's own handlers,
 * and it does nothing once the session has ended.
 */
namespace redis::detail {
class CloseSocketOnStop {
public:
	CloseSocketOnStop()                                     = default;
	CloseSocketOnStop(CloseSocketOnStop &&) noexcept        = default;
	CloseSocketOnStop(const CloseSocketOnStop &)            = delete;
	CloseSocketOnStop &operator=(const CloseSocketOnStop &) = delete;
	CloseSocketOnStop &operator=(CloseSocketOnStop &&)      = delete;
	/// Forgets the socket: a close still queued finds nothing to close.
	~CloseSocketOnStop();

	/// From now on, a stop request on \p stop closes \p socket, which must
	/// not move any more. Called once.
	void arm(boost::asio::ip::tcp::socket &socket,
			 stdexec::inplace_stop_token stop);

private:
	/// What the queued close and the destructor share: the socket, until
	/// the session ends.
	struct Target {
		std::mutex mutex;
		boost::asio::ip::tcp::socket *socket = nullptr;
	};

	/// Runs on the socket's executor.
	static void close(Target &target);

	/// The stop callback: queues the close.
	struct Request {
		std::shared_ptr<Target> target;
		boost::asio::any_io_executor executor;
		void operator()() const noexcept;
	};

	std::shared_ptr<Target> target_;
	std::unique_ptr<stdexec::inplace_stop_callback<Request>> callback_;
};
} // namespace redis::detail