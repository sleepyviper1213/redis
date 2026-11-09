// Verifying stress test for redis_server.
//
// redis-benchmark generates load but only checks that *a* reply arrived. This
// checks that every reply is the *right* one, and says exactly where the first
// wrong one appeared.
//
// What it does, in order:
//   1. Protocol: every request split at every byte boundary, one byte at a
//      time, malformed framing (must be answered with -ERR Protocol error and a
//      close), and garbage input (the server must survive it).
//   2. Load: many clients, each working on its own keys against a local model,
//      so every reply has an exact expected value. Requests are pipelined, some
//      are sent in random pieces, some values are large and binary (embedded
//      CRLF). Values carry the client id and a sequence number, so a reply that
//      crossed sessions or arrived out of order is recognisable. A shared
//      counter must be strictly increasing for each client and exact in total;
//      a shared set must have the exact cardinality.
//   3. Final sweep: each client re-reads everything it believes it wrote.
//
// hiredis is used only as an independent RESP encoder and reader, so a framing
// bug cannot hide by being on both sides. Every run prints its seed; rerun with
// --seed to replay. A failure prints the client, the request number and that
// client's last exchanges.
#include <CLI/CLI.hpp>
#include <boost/asio.hpp>
#include <fmt/format.h>
#include <hiredis/hiredis.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
namespace asio = boost::asio;
using asio::ip::tcp;
using Clock = std::chrono::steady_clock;

struct Options {
	std::string host   = "127.0.0.1";
	uint16_t port      = 6379;
	unsigned clients   = 16;
	unsigned ops       = 20000; ///< Per client.
	unsigned pipeline  = 32;    ///< Largest pipeline depth.
	size_t max_value   = 100000;
	uint64_t seed      = 0;
	unsigned stall_sec = 30;
	/// In every key name, chosen afresh each run, so a run never meets the
	/// leftovers of an earlier one: replaying a seed against the same server
	/// still starts from keys the model knows nothing about.
	std::string run_id;
};

/// A check failed; the message says where.
struct Failure : std::runtime_error {
	using std::runtime_error::runtime_error;
};

std::string printable(std::string_view bytes, size_t limit = 48) {
	std::string out;
	for (size_t i = 0; i < bytes.size() && i < limit; ++i) {
		const auto c = static_cast<unsigned char>(bytes[i]);
		if (c == '\r') out += "\\r";
		else if (c == '\n') out += "\\n";
		else if (c >= 32 && c < 127) out += static_cast<char>(c);
		else fmt::format_to(std::back_inserter(out), "\\x{:02x}", c);
	}
	if (bytes.size() > limit)
		fmt::format_to(std::back_inserter(out), "...({} bytes)", bytes.size());
	return out;
}

// --------------------------------------------------------------------------
// Replies
// --------------------------------------------------------------------------

struct Reply {
	enum class Kind { Status, Error, Integer, Bulk, Nil, Other };
	Kind kind         = Kind::Other;
	long long integer = 0;
	std::string text;

	static Reply status(std::string s) {
		return {Kind::Status, 0, std::move(s)};
	}

	static Reply error(std::string s) { return {Kind::Error, 0, std::move(s)}; }

	static Reply integer_of(long long n) { return {Kind::Integer, n, {}}; }

	static Reply bulk(std::string s) { return {Kind::Bulk, 0, std::move(s)}; }

	static Reply nil() { return {Kind::Nil, 0, {}}; }

	bool operator==(const Reply &) const = default;
};
} // namespace

/// A reply as the tests print it: RESP's type marker, then the value escaped.
template <>
struct fmt::formatter<Reply> {
	static constexpr auto parse(fmt::format_parse_context &ctx) {
		return ctx.begin();
	}

	static auto format(const Reply &reply, fmt::format_context &ctx) {
		using Kind = Reply::Kind;
		switch (reply.kind) {
		case Kind::Status:
			return fmt::format_to(ctx.out(), "+{}", printable(reply.text));
		case Kind::Error:
			return fmt::format_to(ctx.out(), "-{}", printable(reply.text));
		case Kind::Integer:
			return fmt::format_to(ctx.out(), ":{}", reply.integer);
		case Kind::Bulk:
			return fmt::format_to(ctx.out(), "${}", printable(reply.text));
		case Kind::Nil: return fmt::format_to(ctx.out(), "(nil)");
		case Kind::Other: break;
		}
		return fmt::format_to(ctx.out(), "(other: {})", printable(reply.text));
	}
};

namespace {
Reply to_reply(const redisReply &r) {
	switch (r.type) {
	case REDIS_REPLY_STATUS: return Reply::status({r.str, r.len});
	case REDIS_REPLY_ERROR: return Reply::error({r.str, r.len});
	case REDIS_REPLY_INTEGER: return Reply::integer_of(r.integer);
	case REDIS_REPLY_STRING: return Reply::bulk({r.str, r.len});
	case REDIS_REPLY_NIL: return Reply::nil();
	default: return {Reply::Kind::Other, 0, fmt::format("type {}", r.type)};
	}
}

/// A request as RESP bytes, encoded by hiredis.
/// The argument count is part of the type: `encode({"GET", key})` deduces it.
template <size_t N>
std::string encode(const std::string_view (&args)[N]) {
	std::array<const char *, N> argv{};
	std::array<size_t, N> lens{};
	for (size_t i = 0; i < N; ++i) {
		argv[i] = args[i].data();
		lens[i] = args[i].size();
	}
	char *cmd      = nullptr;
	const auto len = redisFormatCommandArgv(&cmd,
											static_cast<int>(N),
											argv.data(),
											lens.data());
	if (len < 0) throw Failure("hiredis could not encode a request");
	std::string out(cmd, static_cast<size_t>(len));
	redisFreeCommand(cmd);
	return out;
}

// --------------------------------------------------------------------------
// One connection: raw bytes out, hiredis' reader in
// --------------------------------------------------------------------------

class Connection {
public:
	explicit Connection(const Options &opt) : socket_(io_) {
		socket_.connect({asio::ip::make_address(opt.host), opt.port});
		socket_.set_option(tcp::no_delay(true));
		reader_.reset(redisReaderCreate());
	}

	void send(std::string_view bytes) {
		asio::write(socket_, asio::buffer(bytes.data(), bytes.size()));
	}

	/// In random pieces, so requests arrive split across reads.
	void send_in_pieces(std::string_view bytes, std::mt19937_64 &rng) {
		size_t pos = 0;
		while (pos < bytes.size()) {
			const size_t left = bytes.size() - pos;
			const size_t n    = 1 + rng() % std::min<size_t>(left, 1500);
			send(bytes.substr(pos, n));
			pos += n;
			if (rng() % 4 == 0) std::this_thread::yield();
		}
	}

	/// The next reply; std::nullopt if the server closed the connection cleanly
	/// between replies.
	std::optional<Reply> read_reply() {
		for (;;) {
			void *raw = nullptr;
			if (redisReaderGetReply(reader_.get(), &raw) != REDIS_OK)
				throw Failure(fmt::format("malformed RESP from the server: {}",
										  reader_->errstr));
			if (raw) {
				const std::unique_ptr<redisReply, void (*)(void *)> reply(
					static_cast<redisReply *>(raw),
					freeReplyObject);
				return to_reply(*reply);
			}

			std::array<char, 65536> buf;
			boost::system::error_code ec;
			const size_t n = socket_.read_some(asio::buffer(buf), ec);
			if (ec == asio::error::eof || ec == asio::error::connection_reset ||
				ec == asio::error::connection_aborted) {
				// Bytes fed but not parsed into a reply: a truncated one. (Only
				// bulk, status, error and integer replies are used, so a reply
				// is never partly parsed.)
				if (reader_->pos < reader_->len)
					throw Failure(fmt::format("connection closed in the middle "
											  "of a reply ({} unparsed bytes)",
											  reader_->len - reader_->pos));
				return std::nullopt;
			}
			if (ec) throw Failure(fmt::format("read failed: {}", ec.message()));
			if (redisReaderFeed(reader_.get(), buf.data(), n) != REDIS_OK)
				throw Failure(fmt::format("malformed RESP from the server: {}",
										  reader_->errstr));
		}
	}

	Reply expect_reply() {
		auto r = read_reply();
		if (!r) throw Failure("the server closed the connection unexpectedly");
		return *r;
	}

	void close() {
		boost::system::error_code ignored;
		socket_.close(ignored);
	}

private:
	asio::io_context io_;
	tcp::socket socket_;

	struct ReaderDeleter {
		void operator()(redisReader *r) const { redisReaderFree(r); }
	};

	std::unique_ptr<redisReader, ReaderDeleter> reader_;
};

/// What differs between two replies, down to the first differing byte, since
/// a truncated printout of two long values can look identical.
std::string diff(const Reply &want, const Reply &got) {
	const bool text_kind = want.kind == Reply::Kind::Bulk ||
						   want.kind == Reply::Kind::Status ||
						   want.kind == Reply::Kind::Error;
	if (want.kind != got.kind || !text_kind)
		return fmt::format("expected {}, got {}", want, got);

	size_t i = 0;
	while (i < want.text.size() && i < got.text.size() &&
		   want.text[i] == got.text[i])
		++i;
	const auto window = [i](const std::string &t) {
		const size_t from = i > 12 ? i - 12 : 0;
		return fmt::format(
			"{}{}",
			from ? "..." : "",
			printable(std::string_view(t).substr(from, 32), 160));
	};
	return fmt::format(
		"the values differ at byte {} (expected {} bytes, got {})\n"
		"        expected around it: {}\n"
		"        got around it:      {}",
		i,
		want.text.size(),
		got.text.size(),
		window(want.text),
		window(got.text));
}

// --------------------------------------------------------------------------
// Progress and hang detection
// --------------------------------------------------------------------------

std::atomic<long long> g_progress_ms{0};
std::atomic<unsigned long long> g_verified{0};

long long now_ms() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   Clock::now().time_since_epoch())
		.count();
}

void note_progress() {
	g_progress_ms.store(now_ms(), std::memory_order_relaxed);
}

// --------------------------------------------------------------------------
// A client with a model of its own keys
// --------------------------------------------------------------------------

struct Shared {
	std::string counter_key;
	std::string set_key;
};

class Worker {
public:
	Worker(unsigned id, const Options &opt, const Shared &shared)
		: id_(id),
		  opt_(opt),
		  shared_(shared),
		  rng_(opt.seed * 1000003 + id),
		  prefix_(fmt::format("r{}:w{}:", opt.run_id, id)),
		  conn_(opt) {}

	/// Throws Failure on the first reply that is not the expected one.
	void run() {
		unsigned done = 0;
		while (done < opt_.ops) done += round();
	}

	/// Re-read everything this client believes it wrote.
	void sweep() {
		for (unsigned k = 0; k < KEYS; ++k) {
			const auto key = str_key(k);
			const auto it  = strs_.find(key);
			check_one(
				{encode({"GET", key}),
				 it == strs_.end() ? Reply::nil() : Reply::bulk(it->second),
				 Check::Exact,
				 fmt::format("sweep GET {}", key)});
		}
		for (unsigned k = 0; k < COUNTERS; ++k) {
			const auto key = counter_key(k);
			const auto it  = counters_.find(key);
			check_one({encode({"GET", key}),
					   it == counters_.end()
						   ? Reply::nil()
						   : Reply::bulk(fmt::to_string(it->second)),
					   Check::Exact,
					   fmt::format("sweep GET {}", key)});
		}
	}

	[[nodiscard]] long long shared_incrs() const { return shared_incrs_; }

	[[nodiscard]] long long shared_adds() const { return shared_adds_; }

	[[nodiscard]] std::string trace() const {
		std::string out;
		for (const auto &line : trace_)
			fmt::format_to(std::back_inserter(out), "    {}\n", line);
		return out;
	}

	[[noreturn]] void fail(const std::string &what) const {
		throw Failure(fmt::format("client {}, request #{}: {}\n"
								  "  last exchanges (oldest first):\n{}"
								  "  replay with --seed {}",
								  id_,
								  seq_,
								  what,
								  trace(),
								  opt_.seed));
	}

private:
	static constexpr unsigned KEYS      = 24;
	static constexpr unsigned COUNTERS  = 4;
	static constexpr size_t BATCH_BYTES = 32 * 1024;
	static constexpr size_t LARGE_VALUE = 16 * 1024;

	enum class Check { Exact, SharedCounter };

	struct Op {
		std::string wire;
		Reply expected;
		Check check = Check::Exact;
		std::string what;
		bool large = false;
	};

	[[nodiscard]] std::string str_key(unsigned k) const {
		return fmt::format("{}k{}", prefix_, k);
	}

	[[nodiscard]] std::string counter_key(unsigned k) const {
		return fmt::format("{}n{}", prefix_, k);
	}

	/// "w3:41:" then bytes. Mostly short and printable, sometimes long and
	/// binary with CR, LF, '$' and '*' inside, to stress the framing.
	std::string make_value(bool &large) {
		std::string v   = fmt::format("w{}:{}:", id_, seq_);
		const auto roll = rng_() % 100;
		size_t len;
		bool binary = false;
		if (roll < 90) len = rng_() % 65;
		else if (roll < 98) {
			len    = 64 + rng_() % 4000;
			binary = rng_() % 3 == 0;
		} else {
			len    = rng_() % opt_.max_value;
			binary = true;
		}
		for (size_t i = 0; i < len; ++i)
			v += binary ? static_cast<char>(rng_() & 0xff)
						: static_cast<char>('a' + rng_() % 26);
		large = v.size() > LARGE_VALUE;
		return v;
	}

	Op make_op() {
		++seq_;
		const auto roll = rng_() % 100;
		const auto key  = str_key(static_cast<unsigned>(rng_() % KEYS));
		Op op;

		if (roll < 25) {
			const auto v = make_value(op.large);
			op.wire      = encode({"SET", key, v});
			op.expected  = Reply::status("OK");
			op.what      = fmt::format("SET {} {}", key, printable(v, 24));
			strs_[key]   = v;
		} else if (roll < 50) {
			const auto it = strs_.find(key);
			op.wire       = encode({"GET", key});
			op.expected =
				it == strs_.end() ? Reply::nil() : Reply::bulk(it->second);
			op.what = fmt::format("GET {}", key);
		} else if (roll < 58) {
			op.wire     = encode({"DEL", key});
			op.expected = Reply::integer_of(strs_.erase(key) ? 1 : 0);
			op.what     = fmt::format("DEL {}", key);
		} else if (roll < 63) {
			op.wire     = encode({"EXISTS", key});
			op.expected = Reply::integer_of(strs_.contains(key) ? 1 : 0);
			op.what     = fmt::format("EXISTS {}", key);
		} else if (roll < 73) {
			std::string tail = fmt::format("<{}>", seq_);
			for (auto n = rng_() % 200; n; --n)
				tail += static_cast<char>(rng_() & 0xff);
			op.wire = encode({"APPEND", key, tail});
			auto &v = strs_[key];
			v += tail;
			op.expected = Reply::integer_of(static_cast<long long>(v.size()));
			op.what     = fmt::format("APPEND {} {}", key, printable(tail, 24));
			op.large    = v.size() > LARGE_VALUE;
		} else if (roll < 78) {
			const auto it = strs_.find(key);
			op.wire       = encode({"STRLEN", key});
			op.expected   = Reply::integer_of(
				it == strs_.end() ? 0
								  : static_cast<long long>(it->second.size()));
			op.what = fmt::format("STRLEN {}", key);
		} else if (roll < 88) {
			const auto ck = counter_key(static_cast<unsigned>(rng_() % COUNTERS));
			op.wire       = encode({"INCR", ck});
			op.expected   = Reply::integer_of(++counters_[ck]);
			op.what       = fmt::format("INCR {}", ck);
		} else if (roll < 95) {
			op.wire  = encode({"INCR", shared_.counter_key});
			op.check = Check::SharedCounter;
			op.what  = fmt::format("INCR {} (shared)", shared_.counter_key);
			++shared_incrs_;
		} else {
			const auto member = fmt::format("{}m{}", prefix_, seq_);
			op.wire           = encode({"SADD", shared_.set_key, member});
			op.expected       = Reply::integer_of(1); // unique, so always new
			op.what = fmt::format("SADD {} {}", shared_.set_key, member);
			++shared_adds_;
		}
		return op;
	}

	void record(const Op &op, const Reply &got) {
		const std::string expected =
			op.check == Check::SharedCounter
				? fmt::format("an integer > {}", last_shared_)
				: fmt::to_string(op.expected);
		trace_.push_back(
			fmt::format("#{} {}\n        expected {}\n        got      {}",
						seq_,
						op.what,
						expected,
						got));
		if (trace_.size() > TRACE_LINES) trace_.pop_front();
	}

	void verify(const Op &op, const Reply &got) {
		record(op, got);
		if (op.check == Check::SharedCounter) {
			if (got.kind != Reply::Kind::Integer || got.integer <= last_shared_)
				fail(fmt::format(
					"the shared counter did not strictly increase: {}",
					got));
			last_shared_ = got.integer;
		} else if (!(got == op.expected)) {
			fail(fmt::format("reply differs from the model: {}",
							 diff(op.expected, got)));
		}
		g_verified.fetch_add(1, std::memory_order_relaxed);
		note_progress();
	}

	void check_one(const Op &op) {
		++seq_;
		conn_.send(op.wire);
		verify(op, conn_.expect_reply());
	}

	/// One pipelined burst. Returns how many requests it held.
	unsigned round() {
		const unsigned depth =
			1 + static_cast<unsigned>(rng_() % opt_.pipeline);
		std::vector<Op> ops;
		std::string wire;
		bool large = false;
		for (unsigned i = 0; i < depth && wire.size() < BATCH_BYTES && !large;
			 ++i) {
			ops.push_back(make_op());
			wire += ops.back().wire;
			large = ops.back().large;
		}

		if (rng_() % 8 == 0) conn_.send_in_pieces(wire, rng_);
		else conn_.send(wire);

		// make_op advanced seq_; report each reply under its own number.
		const auto last = seq_;
		seq_            = last - ops.size();
		for (const auto &op : ops) {
			++seq_;
			auto got = conn_.read_reply();
			if (!got)
				fail("the server closed the connection with replies pending");
			verify(op, *got);
		}
		return static_cast<unsigned>(ops.size());
	}

	static constexpr size_t TRACE_LINES = 12;

	unsigned id_;
	const Options &opt_;
	const Shared &shared_;
	std::mt19937_64 rng_;
	std::string prefix_;
	Connection conn_;

	std::unordered_map<std::string, std::string> strs_;
	std::unordered_map<std::string, long long> counters_;
	long long last_shared_  = 0;
	long long shared_incrs_ = 0;
	long long shared_adds_  = 0;
	unsigned long long seq_ = 0;
	std::deque<std::string> trace_;
};

// --------------------------------------------------------------------------
// Protocol phase
// --------------------------------------------------------------------------

/// Run \p body; a failure inside it is reported under \p context.
template <typename Body>
void in_context(const std::string &context, Body &&body) {
	try {
		body();
	} catch (const Failure &f) {
		throw Failure(fmt::format("{}: {}", context, f.what()));
	} catch (const boost::system::system_error &e) {
		throw Failure(fmt::format("{}: {}", context, e.what()));
	}
}

void expect(const Reply &got, const Reply &want, const std::string &context) {
	if (!(got == want))
		throw Failure(fmt::format("{}: {}", context, diff(want, got)));
}

/// Every request split at every byte boundary, and sent one byte at a time,
/// must give the same replies as when sent whole.
void fragmentation(const Options &opt, std::mt19937_64 &rng) {
	struct Case {
		std::string name;
		std::string wire;
		std::vector<Reply> replies;
	};

	const std::string big(3000, 'x');
	const std::array<Case, 8> cases = {{
		{"RESP SET", encode({"SET", "frag:k", "hello"}), {Reply::status("OK")}},
		{"RESP GET", encode({"GET", "frag:k"}), {Reply::bulk("hello")}},
		{"RESP PING", encode({"PING"}), {Reply::status("PONG")}},
		{"inline PING", "PING\r\n", {Reply::status("PONG")}},
		{"two inline",
		 "PING\r\nPING\r\n",
		 {Reply::status("PONG"), Reply::status("PONG")}},
		{"RESP then inline",
		 fmt::format("{}PING\r\n", encode({"PING"})),
		 {Reply::status("PONG"), Reply::status("PONG")}},
		{"binary value",
		 fmt::format(
			 "{}{}",
			 encode({"SET", "frag:b", std::string("a\r\n$3\r\nb*1\r\n", 12)}),
			 encode({"GET", "frag:b"})),
		 {Reply::status("OK"),
		  Reply::bulk(std::string("a\r\n$3\r\nb*1\r\n", 12))}},
		{"3000-byte value",
		 fmt::format("{}{}",
					 encode({"SET", "frag:l", big}),
					 encode({"GET", "frag:l"})),
		 {Reply::status("OK"), Reply::bulk(big)}},
	}};

	Connection conn(opt);
	for (const auto &c : cases) {
		// Every split point for short requests; the ends plus a sample for
		// long ones.
		std::vector<size_t> splits;
		for (size_t p = 1; p < c.wire.size(); ++p)
			if (c.wire.size() <= 96 || p < 48 || p + 48 > c.wire.size() ||
				rng() % 40 == 0)
				splits.push_back(p);

		for (const size_t p : splits)
			in_context(fmt::format("{} split at byte {} of {}",
								   c.name,
								   p,
								   c.wire.size()),
					   [&] {
						   conn.send(std::string_view(c.wire).substr(0, p));
						   std::this_thread::sleep_for(
							   std::chrono::milliseconds(1));
						   conn.send(std::string_view(c.wire).substr(p));
						   for (const auto &want : c.replies)
							   expect(conn.expect_reply(), want, "reply");
						   note_progress();
					   });

		in_context(fmt::format("{} sent one byte at a time", c.name), [&] {
			for (const char ch : c.wire) conn.send(std::string_view(&ch, 1));
			for (const auto &want : c.replies)
				expect(conn.expect_reply(), want, "reply");
			note_progress();
		});
	}
}

/// Malformed framing is answered with "-ERR Protocol error" and the
/// connection closes; unknown commands and bad arity are errors the
/// connection survives; garbage never kills the server.
void malformed(const Options &opt, std::mt19937_64 &rng) {
	const std::array<std::pair<std::string_view, std::string_view>, 7>
		rejected = {{
			{"missing CRLF after a bulk string", "*1\r\n$3\r\nabcXY"},
			{"non-numeric array length", "*abc\r\n"},
			{"non-numeric bulk length", "*1\r\n$abc\r\n"},
			{"array element that is not a bulk string", "*1\r\nabc\r\n"},
			{"negative bulk length", "*1\r\n$-5\r\nxx\r\n"},
			{"negative array length", "*-5\r\n"},
			{"absurd bulk length", "*1\r\n$99999999999999\r\n"},
		}};
	for (const auto &[name, wire] : rejected)
		in_context(fmt::format("malformed request ({})", name), [&] {
			Connection conn(opt);
			conn.send(wire);
			const auto first = conn.expect_reply();
			if (first.kind != Reply::Kind::Error ||
				!first.text.starts_with("ERR Protocol error"))
				throw Failure(
					fmt::format("not answered with ERR Protocol error: {}",
								first));
			if (conn.read_reply())
				throw Failure(
					"connection stayed open after the protocol error");
			note_progress();
		});

	const std::array<std::pair<std::string_view, std::string_view>, 3>
		survivable = {{
			{"unknown command", "NOSUCH 1 2\r\n"},
			{"wrong arity", "GET\r\n"},
			{"empty array", "*0\r\n"},
		}};
	for (const auto &[name, wire] : survivable) {
		Connection conn(opt);
		conn.send(wire);
		const auto r = conn.expect_reply();
		if (r.kind != Reply::Kind::Error)
			throw Failure(
				fmt::format("{} was not answered with an error: {}", name, r));
		conn.send("PING\r\n");
		expect(conn.expect_reply(),
			   Reply::status("PONG"),
			   fmt::format("PING after {} on the same connection", name));
		note_progress();
	}

	// Garbage, and connections that open and vanish. The server must keep
	// serving afterwards.
	for (int i = 0; i < 200; ++i) {
		try {
			Connection conn(opt);
			std::string junk(rng() % 512, '\0');
			for (auto &c : junk) c = static_cast<char>(rng() & 0xff);
			if (rng() % 4) conn.send(junk);
			conn.close();
		} catch (const boost::system::system_error &) {
			// A reset while sending garbage is the server's right.
		}
		note_progress();
	}
	Connection alive(opt);
	alive.send("PING\r\n");
	expect(alive.expect_reply(),
		   Reply::status("PONG"),
		   "PING after garbage input and abandoned connections");
}

// --------------------------------------------------------------------------

struct Run {
	std::vector<std::unique_ptr<Worker>> workers;
	std::mutex mutex;
	std::vector<std::string> failures;
};

} // namespace

int main(int argc, char **argv) {
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	Options opt;
	CLI::App app{"Verifying stress test for redis_server"};
	app.add_option("--host", opt.host, "Server address")->capture_default_str();
	app.add_option("-p,--port", opt.port, "Server port")->capture_default_str();
	app.add_option("-c,--clients", opt.clients, "Concurrent clients")
		->capture_default_str();
	app.add_option("-n,--ops", opt.ops, "Requests per client")
		->capture_default_str();
	app.add_option("-P,--pipeline", opt.pipeline, "Largest pipeline depth")
		->capture_default_str();
	app.add_option("--max-value", opt.max_value, "Largest value in bytes")
		->capture_default_str();
	app.add_option("--seed", opt.seed, "RNG seed (0 picks one and prints it)");
	app.add_option("--stall",
				   opt.stall_sec,
				   "Seconds without progress before the run is declared hung")
		->capture_default_str();
	CLI11_PARSE(app, argc, argv);

	std::random_device entropy;
	if (opt.seed == 0) opt.seed = entropy() | 1U;
	{
		char id[16];
		std::snprintf(id, sizeof id, "%08x", entropy());
		opt.run_id = id;
	}
	if (opt.pipeline == 0) opt.pipeline = 1;
	std::printf("stress: %u clients x %u requests, pipeline <= %u, seed %llu\n",
				opt.clients,
				opt.ops,
				opt.pipeline,
				static_cast<unsigned long long>(opt.seed));

	Run run;
	note_progress();

	// A hung server, or a wrongly dropped reply, would block a client in
	// read() forever: say where and stop.
	std::thread([&] {
		for (;;) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			if (now_ms() - g_progress_ms.load() > opt.stall_sec * 1000LL) {
				std::fprintf(
					stderr,
					"\nSTALLED: no verified reply for %u s. The server "
					"is hung, or a reply was never sent.\nreplay with "
					"--seed %llu\n",
					opt.stall_sec,
					static_cast<unsigned long long>(opt.seed));
				std::fflush(stderr);
				std::_Exit(2);
			}
		}
	}).detach();

	const auto started = Clock::now();
	std::mt19937_64 rng(opt.seed);
	try {
		std::printf("phase 1: protocol (fragmentation, malformed input)\n");
		fragmentation(opt, rng);
		malformed(opt, rng);

		std::printf("phase 2: load\n");
		const Shared shared{fmt::format("r{}:shared:counter", opt.run_id),
							fmt::format("r{}:shared:set", opt.run_id)};
		for (unsigned i = 0; i < opt.clients; ++i)
			run.workers.push_back(std::make_unique<Worker>(i, opt, shared));

		std::vector<std::thread> threads;
		for (auto &w : run.workers)
			threads.emplace_back([&run, &w] {
				try {
					w->run();
					w->sweep();
				} catch (const std::exception &e) {
					const std::lock_guard lock(run.mutex);
					run.failures.emplace_back(e.what());
				}
			});
		for (auto &t : threads) t.join();

		if (run.failures.empty()) {
			std::printf("phase 3: shared state\n");
			long long incrs = 0, adds = 0;
			for (const auto &w : run.workers) {
				incrs += w->shared_incrs();
				adds += w->shared_adds();
			}
			Connection conn(opt);
			conn.send(encode({"GET", shared.counter_key}));
			const auto counter = conn.expect_reply();
			if (incrs == 0 ? counter.kind != Reply::Kind::Nil
						   : counter != Reply::bulk(fmt::to_string(incrs)))
				run.failures.push_back(fmt::format(
					"shared counter: {} increments were acknowledged but the "
					"counter reads {} (a lost or duplicated update)",
					incrs,
					counter));
			conn.send(encode({"SCARD", shared.set_key}));
			const auto card = conn.expect_reply();
			if (card != Reply::integer_of(adds))
				run.failures.push_back(
					fmt::format("shared set: {} unique members were added but "
								"SCARD is {}",
								adds,
								card));
		}
	} catch (const std::exception &e) { run.failures.emplace_back(e.what()); }

	const auto secs =
		std::chrono::duration<double>(Clock::now() - started).count();
	if (!run.failures.empty()) {
		std::fprintf(stderr,
					 "\nFAILED (%zu failure%s):\n",
					 run.failures.size(),
					 run.failures.size() == 1 ? "" : "s");
		for (const auto &f : run.failures)
			std::fprintf(stderr, "\n%s\n", f.c_str());
		return 1;
	}
	std::printf("PASS: %llu replies verified in %.1f s (%.0f/s), seed %llu\n",
				g_verified.load(),
				secs,
				static_cast<double>(g_verified.load()) / secs,
				static_cast<unsigned long long>(opt.seed));
	return 0;
}
