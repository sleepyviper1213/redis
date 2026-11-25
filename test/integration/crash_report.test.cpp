// TC-224, TC-301, TC-302: a fatal signal gets a crash report, and a report that hangs
// still ends in an abort (UC-018 A2, DX-81).
//
// Starts its own server binary, so it runs only where the suite is given one
// (scripts/test_server.sh sets REDIS_TEST_SERVER_BIN) and only on POSIX, which
// has fork(). Otherwise it skips.

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
using Clock = std::chrono::steady_clock;

/// How a process ended: the signal that killed it, or 0.
struct Ending {
	int signal = 0;
	std::chrono::milliseconds waited{0};
};

/// Starts \p binary on \p port with its log in \p log_file. \p seam, if not
/// null, is a test-seam environment variable to set: the crash report stalls
/// (REDIS_TEST_CRASH_REPORT_HANG) or the server overflows its stack once the
/// handler is installed (REDIS_TEST_CRASH_STACK_OVERFLOW).
pid_t spawn_server(const std::string &binary, int port, const fs::path &log_file,
				   const char *seam) {
	const std::string port_text = std::to_string(port);
	const std::string log_text  = log_file.string();
	const pid_t pid = fork();
	if (pid == 0) {
		if (seam != nullptr) setenv(seam, "1", 1);
		execl(binary.c_str(), binary.c_str(), "--port", port_text.c_str(),
			  "--log-file", log_text.c_str(), static_cast<char *>(nullptr));
		_exit(127);
	}
	return pid;
}

/// The signal that ended \p pid, waiting up to 15 s; -1 if it did not end (then
/// killed).
Ending wait_for_signal(pid_t pid) {
	const auto start = Clock::now();
	for (int i = 0; i < 150; ++i) {
		int status = 0;
		if (waitpid(pid, &status, WNOHANG) == pid) {
			return {WIFSIGNALED(status) ? WTERMSIG(status) : 0,
					std::chrono::duration_cast<std::chrono::milliseconds>(
						Clock::now() - start)};
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	kill(pid, SIGKILL);
	waitpid(pid, nullptr, 0);
	return {-1, std::chrono::milliseconds(15000)};
}

bool wait_until_listening(int port) {
	for (int i = 0; i < 100; ++i) {
		if (TestClient("127.0.0.1", port).is_connected()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return false;
}

std::string read_file(const fs::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Starts a server, faults it with SIGSEGV and reports how it ended.
Ending crash_server(const std::string &binary, int port, const fs::path &log_file,
					const char *seam) {
	const pid_t pid = spawn_server(binary, port, log_file, seam);
	REQUIRE(pid > 0);
	REQUIRE(wait_until_listening(port));
	kill(pid, SIGSEGV);
	return wait_for_signal(pid);
}

} // namespace

TEST_CASE("TC-224: A fault ends with the report and the original signal",
		  "[crash][integration]") {
	const char *binary = std::getenv("REDIS_TEST_SERVER_BIN");
	if (binary == nullptr)
		SKIP("REDIS_TEST_SERVER_BIN is not set; run through scripts/test_server.sh");

	const fs::path dir =
		fs::temp_directory_path() / ("tc224-" + std::to_string(getpid()));
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path log_file = dir / "server.log";

	const auto ending = crash_server(binary, test_port() + 2, log_file, nullptr);
	CHECK(ending.signal == SIGSEGV);

	const std::string log = read_file(log_file);
	CHECK_THAT(log, ContainsSubstring("=== REDIS BUG REPORT START"));
	CHECK_THAT(log, ContainsSubstring("Crashed by signal: SIGSEGV"));
	CHECK_THAT(log, ContainsSubstring("=== REDIS BUG REPORT END"));

	fs::remove_all(dir);
}

TEST_CASE("TC-301: A hung crash report ends in an abort",
		  "[crash][integration]") {
	const char *binary = std::getenv("REDIS_TEST_SERVER_BIN");
	if (binary == nullptr)
		SKIP("REDIS_TEST_SERVER_BIN is not set; run through scripts/test_server.sh");

	const fs::path dir =
		fs::temp_directory_path() / ("tc301-" + std::to_string(getpid()));
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path log_file = dir / "server.log";

	const auto ending = crash_server(binary, test_port() + 3, log_file,
									"REDIS_TEST_CRASH_REPORT_HANG");
	// SIGABRT, not SIGSEGV (the report never finished) and not -1 (it ended).
	CHECK(ending.signal == SIGABRT);
	// The watchdog is 3 s; the poll adds up to 100 ms.
	CHECK(ending.waited >= std::chrono::milliseconds(2500));
	CHECK(ending.waited < std::chrono::milliseconds(6000));

	CHECK(read_file(log_file).find("=== REDIS BUG REPORT END") ==
		  std::string::npos);

	fs::remove_all(dir);
}

TEST_CASE("TC-302: A stack overflow is named in the report",
		  "[crash][integration]") {
	const char *binary = std::getenv("REDIS_TEST_SERVER_BIN");
	if (binary == nullptr)
		SKIP("REDIS_TEST_SERVER_BIN is not set; run through scripts/test_server.sh");

	const fs::path dir =
		fs::temp_directory_path() / ("tc302-" + std::to_string(getpid()));
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path log_file = dir / "server.log";

	// The seam overflows the stack at install time, so the server never listens:
	// wait for it to end, not for it to accept a connection.
	const pid_t pid = spawn_server(binary, test_port() + 4, log_file,
								   "REDIS_TEST_CRASH_STACK_OVERFLOW");
	REQUIRE(pid > 0);
	const auto ending = wait_for_signal(pid);
	CHECK(ending.signal == SIGSEGV);

	const std::string log = read_file(log_file);
	CHECK_THAT(log, ContainsSubstring("Crashed by signal: SIGSEGV"));
	CHECK_THAT(log, ContainsSubstring("Stack overflow"));
	CHECK_THAT(log, ContainsSubstring("=== REDIS BUG REPORT END"));

	fs::remove_all(dir);
}
#endif
