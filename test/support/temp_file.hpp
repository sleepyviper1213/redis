#pragma once
#include <fmt/format.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace redis::test {

/// A unique path in the system temp directory, removed on destruction.
class TempFile {
public:
	TempFile() {
		static std::atomic<int> counter{0};
		path_ = std::filesystem::temp_directory_path() /
				fmt::format("redis_test_{}_{}.snap",
							counter++,
							std::chrono::steady_clock::now()
								.time_since_epoch()
								.count());
	}
	TempFile(const TempFile &)            = delete;
	TempFile &operator=(const TempFile &) = delete;
	~TempFile() {
		std::error_code ignored;
		std::filesystem::remove(path_, ignored);
	}

	[[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
	std::filesystem::path path_;
};

} // namespace redis::test
