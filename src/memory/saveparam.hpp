#pragma once
#include <chrono>

namespace redis {

struct saveparam {
	std::chrono::seconds interval;
	int changes;
};
} // namespace redis
