// Fuzzes resp::Parser::parse (DX-86, #51).
//
// TC-300: any input is answered with a Value or a RespError. Crashes, UB,
// unbounded recursion and oversized allocations are caught by the sanitizers
// and by libFuzzer's -malloc_limit_mb, not checked here.
// TC-308: the encoder never writes what the parser cannot read back, and
// encoding a value that went through the parser changes nothing.

#include "common/resp/parser.hpp"
#include "common/resp/value.hpp"
#include "core/error/verify.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {
using redis::resp::Parser;
using redis::resp::Value;

std::string encode(const Value &value) { return fmt::format("{:e}", value); }

/// encode(parse(encode(v))) == encode(v). Not parse(encode(v)) == v: the
/// encoder turns a line break in a simple string into a space, and a double
/// need not print back to the same bits, so only the second encoding is
/// required to be stable.
void check_encoding_is_idempotent(const Value &value) {
	const std::string once = encode(value);
	const auto reparsed    = Parser::parse(once);
	REDIS_VERIFY(reparsed.has_value(),
				 "the parser rejects what the encoder wrote");
	REDIS_VERIFY(encode(*reparsed) == once,
				 "encoding a parsed value again changed its bytes");
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	const std::string_view input(reinterpret_cast<const char *>(data), size);
	if (const auto value = Parser::parse(input))
		check_encoding_is_idempotent(*value);
	return 0;
}
