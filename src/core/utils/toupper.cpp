#include "toupper.hpp"

#include <algorithm>
#include <cctype>

namespace redis {

std::string tolower(std::string s) {
	std::ranges::transform(s, s.begin(), [](unsigned char c) {
		return std::tolower(c);
	});
	return s;
}

std::string toupper(std::string s) {
	std::ranges::transform(s, s.begin(), [](unsigned char c) {
		return std::toupper(c);
	});
	return s;
}
} // namespace redis
