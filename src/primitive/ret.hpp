#pragma once

#include <chrono>
#include <string>
#include <variant>
#include <vector>

using Ret = std::variant<bool, size_t, std::chrono::seconds, std::string,
						 std::vector<std::string>>;
