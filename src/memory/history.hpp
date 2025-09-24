#pragma once

#include <string_view>

namespace redis {
struct History {
	std::string_view since;
	std::string_view changes;
};
} // namespace redis
