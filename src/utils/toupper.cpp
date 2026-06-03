#include "utils/toupper.hpp"

#include <algorithm>
#include <cctype>

namespace redis {

std::string ascii_toupper(std::string s) {
	std::ranges::transform(s, s.begin(), [](unsigned char c) {
		return std::toupper(c);
	});
	return s;
}
} // namespace redis
