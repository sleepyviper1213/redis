#pragma once
#include <fmt/format.h>

#include <cstdint>
#include <iterator>
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
		fmt::format_to(std::back_inserter(out_),
					   "${}\r\n{}\r\n",
					   text.size(),
					   text);
	}

	/// The header of an array of \p size elements; the elements follow.
	void array(size_t size) { number('*', size); }

	static constexpr std::string_view NOT_INTEGER =
		"ERR value is not an integer or out of range";
	static constexpr std::string_view SYNTAX_ERROR = "ERR syntax error";

	void wrong_type() {
		error("WRONGTYPE Operation against a key holding the wrong kind of "
			  "value");
	}

	void not_integer() { error(NOT_INTEGER); }

	void syntax_error() { error(SYNTAX_ERROR); }

private:
	// A simple string or error ends at the first CRLF, so one inside the text
	// (a command name or argument echoed back, say) would end the reply early
	// and let the rest pass as a reply of its own. Like Redis, turn them into
	// spaces.
	void line(char prefix, std::string_view text) {
		out_ += prefix;
		const auto first = text.find_first_of("\r\n");
		if (first == std::string_view::npos) {
			out_ += text;
		} else {
			const size_t from = out_.size();
			out_ += text;
			for (size_t i = from + first; i < out_.size(); ++i)
				if (out_[i] == '\r' || out_[i] == '\n') out_[i] = ' ';
		}
		out_ += "\r\n";
	}

	template <typename T>
	void number(char prefix, T n) {
		fmt::format_to(std::back_inserter(out_), "{}{}\r\n", prefix, n);
	}

	// A short-lived view over the caller's reply buffer, never rebound.
	std::string
		&out_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

} // namespace redis
