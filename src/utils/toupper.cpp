#include "toupper.hpp"

#include <algorithm>

namespace redis {

std::string toupper(std::string s) {
	std::ranges::transform(s, s.begin(), [](unsigned char c) {
		return std::tolower(c);
	});
	return s;
}
} // namespace redis
