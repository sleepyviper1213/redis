# ADR-0012: A session's decisions live apart from its I/O, in a Conversation

- **Status:** Accepted
- **Date:** 2026-11-24
- **Requirements:** FR-008, FR-014, NFR-006
- **Issue / PR:** none yet

## Context

`detail::Session` ([session.cpp](../../../../src/server/net/detail/session.cpp)) is one class doing four jobs:

1. **I/O**: wait for readability, `read_some`, `write_some`, then `async_write` for the rest.
2. **Decisions**: whether the bytes read make a batch to run, whether the client has passed a buffer limit, what
   the batch's replies are, whether the connection closes afterwards, and what a timeout or a failure does to the
   replies.
3. **Sender plumbing**: `run_if`, `with_time_limit`, `let_value` and `let_error` (DX-36, ADR-0002).
4. **Scheduler hops**: strand to keyspace thread and back (DX-34, ADR-0001).

These jobs share state through members (`pending_`, `batch_`, `replies_`, `after_`, `done_`) that lambdas in every
stage write. The only way to test any of them is the way
[session.test.cpp](../../../../test/unit/server/net/session.test.cpp) does: a real loopback socket, a whole
`Services` (with a dump file on disk), an I/O thread, and real one-second timers. So:

* The decisions in job 2 are pure, but every test of them waits on a socket and a clock. Some are not tested at
  session level at all. A client over the query buffer limit has no session test (TC-187 is Partial). The client
  dropped after the output limit is not asserted (TC-188). Reaching either through a socket means sending
  megabytes and racing the read size.
* Whether the inline path (`--io-threads 1`, `execute_pending`) and the threaded path (`parse_pending` on the strand,
  `execute_batch` on the keyspace thread) answer the same bytes the same way is not tested, though they are two code
  paths for every request.
* `CloseSocketOnStop`, the one piece with a mutex and a callback from any thread, is only exercised by whole-session
  shutdown tests. A stop that arrives before `arm()`, or that races the destructor, is never set up on purpose.

## Decision

Move job 2, with the state it owns, into a class with no socket, no scheduler and no sender:
`detail::Conversation`. `Session` keeps jobs 1, 3 and 4. It owns a `Conversation` by value and calls it from its
stages. `CloseSocketOnStop` gets tests of its own.

### Shape (as built)

Functions that return `bool` are named `is_` or `has_`, except `ingest_read_bytes`, whose result is not a property
but "there is a batch to run", which its documentation says. Every other name says what it does, including what it
counts.

```cpp
// src/server/net/detail/conversation.hpp
class Conversation {
public:
	struct Parts {                    // every reference outlives the conversation
		NetGuard &guard;
		const CommandHandler &handler;
		CommandContext &ctx;
		bool is_threaded;             // as Executor::is_threaded()
	};

	Conversation(Parts parts, std::string peer);

	std::span<char> read_buffer() noexcept;
	/// 
eturn Whether there is a batch to run.
	bool ingest_read_bytes(size_t bytes);
	void execute_ingested_requests();           // on the keyspace thread when there is one
	std::string_view unsent_replies() const noexcept;
	void discard_sent_replies() noexcept;

	void count_idle_timeout_and_end(std::chrono::seconds limit);
	void count_request_timeout(std::chrono::seconds limit);  // counts only
	void drop_replies_and_end() noexcept;                    // counts nothing
	void end_keeping_replies() noexcept;                     // an I/O failure the session logged

	bool is_over() const noexcept;
	std::string_view peer() const noexcept;
};
```

`report_for(const ConnectionError &, bool socket_open)` and its `IoFailureReport` live in `net_error.hpp`, not here:
telling an end of file from another error needs Asio's `eof`, and `Conversation` must not include Asio.

Two differences from the sketch this ADR was proposed with, both found while building it:

* There is no `has_replies()`. `execute_ingested_requests()` ends the conversation itself when the batch ended in a
  protocol error, so asking afterwards is `is_over()`.
* The request timeout is two calls, not one. It is counted when the limit runs out (`count_request_timeout`), but
  the replies are dropped only once the abandoned work has unwound (`drop_replies_and_end`), as `Session::respond`
  already ordered them: dropping earlier would race the write still in progress.
CloseSocketOnStop
What stays in `Session`: the socket, `CloseOnStop`, the strand, both timers, the `Services` pointer (it holds the
lifetime of `Parts`), the `NetGuard::Ticket`, `sent_`, and the stages `read`, `execute`, `write`, `respond` and
`round_chain`, which only move bytes and call the `Conversation`. `fail_io` becomes `report_for` plus the log line.

### Rules that keep it from costing anything

* There are no virtual calls and no templates. The class is concrete and owned by value, and its calls are the
  same plain calls `Session` makes to its own members today. The hot path is unchanged.
* Threading is as now: one `Conversation` per session, touched on the session's strand. The exception is
  `execute_ingested_requests()`, which runs on the keyspace thread while the strand waits for it. The `run_on` hand-off orders the
  two. `Conversation` has no locks and needs none.
* The rule that argument views stay valid until `drop_consumed()` (ADR-0004) becomes the class's own invariant:
  `ingest_read_bytes()` drops the previous batch's bytes before it appends.
* Errors stay as they are: limits are `Violation`s counted on the guard, I/O failures are `Failure<ConnectionKind>`
  (DX-41, DX-44, ADR-0009).

## Alternatives considered

- **Template `Session` on its stream (a fake socket in tests).** This would test the I/O too, but the deduced
  sender types of a round would be computed again for each stream type. That compile cost is why ADR-0003 cuts the
  chain at `run()` and why `accept_loop` is type-erased. Today's loopback tests already cover the I/O.
- **A virtual transport with erased senders.** It needs one allocation per read and write on the hot path, against
  the zero-allocation round that ADR-0003 relies on.
- **Interfaces for `NetGuard`, `CommandHandler` and `Executor` (mock everything).** These would add virtual calls
  on the hot path, and their mocks would test the mocks. The real guard and handler are cheap to build, so the tests
  use them.
- **Keep it as it is and write more loopback tests.** Those tests are slow and timing-bound, and they cannot set up
  the query and output limits deterministically. TC-187 and TC-188 stay where they are.

## Consequences

- Easier: every decision a session makes is tested in microseconds, from strings, in both modes (TC-278 to TC-290).
  The two buffer-limit gaps close. Equality of the inline and threaded paths becomes a test, not an assumption.
- Easier: the loopback suite narrows to what only a socket shows: cancellation, the Windows unread-send case,
  partial writes, and shutdown (TC-166, 167, 189 to 191 stay there).
- Harder: a behaviour change now touches two files, and a reader has to follow `Session` into `Conversation`.
- MCloseSocketOnStop: `Conversation` never includes Asio or stdexec. If it needs either, the split is in the wrong place.
- `CloseOnStop` is tested directly (TC-291 to TC-294). It does not move.
