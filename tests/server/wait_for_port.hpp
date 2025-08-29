#pragma once
#include <arpa/inet.h>

#include <sys/_endian.h>
#include <sys/socket.h>

#include <chrono>
#include <thread>
#include <unistd.h>

bool wait_for_port(const char *host, pid_t port,
				   std::chrono::milliseconds timeout) {
	auto deadline =
		std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

	while (std::chrono::steady_clock::now() < deadline) {
		int sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock >= 0) {
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port   = htons(port);
			inet_pton(AF_INET, host, &addr.sin_addr);

			const int result = connect(sock, (sockaddr *)&addr, sizeof(addr));
			close(sock);

			if (result == 0) return true; // port is ready
		}
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(200ms);
	}
	return false;
}
