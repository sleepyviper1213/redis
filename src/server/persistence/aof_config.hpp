#pragma once
#include <cstdint>
#include <filesystem>

namespace redis {

/**
 * \brief When the log is forced to disk, as Redis' appendfsync.
 *
 * The enumerators are spelled as the option values, so magic_enum converts
 * between them and the command line and CONFIG GET.
 */
enum class FsyncPolicy : uint8_t {
	always, ///< before the replies of a batch are sent: nothing acknowledged is
			///< lost
	everysec, ///< once a second, off the command thread: at most ~1s is lost
	no,       ///< never; the operating system decides
};

struct AofConfig {
	/// Holds the manifest, the base snapshot and the incremental logs.
	std::filesystem::path dir = "appendonlydir";
	FsyncPolicy fsync         = FsyncPolicy::everysec;
};

} // namespace redis
