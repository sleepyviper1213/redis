#pragma once
#include <array>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <span>
#include <type_traits>


using character_selection_bits = std::bitset<5>;

constexpr std::array<char, 4>
pack_selected_chars(character_selection_bits mask,
					std::span<const char> s) noexcept {
	assert(mask.count() == 4);

	std::array<char, 4> bytes;
	size_t shift = 0;

	auto f = [&](size_t idx) { bytes[shift++] = s[idx]; };

	if (mask.test(0)) f(0);
	if (mask.test(1)) f(1);
	if (mask.test(2)) f(s.size() - 2);
	if (mask.test(3)) f(s.size() - 1);
	if (mask.test(4)) f(s.size());

	assert(shift == 4);
	return bytes;
}
