#include "utility.hpp"

#include <ranges>

std::vector<std::string> split_by_space(const std::string &input) {
	return input | std::ranges::views::split(' ') |
		   std::ranges::to<std::vector<std::string>>();
}
