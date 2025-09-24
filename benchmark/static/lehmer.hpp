#include "../hash.hpp"
#include "../string_compare/strategies.hpp"
#include "table.hpp"

#include <arm_neon.h>
#include <string_view>

constexpr const table_entry *
look_up_identifier(const command_table<lehmer64_engine, X> &table,
				   std::string_view identifier) noexcept {
	if (identifier.size() < min_keyword_size ||
		identifier.size() > max_keyword_size)
		return nullptr;


	return table[identifier];

#if defined(__x86_64__)
	auto check_length_cmov = [&]() -> void {
		__asm__(
			// If what should be the null terminator is not null, then
			// (size != strlen(entry_keyword)), so set result to
			// token_type::identifier.
			"cmpb $0, %[entry_keyword_at_size]\n"
			"cmovne %[token_type_identifier], %[result]\n"

			: [result] "+r"(result)

			: [entry_keyword_at_size] "m"(entry_keyword[size]),
			  [token_type_identifier] "r"((int)token_type::identifier)

			: "cc" // Clobbered by cmp.
		);
	};
#endif
}
