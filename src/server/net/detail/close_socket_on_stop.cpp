#include "close_socket_on_stop.hpp"

#include <boost/asio/post.hpp>
#include <boost/system/error_code.hpp>
#include <stdexec/stop_token.hpp>

#include <memory>

namespace redis::detail {
CloseSocketOnStop::~CloseSocketOnStop() {
	// No new close is queued after this, and one already queued finds no
	// socket.
	callback_.reset();
	if (!target_) return;
	const std::scoped_lock lock(target_->mutex);
	target_->socket = nullptr;
}

void CloseSocketOnStop::arm(boost::asio::ip::tcp::socket &socket,
					  stdexec::inplace_stop_token stop) {
	target_         = std::make_shared<Target>();
	target_->socket = &socket;
	// Calls the request at once if the stop was already requested.
	callback_ = std::make_unique<stdexec::inplace_stop_callback<Request>>(
		stop,
		Request{.target = target_, .executor = socket.get_executor()});
}

void CloseSocketOnStop::close(Target &target) {
	const std::scoped_lock lock(target.mutex);
	if (target.socket == nullptr) return; // the session is over
	boost::system::error_code ignored;
	target.socket->close(ignored);
}

// NOLINTNEXTLINE(bugprone-exception-escape): posting fails only out of memory.
void CloseSocketOnStop::Request::operator()() const noexcept {
	// Not closed here: the stop may be requested on any thread, and the
	// socket is the session's executor's alone.
	boost::asio::post(executor, [target = target] { close(*target); });
}
} // namespace redis::detail