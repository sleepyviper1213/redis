#include "conversation.hpp"

#include "pipeline.hpp"
#include "server/net/guard.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace redis::detail {

Conversation::Conversation(Parts parts, std::string peer)
	: parts_(parts), peer_(std::move(peer)), chunk_(READ_CHUNK_SIZE, '\0') {}

[[nodiscard]] std::span<char> Conversation::read_buffer() noexcept {
	return {chunk_.data(), chunk_.size()};
}

bool Conversation::ingest_read_bytes(size_t bytes) {
	// A spurious wakeup read nothing: what is pending was already answered as
	// far as it goes, so there is nothing to run and no timer to arm.
	if (bytes == 0) return false;
	assert(bytes <= chunk_.size() && "more bytes than read_buffer() holds");
	assert(!is_done_ && "a finished conversation is not fed");
	// Only a threaded conversation keeps its batch between reads: an inline
	// one parses, executes and drops it in execute_ingested_requests().
	assert((parts_.is_threaded || batch_.size() == 0) &&
		   "the previous batch was not dropped");
	// The previous batch has run, so the bytes its requests viewed can go
	// (execute_pending has already dropped them when there is no executor
	// thread).
	drop_consumed(pending_, batch_);
	pending_.append(chunk_.data(), bytes);
	if (!parts_.guard.check_input(pending_.size())) {
		parts_.guard.record(Violation::QueryBufferFull,
								  peer_,
								  pending_.size());
		is_done_ = true;
		return false;
	}
	// Without an executor thread, execute_pending parses and executes in one.
	if (!parts_.is_threaded) return true;

	// Parse here on the strand, so other sessions parse while this batch
	// executes.
	parse_pending(pending_, batch_);
	return batch_.size() != 0 || !batch_.protocol_error.empty();
}

void Conversation::execute_ingested_requests() {
	// Only after ingest_read_bytes() said there is something to run.
	assert(!pending_.empty() && "nothing was ingested");
	assert((!parts_.is_threaded || batch_.size() != 0 ||
			!batch_.protocol_error.empty()) &&
		   "no parsed requests to execute");
	const size_t max_output = parts_.guard.limits().max_output_buffer;
	after_ = parts_.is_threaded ? execute_batch(batch_,
												replies_,
												parts_.handler,
												parts_.ctx,
												max_output)
								: execute_pending(pending_,
												  batch_,
												  replies_,
												  parts_.handler,
												  parts_.ctx,
												  max_output);
	assert((parts_.is_threaded || batch_.size() == 0) &&
		   "execute_pending leaves its batch empty");
	if (after_ == AfterReplies::Close) is_done_ = true;
	if (!parts_.guard.check_output(replies_.size())) {
		// Nobody is going to read this much: drop it with the client.
		parts_.guard.record(Violation::OutputBufferFull,
								  peer_,
								  replies_.size());
		replies_.clear();
		is_done_ = true;
	}
}

[[nodiscard]] std::string_view Conversation::unsent_replies() const noexcept {
	return replies_;
}

void Conversation::count_idle_timeout_and_end(std::chrono::seconds limit) {
	assert(limit > std::chrono::seconds::zero() && "zero means no limit");
	parts_.guard.record(Violation::IdleTimeout,
							  peer_,
							  static_cast<size_t>(limit.count()));
	is_done_ = true;
}

void Conversation::count_request_timeout(std::chrono::seconds limit) {
	assert(limit > std::chrono::seconds::zero() && "zero means no limit");
	parts_.guard.record(Violation::RequestTimeout,
							  peer_,
							  static_cast<size_t>(limit.count()));
}

void Conversation::drop_replies_and_end() noexcept {
	replies_.clear();
	is_done_ = true;
}

[[nodiscard]] bool Conversation::is_over() const noexcept { return is_done_; }

[[nodiscard]] std::string_view Conversation::peer() const noexcept {
	return peer_;
}


} // namespace redis::detail
