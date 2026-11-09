#pragma once
#include <chrono>
#include <cstddef>

namespace redis {

/// Per-server resource limits. The defaults are Redis's (maxclients,
/// client-query-buffer-limit, timeout), except for an output-buffer limit,
/// which Redis leaves unlimited for ordinary clients.
struct NetLimits {
	size_t max_clients       = 10000;
	size_t max_query_buffer  = size_t{1} << 30;   // 1 GiB, as Redis
	size_t max_output_buffer = size_t{256} << 20; // 256 MiB
	/// Close a client that sends nothing for this long; zero never does.
	std::chrono::seconds idle_timeout{0};
};

} // namespace redis
