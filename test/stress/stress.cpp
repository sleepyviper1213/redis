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
//   4. Same-key race: several clients pipeline SET, DEL and GET on one key in
//      rounds, and every round's replies must be explained by one order of
//      the requests that keeps each client's own order and real time
//      (linearizability.hpp checks that).
//
// hiredis is used only as an independent RESP encoder and reader, so a framing
// bug cannot hide by being on both sides. Every run prints its seed; rerun with
// --seed to replay. A failure prints the client, the request number and that
// client's last exchanges.
#include "linearizability.hpp"

#include <CLI/CLI.hpp>
#include <boost/asio.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <hiredis/hiredis.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
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
	std::string host       = "127.0.0.1";
	uint16_t port          = 6379;
	unsigned clients       = 16;
	unsigned ops           = 20000; ///< Per client.
	unsigned pipeline      = 32;    ///< Largest pipeline depth.
	size_t max_value       = 100'000;
	uint64_t seed          = 0;
	unsigned stall_sec     = 30;
	unsigned race_clients  = 8;
	unsigned race_rounds   = 2000;
	unsigned race_pipeline = 4;
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
	enum class Kind { Status, Error, Integer, Bulk, Nil, Array, Other };
	Kind kind         = Kind::Other;
	long long integer = 0;
	std::string text;
	std::vector<Reply> elements; ///< An array's.

	static Reply status(std::string s) {
		return of(Kind::Status, std::move(s));
	}

	static Reply error(std::string s) { return of(Kind::Error, std::move(s)); }

	static Reply integer_of(long long n) {
		Reply r   = of(Kind::Integer);
		r.integer = n;
		return r;
	}

	static Reply bulk(std::string s) { return of(Kind::Bulk, std::move(s)); }

	static Reply nil() { return of(Kind::Nil); }

	static Reply other(std::string what) {
		return of(Kind::Other, std::move(what));
	}

	static Reply of(Kind kind, std::string text = {}) {
		return {.kind     = kind,
				.integer  = 0,
				.text     = std::move(text),
				.elements = {}};
	}

	static Reply array(std::vector<Reply> elements) {
		return {.kind     = Kind::Array,
				.integer  = 0,
				.text     = {},
				.elements = std::move(elements)};
	}

	/// An array of bulk strings.
	template <typename Range>
	static Reply bulks(const Range &values) {
		std::vector<Reply> out;
		for (const auto &v : values) out.push_back(bulk(std::string(v)));
		return array(std::move(out));
	}

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
		case Kind::Array: {
			constexpr size_t SHOWN = 6;
			auto out = fmt::format_to(ctx.out(), "*{}[", reply.elements.size());
			for (size_t i = 0; i < reply.elements.size() && i < SHOWN; ++i)
				out = fmt::format_to(out,
									 "{}{}",
									 i ? ", " : "",
									 reply.elements[i]);
			if (reply.elements.size() > SHOWN)
				out = fmt::format_to(out, ", ...");
			return fmt::format_to(out, "]");
		}
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
	case REDIS_REPLY_ARRAY: {
		std::vector<Reply> elements;
		elements.reserve(r.elements);
		for (size_t i = 0; i < r.elements; ++i)
			elements.push_back(to_reply(*r.element[i]));
		return Reply::array(std::move(elements));
	}
	default: return Reply::other(fmt::format("type {}", r.type));
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

/// A request whose argument count is known only at run time.
std::string encode_argv(const std::vector<std::string_view> &args) {
	std::vector<const char *> argv;
	std::vector<size_t> lens;
	for (const auto a : args) {
		argv.push_back(a.data());
		lens.push_back(a.size());
	}
	char *cmd      = nullptr;
	const auto len = redisFormatCommandArgv(&cmd,
											static_cast<int>(args.size()),
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
				// Bytes left over are the start of the next reply.
				partial_ = reader_->pos < reader_->len;
				return to_reply(*reply);
			}

			std::array<char, 65536> buf;
			boost::system::error_code ec;
			const size_t n = socket_.read_some(asio::buffer(buf), ec);
			if (ec == asio::error::eof || ec == asio::error::connection_reset ||
				ec == asio::error::connection_aborted) {
				// Bytes received since the last whole reply: a truncated one.
				// (Counted here, not from the reader's buffer: an array whose
				// header has been parsed holds no unparsed bytes.)
				if (partial_)
					throw Failure(fmt::format("connection closed in the middle "
											  "of a reply ({} unparsed bytes)",
											  reader_->len - reader_->pos));
				return std::nullopt;
			}
			if (ec) throw Failure(fmt::format("read failed: {}", ec.message()));
			partial_ = true;
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
	/// Bytes have arrived that are not yet part of a returned reply.
	bool partial_ = false;
};

/// What differs between two replies, down to the first differing byte, since
/// a truncated printout of two long values can look identical.
std::string diff(const Reply &want, const Reply &got) {
	if (want.kind == Reply::Kind::Array && got.kind == Reply::Kind::Array) {
		const auto &w = want.elements;
		const auto &g = got.elements;
		for (size_t i = 0; i < w.size() && i < g.size(); ++i)
			if (!(w[i] == g[i]))
				return fmt::format("element {} of the array: {}",
								   i,
								   diff(w[i], g[i]));
		return fmt::format("expected {} elements, got {}: expected {}, got {}",
						   w.size(),
						   g.size(),
						   want,
						   got);
	}
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
		  rng_(opt.seed * 1'000'003 + id),
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
			check_one({.wire     = encode({"GET", key}),
					   .expected = it == strs_.end() ? Reply::nil()
													 : Reply::bulk(it->second),
					   .check    = Check::Exact,
					   .what     = fmt::format("sweep GET {}", key)});
		}
		for (unsigned k = 0; k < COUNTERS; ++k) {
			const auto key = counter_key(k);
			const auto it  = counters_.find(key);
			check_one(
				{.wire     = encode({"GET", key}),
				 .expected = it == counters_.end()
								 ? Reply::nil()
								 : Reply::bulk(fmt::to_string(it->second)),
				 .check    = Check::Exact,
				 .what     = fmt::format("sweep GET {}", key)});
		}
		for (unsigned k = 0; k < LISTS; ++k) {
			const auto key = list_key(k);
			const auto it  = lists_.find(key);
			check_one({.wire     = encode({"LRANGE", key, "0", "-1"}),
					   .expected = it == lists_.end()
									   ? Reply::array({})
									   : Reply::bulks(it->second),
					   .check    = Check::Exact,
					   .what     = fmt::format("sweep LRANGE {} 0 -1", key)});
		}
		for (unsigned k = 0; k < SETS; ++k) {
			const auto key = set_key(k);
			const auto it  = sets_.find(key);
			check_one({.wire     = encode({"SMEMBERS", key}),
					   .expected = it == sets_.end() ? Reply::array({})
													 : Reply::bulks(it->second),
					   .check    = Check::Unordered,
					   .what     = fmt::format("sweep SMEMBERS {}", key)});
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
	static constexpr unsigned KEYS     = 24;
	static constexpr unsigned COUNTERS = 4;
	static constexpr unsigned LISTS    = 4;
	static constexpr unsigned SETS     = 4;
	/// Set members come from a small pool, so adds and removes often find
	/// the member already there, or already gone.
	static constexpr unsigned MEMBERS   = 12;
	static constexpr size_t BATCH_BYTES = 32 * 1024;
	static constexpr size_t LARGE_VALUE = 16 * 1024;

	/// Unordered: an array whose order the server chooses (SMEMBERS), compared
	/// sorted; the expected reply is built sorted.
	enum class Check { Exact, Unordered, SharedCounter };

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

	[[nodiscard]] std::string list_key(unsigned k) const {
		return fmt::format("{}l{}", prefix_, k);
	}

	[[nodiscard]] std::string set_key(unsigned k) const {
		return fmt::format("{}s{}", prefix_, k);
	}

	/// An LRANGE index: inside the list, at its ends, past them, or negative.
	long long list_index(size_t len) {
		const auto span = static_cast<long long>(len) + 3;
		return static_cast<long long>(rng_() %
									  static_cast<uint64_t>(2 * span)) -
			   span;
	}

	/// LRANGE as Redis defines it: negative indexes count from the end, and
	/// the range is clamped to the list.
	static std::vector<std::string> lrange(const std::deque<std::string> &list,
										   long long start, long long stop) {
		const auto n = static_cast<long long>(list.size());
		if (start < 0) start += n;
		if (stop < 0) stop += n;
		start = std::max(start, 0LL);
		stop  = std::min(stop, n - 1);
		std::vector<std::string> out;
		for (auto i = start; i <= stop; ++i)
			out.push_back(list[static_cast<size_t>(i)]);
		return out;
	}

	/// MGET of a few of this client's string keys, repeats allowed.
	Op mget_op() {
		std::vector<std::string> keys;
		std::vector<Reply> values;
		for (auto n = 1 + rng_() % 5; n; --n) {
			keys.push_back(str_key(static_cast<unsigned>(rng_() % KEYS)));
			const auto it = strs_.find(keys.back());
			values.push_back(it == strs_.end() ? Reply::nil()
											   : Reply::bulk(it->second));
		}
		std::vector<std::string_view> args{"MGET"};
		args.insert(args.end(), keys.begin(), keys.end());
		return {.wire     = encode_argv(args),
				.expected = Reply::array(std::move(values)),
				.what     = fmt::format("MGET {}", fmt::join(keys, " "))};
	}

	Op list_op() {
		const auto key  = list_key(static_cast<unsigned>(rng_() % LISTS));
		auto &list      = lists_[key]; // erased below if it ends up empty
		const auto roll = rng_() % 100;
		Op op;
		if (roll < 40) {
			const bool head = rng_() % 2 == 0;
			std::vector<std::string> pushed;
			for (auto n = 1 + rng_() % 3; n; --n)
				pushed.push_back(
					fmt::format("w{}:{}:{}", id_, seq_, pushed.size()));
			for (const auto &e : pushed)
				if (head) list.push_front(e);
				else list.push_back(e);
			std::vector<std::string_view> args{head ? "LPUSH" : "RPUSH", key};
			args.insert(args.end(), pushed.begin(), pushed.end());
			op.wire = encode_argv(args);
			op.expected =
				Reply::integer_of(static_cast<long long>(list.size()));
			op.what =
				fmt::format("{} {} {}", args[0], key, fmt::join(pushed, " "));
		} else if (roll < 60) {
			const bool head            = rng_() % 2 == 0;
			const std::string_view cmd = head ? "LPOP" : "RPOP";
			op.wire                    = encode({cmd, key});
			op.what                    = fmt::format("{} {}", cmd, key);
			if (list.empty()) {
				op.expected = Reply::nil();
			} else if (head) {
				op.expected = Reply::bulk(list.front());
				list.pop_front();
			} else {
				op.expected = Reply::bulk(list.back());
				list.pop_back();
			}
		} else if (roll < 90) {
			const auto start = list_index(list.size());
			const auto stop  = list_index(list.size());
			op.wire          = encode(
				{"LRANGE", key, fmt::to_string(start), fmt::to_string(stop)});
			op.expected = Reply::bulks(lrange(list, start, stop));
			op.what     = fmt::format("LRANGE {} {} {}", key, start, stop);
		} else {
			op.wire = encode({"LLEN", key});
			op.expected =
				Reply::integer_of(static_cast<long long>(list.size()));
			op.what = fmt::format("LLEN {}", key);
		}
		if (list.empty()) lists_.erase(key);
		return op;
	}

	Op set_op() {
		const auto key  = set_key(static_cast<unsigned>(rng_() % SETS));
		auto &set       = sets_[key]; // erased below if it ends up empty
		const auto roll = rng_() % 100;
		Op op;
		if (roll < 60) {
			const bool add = roll < 35;
			std::vector<std::string> members;
			for (auto n = 1 + rng_() % 3; n; --n)
				members.push_back(fmt::format("m{}", rng_() % MEMBERS));
			long long changed = 0;
			for (const auto &m : members)
				changed += add ? (set.insert(m).second ? 1 : 0)
							   : static_cast<long long>(set.erase(m));
			std::vector<std::string_view> args{add ? "SADD" : "SREM", key};
			args.insert(args.end(), members.begin(), members.end());
			op.wire     = encode_argv(args);
			op.expected = Reply::integer_of(changed);
			op.what =
				fmt::format("{} {} {}", args[0], key, fmt::join(members, " "));
		} else if (roll < 80) {
			op.wire     = encode({"SMEMBERS", key});
			op.expected = Reply::bulks(set); // a std::set: already sorted
			op.check    = Check::Unordered;
			op.what     = fmt::format("SMEMBERS {}", key);
		} else if (roll < 90) {
			const auto member = fmt::format("m{}", rng_() % MEMBERS);
			op.wire           = encode({"SISMEMBER", key, member});
			op.expected       = Reply::integer_of(set.contains(member) ? 1 : 0);
			op.what           = fmt::format("SISMEMBER {} {}", key, member);
		} else {
			op.wire     = encode({"SCARD", key});
			op.expected = Reply::integer_of(static_cast<long long>(set.size()));
			op.what     = fmt::format("SCARD {}", key);
		}
		if (set.empty()) sets_.erase(key);
		return op;
	}

	/// "w3:41:" then bytes. Mostly short and printable, sometimes long and
	/// binary with CR, LF, '$' and '*' inside, to stress the framing.
	std::string make_value(bool &large) {
		std::string v   = fmt::format("w{}:{}:", id_, seq_);
		const auto roll = rng_() % 100;
		size_t len;
		bool binary = false;
		if (roll < 90) {
			len = rng_() % 65;
		} else if (roll < 98) {
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

		if (roll < 12) return list_op();
		if (roll < 22) return set_op();
		if (roll < 27) return mget_op();
		if (roll < 45) {
			const auto v = make_value(op.large);
			op.wire      = encode({"SET", key, v});
			op.expected  = Reply::status("OK");
			op.what      = fmt::format("SET {} {}", key, printable(v, 24));
			strs_[key]   = v;
		} else if (roll < 62) {
			const auto it = strs_.find(key);
			op.wire       = encode({"GET", key});
			op.expected =
				it == strs_.end() ? Reply::nil() : Reply::bulk(it->second);
			op.what = fmt::format("GET {}", key);
		} else if (roll < 68) {
			op.wire     = encode({"DEL", key});
			op.expected = Reply::integer_of(strs_.erase(key) ? 1 : 0);
			op.what     = fmt::format("DEL {}", key);
		} else if (roll < 72) {
			op.wire     = encode({"EXISTS", key});
			op.expected = Reply::integer_of(strs_.contains(key) ? 1 : 0);
			op.what     = fmt::format("EXISTS {}", key);
		} else if (roll < 79) {
			std::string tail = fmt::format("<{}>", seq_);
			for (auto n = rng_() % 200; n; --n)
				tail += static_cast<char>(rng_() & 0xff);
			op.wire = encode({"APPEND", key, tail});
			auto &v = strs_[key];
			v += tail;
			op.expected = Reply::integer_of(static_cast<long long>(v.size()));
			op.what     = fmt::format("APPEND {} {}", key, printable(tail, 24));
			op.large    = v.size() > LARGE_VALUE;
		} else if (roll < 82) {
			const auto it = strs_.find(key);
			op.wire       = encode({"STRLEN", key});
			op.expected   = Reply::integer_of(
				it == strs_.end() ? 0
								  : static_cast<long long>(it->second.size()));
			op.what = fmt::format("STRLEN {}", key);
		} else if (roll < 90) {
			const auto ck =
				counter_key(static_cast<unsigned>(rng_() % COUNTERS));
			op.wire     = encode({"INCR", ck});
			op.expected = Reply::integer_of(++counters_[ck]);
			op.what     = fmt::format("INCR {}", ck);
		} else if (roll < 96) {
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
		} else if (op.check == Check::Unordered &&
				   got.kind == Reply::Kind::Array) {
			Reply sorted = got;
			std::ranges::sort(sorted.elements, {}, &Reply::text);
			if (!(sorted == op.expected))
				fail(fmt::format(
					"reply differs from the model (order aside): {}",
					diff(op.expected, sorted)));
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
	std::unordered_map<std::string, std::deque<std::string>> lists_;
	std::unordered_map<std::string, std::set<std::string>> sets_;
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
		std::string_view name;
		std::string wire;
		std::vector<Reply> replies;
	};

	const std::string big(3000, 'x');
	const std::array<Case, 8> cases = {{
		{.name    = "RESP SET",
		 .wire    = encode({"SET", "frag:k", "hello"}),
		 .replies = {Reply::status("OK")}},
		{.name    = "RESP GET",
		 .wire    = encode({"GET", "frag:k"}),
		 .replies = {Reply::bulk("hello")}},
		{.name    = "RESP PING",
		 .wire    = encode({"PING"}),
		 .replies = {Reply::status("PONG")}},
		{.name    = "inline PING",
		 .wire    = "PING\r\n",
		 .replies = {Reply::status("PONG")}},
		{.name    = "two inline",
		 .wire    = "PING\r\nPING\r\n",
		 .replies = {Reply::status("PONG"), Reply::status("PONG")}},
		{.name    = "RESP then inline",
		 .wire    = fmt::format("{}PING\r\n", encode({"PING"})),
		 .replies = {Reply::status("PONG"), Reply::status("PONG")}},
		{.name = "binary value",
		 .wire = fmt::format(
			 "{}{}",
			 encode({"SET", "frag:b", std::string("a\r\n$3\r\nb*1\r\n", 12)}),
			 encode({"GET", "frag:b"})),
		 .replies = {Reply::status("OK"),
					 Reply::bulk(std::string("a\r\n$3\r\nb*1\r\n", 12))}},
		{.name    = "3000-byte value",
		 .wire    = fmt::format("{}{}",
								encode({"SET", "frag:l", big}),
								encode({"GET", "frag:l"})),
		 .replies = {Reply::status("OK"), Reply::bulk(big)}},
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
// Same-key race
// --------------------------------------------------------------------------

std::int64_t now_ns() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			   Clock::now().time_since_epoch())
		.count();
}

/// What the n-th SET of a client writes, and the id the checker knows it by.
/// Every value is written once, so a GET's reply names the SET it read.
constexpr unsigned RACE_ID_BITS = 40;

lin::ValueId race_id(unsigned client, std::uint64_t n) {
	return (std::uint64_t{client} + 1) << RACE_ID_BITS | n;
}

std::string race_value(lin::ValueId id) {
	if (id == lin::ABSENT) return "(nil)";
	return fmt::format("v:{}:{}",
					   (id >> RACE_ID_BITS) - 1,
					   id & ((std::uint64_t{1} << RACE_ID_BITS) - 1));
}

std::optional<lin::ValueId> parse_race_value(std::string_view text) {
	unsigned client = 0;
	std::uint64_t n = 0;
	if (!text.starts_with("v:")) return std::nullopt;
	const char *p   = text.data() + 2;
	const char *end = text.data() + text.size();
	auto parsed     = std::from_chars(p, end, client);
	if (parsed.ec != std::errc{} || parsed.ptr == end || *parsed.ptr != ':')
		return std::nullopt;
	parsed = std::from_chars(parsed.ptr + 1, end, n);
	if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
	return race_id(client, n);
}

/// One client of the race: sends a pipeline per round and records what it
/// sent, what came back and when, for the checker.
class Racer {
public:
	Racer(unsigned id, const Options &opt, std::string key)
		: id_(id),
		  opt_(opt),
		  key_(std::move(key)),
		  rng_(opt.seed * 7919 + id),
		  conn_(opt) {}

	/// One pipeline. Throws Failure on a reply that is malformed for its
	/// request; whether the replies fit together is the checker's business.
	void round() {
		ops_.clear();
		shown_.clear();
		const auto depth = 1 + rng_() % opt_.race_pipeline;
		std::string wire;
		for (unsigned i = 0; i < depth; ++i) {
			const auto roll = rng_() % 10;
			lin::Op op;
			if (roll < 4) {
				op.kind  = lin::OpKind::Set;
				op.value = race_id(id_, ++writes_);
				wire += encode({"SET", key_, race_value(op.value)});
			} else if (roll < 8) {
				op.kind = lin::OpKind::Get;
				wire += encode({"GET", key_});
			} else {
				op.kind = lin::OpKind::Del;
				wire += encode({"DEL", key_});
			}
			ops_.push_back(op);
		}

		const auto sent = now_ns();
		conn_.send(wire);
		for (auto &op : ops_) {
			op.sent        = sent;
			const auto got = conn_.expect_reply();
			op.completed   = now_ns();
			interpret(op, got);
			g_verified.fetch_add(1, std::memory_order_relaxed);
			note_progress();
		}
	}

	[[nodiscard]] const std::vector<lin::Op> &ops() const { return ops_; }

	/// This round's requests and replies, one per line, times relative to
	/// \p origin.
	[[nodiscard]] std::string describe(std::int64_t origin) const {
		std::string out;
		for (size_t i = 0; i < ops_.size(); ++i)
			fmt::format_to(
				std::back_inserter(out),
				"    client {} #{}: {:<30} sent +{} us, done +{} us\n",
				id_,
				i,
				shown_[i],
				(ops_[i].sent - origin) / 1000,
				(ops_[i].completed - origin) / 1000);
		return out;
	}

private:
	void interpret(lin::Op &op, const Reply &got) {
		switch (op.kind) {
		case lin::OpKind::Set:
			if (got != Reply::status("OK"))
				throw Failure(
					fmt::format("race client {}: SET answered {}", id_, got));
			shown_.push_back(fmt::format("SET {} -> OK", race_value(op.value)));
			break;
		case lin::OpKind::Del:
			if (got.kind != Reply::Kind::Integer || got.integer < 0 ||
				got.integer > 1)
				throw Failure(
					fmt::format("race client {}: DEL answered {}", id_, got));
			op.removed = got.integer == 1;
			shown_.push_back(fmt::format("DEL -> {}", got.integer));
			break;
		case lin::OpKind::Get:
			if (got.kind == Reply::Kind::Nil) {
				op.value = lin::ABSENT;
			} else if (const auto id = got.kind == Reply::Kind::Bulk
										   ? parse_race_value(got.text)
										   : std::nullopt) {
				op.value = *id;
			} else {
				throw Failure(fmt::format(
					"race client {}: GET answered {}, which no SET wrote",
					id_,
					got));
			}
			shown_.push_back(fmt::format("GET -> {}", race_value(op.value)));
			break;
		}
	}

	unsigned id_;
	const Options &opt_;
	std::string key_;
	std::mt19937_64 rng_;
	Connection conn_;
	std::uint64_t writes_ = 0;
	std::vector<lin::Op> ops_;
	std::vector<std::string> shown_;
};

/// Clients race SET, DEL and GET on one key, round after round. A barrier ends
/// each round with nothing in flight, so the rounds can be checked one at a
/// time, each from every value the round before may have left.
void same_key_race(const Options &opt) {
	const auto key = fmt::format("r{}:race", opt.run_id);
	std::vector<std::unique_ptr<Racer>> racers;
	for (unsigned i = 0; i < opt.race_clients; ++i)
		racers.push_back(std::make_unique<Racer>(i, opt, key));

	std::vector<lin::ValueId> possible{lin::ABSENT};
	std::vector<std::string> errors(opt.race_clients);
	std::string failure;
	std::atomic<bool> client_failed{false};
	bool stop       = false;
	unsigned rounds = 0;
	Clock::duration checking{};

	// Runs once per round, after every client has arrived and before any
	// leaves: the clients' records are stable while it reads them.
	const auto end_of_round = [&]() noexcept {
		if (client_failed.load()) {
			stop = true;
			return;
		}
		try {
			lin::Segment segment;
			std::int64_t origin = std::numeric_limits<std::int64_t>::max();
			for (const auto &r : racers) {
				segment.clients.push_back(r->ops());
				for (const auto &op : r->ops())
					origin = std::min(origin, op.sent);
			}
			const auto check_started = Clock::now();
			auto after               = lin::check(possible, segment);
			checking += Clock::now() - check_started;
			if (after.empty()) {
				std::string before;
				for (const auto v : possible)
					fmt::format_to(std::back_inserter(before),
								   " {}",
								   race_value(v));
				std::string requests;
				for (const auto &r : racers) requests += r->describe(origin);
				failure = fmt::format(
					"same-key race, round {}: no order of these requests "
					"explains "
					"the replies (each client's order and real time kept)\n"
					"  the key could hold before the round:{}\n{}"
					"  replay with --seed {}",
					rounds + 1,
					before,
					requests,
					opt.seed);
				stop = true;
				return;
			}
			possible = std::move(after);
		} catch (const std::exception &e) {
			failure =
				fmt::format("same-key race: checker failed: {}", e.what());
			stop = true;
			return;
		}
		stop = ++rounds == opt.race_rounds;
	};
	std::barrier sync(static_cast<std::ptrdiff_t>(opt.race_clients),
					  end_of_round);

	std::vector<std::thread> threads;
	for (unsigned i = 0; i < opt.race_clients; ++i)
		threads.emplace_back([&, i] {
			try {
				while (!stop) {
					racers[i]->round();
					sync.arrive_and_wait();
				}
			} catch (const std::exception &e) {
				errors[i] = e.what();
				client_failed.store(true);
				sync.arrive_and_drop();
			}
		});
	for (auto &t : threads) t.join();

	for (const auto &e : errors)
		if (!e.empty()) throw Failure(e);
	if (!failure.empty()) throw Failure(failure);
	std::printf("  %u rounds linearizable (checking took %.1f s)\n",
				rounds,
				std::chrono::duration<double>(checking).count());
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
	app.add_option("--race-clients",
				   opt.race_clients,
				   "Clients racing on one key in the linearizability phase")
		->check(CLI::Range(1, static_cast<int>(lin::MAX_CLIENTS)))
		->capture_default_str();
	app.add_option("--race-rounds",
				   opt.race_rounds,
				   "Rounds of the linearizability phase; 0 skips it")
		->capture_default_str();
	app.add_option("--race-pipeline",
				   opt.race_pipeline,
				   "Largest pipeline depth in the linearizability phase")
		->check(CLI::Range(1, static_cast<int>(lin::MAX_OPS)))
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
		const Shared shared{
			.counter_key = fmt::format("r{}:shared:counter", opt.run_id),
			.set_key     = fmt::format("r{}:shared:set", opt.run_id)};
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

		if (run.failures.empty() && opt.race_rounds > 0) {
			std::printf("phase 4: same-key race (%u clients, %u rounds)\n",
						opt.race_clients,
						opt.race_rounds);
			same_key_race(opt);
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
