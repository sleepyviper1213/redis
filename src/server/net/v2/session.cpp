#include "session.hpp"

#include "core/logging.hpp"
#include "server/commands/handler.hpp"
#include "server/net/executor.hpp"
#include "server/net/formatter.hpp"
#include "server/net/pipeline.hpp"

#include <boost/asio/write.hpp>
#include <exec/asio/use_sender.hpp>
#include <exec/repeat_until.hpp>
#include <exec/variant_sender.hpp>
#include <spdlog/logger.h>

#include <exception>
#include <utility>

using boost::asio::ip::tcp;

namespace redis::v2 {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::net);
}

/**
 * \p sender if \p condition holds, otherwise a sender that completes at once
 * with nothing. let_value needs one sender type from all its branches, so the
 * two are made one. The unused sender is only built, never started.
 */
template <stdexec::sender Sender>
auto run_if(bool condition, Sender sender) {
	using Skip   = decltype(stdexec::just());
	using Result = exec::variant_sender<Sender, Skip>;
	if (condition) return Result(std::move(sender));
	return Result(stdexec::just());
}
} // namespace

Session::Session(tcp::socket socket, Executor &executor,
				 const CommandHandler &handler, CommandContext &ctx)
	: socket_(std::move(socket)),
	  strand_(std::make_unique<AsioContext>(socket_.get_executor())),
	  executor_(executor),
	  handler_(handler),
	  ctx_(ctx),
	  chunk_(READ_CHUNK_SIZE, '\0') {
	boost::system::error_code ec;
	const auto peer = socket_.remote_endpoint(ec);
	if (!ec) log().debug("Accepted from {}", peer);
}

bool Session::ingest(size_t bytes) {
	// The previous batch has run, so the bytes its requests viewed can go
	// (execute_pending has already dropped them when there is no executor
	// thread).
	drop_consumed(pending_, batch_);
	pending_.append(chunk_.data(), bytes);
	if (pending_.size() > MAX_QUERY_BUFFER) {
		log().warn("Query buffer limit exceeded, closing connection");
		done_ = true;
		return false;
	}
	// Without an executor thread, execute_pending parses and executes in one.
	if (!executor_.is_threaded()) return true;

	// Parse here on the strand, so other sessions parse while this batch
	// executes.
	parse_pending(pending_, batch_);
	return batch_.size() != 0 || !batch_.protocol_error.empty();
}

void Session::run_batch() {
	after_ = executor_.is_threaded()
				 ? execute_batch(batch_, replies_, handler_, ctx_)
				 : execute_pending(pending_, batch_, replies_, handler_, ctx_);
}

void Session::fail(std::exception_ptr error) {
	done_ = true;
	try {
		std::rethrow_exception(error);
	} catch (const boost::system::system_error &e) {
		if (e.code() == boost::asio::error::eof)
			log().debug("Client disconnected normally");
		else
			log().error("Connection: {}", e.code());
	} catch (const std::exception &e) {
		log().error("Connection: {}", e.what());
	}
}

auto Session::read() {
	return socket_.async_read_some(boost::asio::buffer(chunk_),
								   exec::asio::use_sender);
}

auto Session::execute() {
	// On the executor's thread and back on this strand afterwards, or right
	// here if there is no such thread.
	return executor_.run_on([this] { run_batch(); }, strand_->get_scheduler());
}

auto Session::write() {
	return boost::asio::async_write(socket_, boost::asio::buffer(replies_),
									exec::asio::use_sender)
		 | stdexec::then([this](size_t) { replies_.clear(); });
}

auto Session::round() {
	using stdexec::just;

	return read()
		 | stdexec::let_value([this](size_t bytes) {
			   return run_if(ingest(bytes), execute());
		   })
		 | stdexec::let_value([this] {
			   if (after_ == AfterReplies::Close) done_ = true;
			   // A client whose request is still incomplete gets no reply.
			   return run_if(!replies_.empty(), write());
		   })
		 | stdexec::let_error([this](std::exception_ptr error) {
			   fail(std::move(error));
			   return just();
		   })
		 | stdexec::then([this] { return done_; });
}

SessionSender Session::serve(tcp::socket socket, Executor &executor,
							 const CommandHandler &handler,
							 CommandContext &ctx) {
	return stdexec::just(Session(std::move(socket), executor, handler, ctx))
		 | stdexec::let_value([](Session &session) {
			   return exec::repeat_until(session.round());
		   })
		 // The chain's only failures are the ones round() did not already turn
		 // into "the session is over"; log them and finish normally.
		 | stdexec::upon_error([](std::exception_ptr e) noexcept {
			   try {
				   std::rethrow_exception(e);
			   } catch (const std::exception &ex) {
				   log().error("Session failed: {}", ex.what());
			   } catch (...) {
				   log().error("Session failed");
			   }
		   });
}

} // namespace redis::v2
