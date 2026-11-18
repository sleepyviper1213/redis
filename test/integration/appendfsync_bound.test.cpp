// TC-261: the data-loss bound of each --appendfsync policy (NFR-003, DX-77).
//
// Starts its own server binary with --appendonly, kills it with SIGKILL, and
// restarts it on the same directory. Runs only where the suite is given a
// server binary (scripts/test_server.sh sets REDIS_TEST_SERVER_BIN), and only
// on POSIX, which has fork() and SIGKILL. Elsewhere it skips.

#include "test_client.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
namespace {

namespace fs = std::filesystem;

/// Starts \p binary on \p port with the append-only file in \p dir and the
/// given fsync policy. The snapshot file is in \p dir too.
pid_t spawn_server(const std::string &binary, int port, const fs::path &dir,
				   const std::string &policy) {
	const std::string port_text = std::to_string(port);
	const std::string dump_text = (dir / "dump.snap").string();
	const std::string aof_text  = (dir / "aof").string();
	const pid_t pid             = fork();
	if (pid == 0) {
		execl(binary.c_str(), binary.c_str(), "--port", port_text.c_str(),
			  "--dump", dump_text.c_str(), "--appendonly", "--appenddir",
			  aof_text.c_str(), "--appendfsync", policy.c_str(),
			  static_cast<char *>(nullptr));
		_exit(127);
	}
	return pid;
}

/// Whether a client can connect to \p port within about 5 s.
bool wait_until_listening(int port) {
	for (int i = 0; i < 100; ++i) {
		if (TestClient("127.0.0.1", port).is_connected()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return false;
}

/// SIGKILL: no final save and no flush by the server on the way out.
void kill_hard(pid_t pid) {
	kill(pid, SIGKILL);
	waitpid(pid, nullptr, 0);
}

/// Whether \p key exists after a restart.
bool exists_on(int port, const std::string &key) {
	TestClient client("127.0.0.1", port);
	const auto r = client.send("EXISTS %s", key.c_str());
	return r.ok && r.value == "1";
}

/// Sets \p key, on a fresh connection, and returns once the reply arrived.
void set_key(int port, const std::string &key) {
	TestClient client("127.0.0.1", port);
	REQUIRE(client.is_connected());
	const auto r = client.send("SET %s v", key.c_str());
	REQUIRE(r.ok);
}

} // namespace

TEST_CASE("TC-261: everysec keeps every write older than a second, always "
		  "keeps every acknowledged write",
		  "[appendfsync][integration]") {
	const char *binary = std::getenv("REDIS_TEST_SERVER_BIN");
	if (binary == nullptr)
		SKIP("REDIS_TEST_SERVER_BIN is not set; run through scripts/test_server.sh");

	const int port = test_port() + 2;
	const fs::path dir =
		fs::temp_directory_path() / ("tc261-" + std::to_string(getpid()));

	SECTION("everysec: writes older than a second survive a SIGKILL") {
		fs::remove_all(dir);
		fs::create_directories(dir);

		pid_t pid = spawn_server(binary, port, dir, "everysec");
		REQUIRE(pid > 0);
		REQUIRE(wait_until_listening(port));

		set_key(port, "old1");
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		set_key(port, "old2");
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		// Written just before the kill: it may or may not be on disk.
		set_key(port, "recent");
		kill_hard(pid);

		pid = spawn_server(binary, port, dir, "everysec");
		REQUIRE(pid > 0);
		REQUIRE(wait_until_listening(port));
		CHECK(exists_on(port, "old1"));
		CHECK(exists_on(port, "old2"));
		kill_hard(pid);

		fs::remove_all(dir);
	}

	SECTION("always: every acknowledged write survives a SIGKILL") {
		fs::remove_all(dir);
		fs::create_directories(dir);

		pid_t pid = spawn_server(binary, port, dir, "always");
		REQUIRE(pid > 0);
		REQUIRE(wait_until_listening(port));

		set_key(port, "a1");
		set_key(port, "a2");
		set_key(port, "a3");
		kill_hard(pid);

		pid = spawn_server(binary, port, dir, "always");
		REQUIRE(pid > 0);
		REQUIRE(wait_until_listening(port));
		CHECK(exists_on(port, "a1"));
		CHECK(exists_on(port, "a2"));
		CHECK(exists_on(port, "a3"));
		kill_hard(pid);

		fs::remove_all(dir);
	}
}
#endif
