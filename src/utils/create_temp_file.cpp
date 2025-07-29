#include "create_temp_file.hpp"

#include <fmt/chrono.h>

namespace redis {

std::filesystem::path createTempFile() {
	return fmt::format("{}.snap", std::chrono::system_clock::now());
}
} // namespace redis
