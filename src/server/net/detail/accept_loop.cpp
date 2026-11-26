#include "accept_loop.hpp"

#include "core/stdexec_ext/retry.hpp"
#include "server/net/net_error.hpp"

#include <cstddef>
#include <utility>

namespace redis::detail {

namespace {

/// Logs what a failed accept said, whatever was thrown: a failure to log must
/// not end the loop.
void log_accept_failure(const std::exception_ptr &error) {
	log_error(connection_error(ConnectionKind::AcceptFailed, error));
}

} // namespace

// A coroutine for the loop only: it runs once per accept or failure, not per
// request, so its frame is not on the request path. Sessions stay sender
// chains.
stdexec_ext::InlineTask<void> accept_loop(AcceptSource &source) {
	stdexec_ext::AttemptFunc auto accept_one = [&source] {
		return source.attempt();
	};

	// Transient failures (e.g. EMFILE when out of descriptors) must not kill
	// the loop, and the failed socket is unusable: log, wait, try again, each
	// wait longer than the last up to the cap (DX-89). Once the acceptor is
	// closed there is nothing to retry, and that is not a failure to report.
	// Not retry_policy::exponential_backoff(): this policy also logs, and gives
	// up on the acceptor closing rather than on a count.
	stdexec_ext::RetryFunc auto after_failure =
		[&source](size_t failures, const std::exception_ptr &error) {
			if (source.is_open()) log_accept_failure(error);
			return source.pause(stdexec_ext::retry_policy::backoff_delay(
				ACCEPT_BACKOFF_INITIAL, ACCEPT_BACKOFF_CAP, failures));
		};

	while (source.is_open()) {
		try {
			co_await stdexec_ext::retry(accept_one, after_failure);
		} catch (...) {
			co_return; // retry gives up only once the acceptor is closed
		}
	}
}

} // namespace redis::detail
