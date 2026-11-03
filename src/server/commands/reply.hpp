#pragma once
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace redis {

/**
 * \brief Appends RESP replies to a client's output buffer.
 *
 * Commands write straight into the buffer a whole batch of replies builds up
 * in, so a reply costs no intermediate object and no second copy. A command
 * writes its reply only once its outcome is known: after the first write there
 * is no way to replace it with an error.
 */
class ReplyWriter {
public:
	explicit ReplyWriter(std::string &out) noexcept : out_(out) {}

	void ok() { out_ += "+OK\r\n"; }
	void simple(std::string_view text) { line('+', text); }
	void error(std::string_view message) { line('-', message); }
	void integer(int64_t n) { number(':', n); }
	void nil() { out_ += "$-1\r\n"; }

	void bulk(std::string_view text) {
		number('$', text.size());
		out_.append(text);
		out_ += "\r\n";
	}

	/// The header of an array of \p size elements; the elements follow.
	void array(size_t size) { number('*', size); }

	static constexpr std::string_view NOT_INTEGER =
		"ERR value is not an integer or out of range";
	static constexpr std::string_view SYNTAX_ERROR = "ERR syntax error";

	void wrong_type() {
		error(
			"WRONGTYPE Operation against a key holding the wrong kind of value");
	}
	void not_integer() { error(NOT_INTEGER); }
	void syntax_error() { error(SYNTAX_ERROR); }

private:
	void line(char prefix, std::string_view text) {
		out_ += prefix;
		out_.append(text);
		out_ += "\r\n";
	}

	// The prefix, the decimal number and the CRLF go out in one append.
	template <typename T>
	void number(char prefix, T n) {
		char buf[32]; // a prefix, any 64-bit decimal, and CRLF
		buf[0]         = prefix;
		const auto end = std::to_chars(buf + 1, buf + sizeof buf - 2, n).ptr;
		end[0]         = '\r';
		end[1]         = '\n';
		out_.append(buf, static_cast<size_t>(end + 2 - buf));
	}

	// A short-lived view over the caller's reply buffer, never rebound.
	std::string &out_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

} // namespace redis
