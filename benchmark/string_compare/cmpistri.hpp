#pragma once

enum class string_compare_strategy {
	memcmp,
	check_first_then_memcmp,
	check_two_then_memcmp,
	cmpestri,
	sse2,
	ptest,
	neon_mask_test,
	chunk_8_4,
};
