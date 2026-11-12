#pragma once
/**
 * \file
 * Support for the network tests: an io_context that runs while it lives, a
 * client of the server under test over a real loopback connection, and a wait
 * with a deadline. Every wait here has one, so a server that stops answering
 * fails the test instead of hanging the run.
 */

#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <fmt/format.h>
#include <stdexec/execution.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace redis::test {

inline constexpr std::string_view PING = "*1\r\n$4\r\nPING\r\n";
inline constexpr std::string_view PONG = "+PONG\r\n";

/**
 * \brief An io_context run by threads of its own for as long as this lives.
 *
 * Destroying it lets the threads return once the context runs out of work,
 * then joins them, so whatever runs on the context must be finished or
 * stopped by then.
 */
class RunningContext {
public:
	explicit RunningContext(unsigned threads = 1)
		: work_(boost::asio::make_work_guard(io_)) {
		threads_.reserve(threads);
		for (unsigned i = 0; i < threads; ++i)
			threads_.emplace_back([this] { io_.run(); });
	}

	RunningContext(const RunningContext &)            = delete;
	RunningContext &operator=(const RunningContext &) = delete;

	~RunningContext() { work_.reset(); } // threads_ join as they go

	[[nodiscard]] boost::asio::io_context &io() noexcept { return io_; }

	[[nodiscard]] boost::asio::any_io_executor executor() noexcept {
		return io_.get_executor();
	}

private:
	boost::asio::io_context io_;
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
		work_;
	std::vector<std::jthread> threads_;
};

/// How a client's wait for the server ended.
enum class Heard { Data, Closed, Silence };

/**
 * \brief A client of the server under test, on 127.0.0.1.
 *
 * Sends block; every read has a deadline. Its handlers run on the io_context
 * it was made with, which must be running (a RunningContext).
 */
class Client {
public:
	/// Connects to \p port; throws if the connection is refused.
	Client(boost::asio::io_context &io, boost::asio::ip::port_type port)
		: socket_(io) {
		socket_.connect({boost::asio::ip::make_address("127.0.0.1"), port});
	}

	void send(std::string_view bytes) {
		boost::asio::write(socket_,
						   boost::asio::buffer(bytes.data(), bytes.size()));
	}

	/// Waits up to \p limit for the server to send something (into \p data)
	/// or hang up.
	Heard listen(std::chrono::milliseconds limit, std::string &data) {
		std::array<char, 64 * 1024> chunk{};
		std::promise<std::pair<boost::system::error_code, size_t>> done;
		auto result = done.get_future();
		socket_.async_read_some(boost::asio::buffer(chunk),
								[&](boost::system::error_code ec, size_t n) {
									done.set_value({ec, n});
								});
		if (result.wait_for(limit) != std::future_status::ready) {
			// Cancelling completes the read handler, which touches `done`.
			boost::asio::post(socket_.get_executor(),
							  [this] { socket_.cancel(); });
			result.wait();
			return Heard::Silence;
		}
		const auto [ec, n] = result.get();
		if (ec) return Heard::Closed;
		data.assign(chunk.data(), n);
		return Heard::Data;
	}

	/// Reads until \p total bytes have arrived or \p limit passes; returns
	/// whatever arrived.
	std::string read_exactly(size_t total, std::chrono::milliseconds limit) {
		std::string received;
		const auto deadline = std::chrono::steady_clock::now() + limit;
		while (received.size() < total) {
			const auto left = remaining(deadline);
			if (left <= std::chrono::milliseconds::zero()) break;
			std::string part;
			if (listen(left, part) != Heard::Data) break;
			received += part;
		}
		return received;
	}

	/// Sends a PING; true if the PONG comes back within a generous deadline.
	bool ping_answered() {
		using namespace std::chrono_literals;
		send(PING);
		std::string data;
		return listen(2s, data) == Heard::Data && data == PONG;
	}

	/// True if the server hangs up within \p limit; whatever it still sends
	/// before that is skipped.
	bool hangs_up_within(std::chrono::milliseconds limit) {
		const auto deadline = std::chrono::steady_clock::now() + limit;
		for (;;) {
			const auto left = remaining(deadline);
			if (left <= std::chrono::milliseconds::zero()) return false;
			std::string skipped;
			switch (listen(left, skipped)) {
			case Heard::Closed: return true;
			case Heard::Silence: return false;
			case Heard::Data: break;
			}
		}
	}

	void close() {
		boost::system::error_code ignored;
		socket_.close(ignored);
	}

private:
	static std::chrono::milliseconds
	remaining(std::chrono::steady_clock::time_point deadline) {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now());
	}

	boost::asio::ip::tcp::socket socket_;
};

/**
 * \brief Waits up to \p limit for \p sender to complete; \p what names it in
 * the message if it does not.
 *
 * A sender still running when the deadline passes belongs to state the test
 * is about to destroy, so it can be neither abandoned nor waited for: the run
 * ends here, with the message, a hang turned into a fast, loud failure.
 *
 * \tparam Sender The type of the awaited sender (a stdexec::sender), which
 *         sends nothing and cannot fail: drained() and the like.
 */
template <stdexec::sender Sender>
void require_finishes_within(Sender sender, std::chrono::milliseconds limit,
							 std::string_view what) {
	std::promise<void> done;
	auto finished = done.get_future();
	std::jthread waiter([&done, sender = std::move(sender)]() mutable {
		stdexec::sync_wait(std::move(sender));
		done.set_value();
	});
	if (finished.wait_for(limit) == std::future_status::ready) return;
	fmt::println(stderr,
				 "{} did not finish within {} ms: ending the run rather than "
				 "hang",
				 what,
				 limit.count());
	std::fflush(stderr);
	std::abort();
}

} // namespace redis::test
