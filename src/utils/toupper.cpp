#include "toupper.hpp"

#include <algorithm>
#include <cctype>

namespace redis {

std::string toupper(std::string s) {
	std::ranges::transform(s, s.begin(), [](unsigned char c) {
		return std::toupper(c);
	});
	return s;
}
} // namespace redis
