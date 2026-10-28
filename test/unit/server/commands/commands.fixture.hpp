#pragma once
#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/persistence/persistence.hpp"
#include "server/storage/database.hpp"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace redis::test {

/// A unique path in the system temp directory, removed on destruction.
class TempFile {
public:
	TempFile() {
		static std::atomic<int> counter{0};
		path_ = std::filesystem::temp_directory_path() /
				("redis_test_" + std::to_string(counter++) + "_" +
				 std::to_string(std::chrono::steady_clock::now()
									.time_since_epoch()
									.count()) +
				 ".snap");
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

/// Everything a command needs, in-process: no sockets, no event loop.
struct Server {
	TempFile dump;
	Database db;
	Persistence persistence{dump.path()};
	CommandContext ctx{db, persistence};

	/// Run one command and return the RESP-encoded reply, exactly the bytes
	/// a client would receive.
	std::string run(std::vector<std::string> argv) {
		static const CommandHandler handler;
		return handler.execute(ctx, resp::Value::from_raw_array(argv));
	}
};

} // namespace redis::test
