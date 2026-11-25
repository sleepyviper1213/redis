// TC-262: a FailFast end still saves the final snapshot (FR-017, UC-017 A2).
//
// Starts its own server binary, so it runs only where the suite is given one
// (scripts/test_server.sh sets REDIS_TEST_SERVER_BIN) and only on POSIX, which
// has fork(). It needs no server of its own otherwise: it skips.

#include "test_client.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

using Catch::Matchers::ContainsSubstring;
#endif

#if !defined(_WIN32)
namespace {

namespace fs = std::filesystem;

// The housekeeping tick is 100 ms: the injected failure comes about 2 s after
// the start, which leaves the test time to connect and write.
constexpr unsigned FAULT_AFTER_TICKS = 20;

/// Starts \p binary on \p port with \p dump as its snapshot file.
pid_t spawn_server(const std::string &binary, int port, const fs::path &dump) {
	const std::string port_text = std::to_string(port);
	const std::string dump_text = dump.string();
	const std::string fault_text = std::to_string(FAULT_AFTER_TICKS);
	const pid_t pid = fork();
	if (pid == 0) {
		setenv("REDIS_TEST_FAIL_HOUSEKEEPING_AFTER", fault_text.c_str(), 1);
		execl(binary.c_str(), binary.c_str(), "--port", port_text.c_str(),
			  "--dump", dump_text.c_str(), static_cast<char *>(nullptr));
		_exit(127);
	}
	return pid;
}

/// Exit status of \p pid, or -1 if it did not exit within 15 s (then killed).
int wait_for_exit(pid_t pid) {
	for (int i = 0; i < 150; ++i) {
		int status = 0;
		if (waitpid(pid, &status, WNOHANG) == pid) {
			return WIFEXITED(status) ? WEXITSTATUS(status)
									 : 128 + WTERMSIG(status);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	kill(pid, SIGKILL);
	waitpid(pid, nullptr, 0);
	return -1;
}

/// Whether a client can connect to \p port within about 5 s.
bool wait_until_listening(int port) {
	for (int i = 0; i < 100; ++i) {
		if (TestClient("127.0.0.1", port).is_connected()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return false;
}

} // namespace

TEST_CASE("TC-262: A FailFast end still saves the final snapshot and exits "
		  "non-zero",
		  "[failfast][integration]") {
	const char *binary = std::getenv("REDIS_TEST_SERVER_BIN");
	if (binary == nullptr)
		SKIP("REDIS_TEST_SERVER_BIN is not set; run through scripts/test_server.sh");

	const int port = test_port() + 1;
	const fs::path dir =
		fs::temp_directory_path() / ("tc262-" + std::to_string(getpid()));
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path dump = dir / "dump.snap";

	const pid_t pid = spawn_server(binary, port, dump);
	REQUIRE(pid > 0);
	REQUIRE(wait_until_listening(port));

	{
		TestClient client("127.0.0.1", port);
		REQUIRE(client.is_connected());
		const auto set = client.send("SET tc262-key tc262-value");
		REQUIRE(set.ok);
	}

	const int status = wait_for_exit(pid);
	CHECK(status != 0);
	CHECK(status != -1);

	// The snapshot holds the write made before the failure. Keys and values
	// are byte strings in the file, so they appear verbatim.
	REQUIRE(fs::exists(dump));
	std::ifstream in(dump, std::ios::binary);
	const std::string bytes((std::istreambuf_iterator<char>(in)),
							std::istreambuf_iterator<char>());
	CHECK_THAT(bytes, ContainsSubstring("tc262-key"));
	CHECK_THAT(bytes, ContainsSubstring("tc262-value"));

	fs::remove_all(dir);
}
#endif
