#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// Files the server keeps its data in, held by descriptor. Everything after the
// open (size, truncate, fsync, read, write) goes through the descriptor, never
// through the path again: a path can be made to name a different file between
// two uses (a TOCTOU race), a descriptor cannot. The implementation is per
// platform, like the rest of this namespace.
namespace redis::os {

/// How DurableFile::open_existing() opens a file.
enum class Access : uint8_t {
	read,       ///< to read it
	read_write, ///< to read it and cut it short
};

/**
 * \brief An open file for data that must survive a crash and must not be
 * tampered with: the append-only log, the manifest, the directory's lock.
 *
 * What makes it that, over a plain stream:
 *  - it can be forced to disk (sync()) from a thread other than the writer's;
 *  - it can be cut back after a failed write (truncate()), and sized, read and
 *    locked, all through the open descriptor and never by name again;
 *  - files it creates are private to the owner (mode 0600), and none of the
 *    ways to open one for writing follows a symbolic link at the last component
 *    of the path, so a link planted in the directory cannot redirect a write to
 *    some other file;
 *  - failures are values (std::error_code), not exceptions or a stream state.
 *
 * It does not replace a file atomically; that is create_new(), sync(), then
 * rename, as AppendOnlyFile::write_manifest does.
 *
 * Move-only: owns the descriptor and closes it when destroyed.
 */
class DurableFile {
public:
	DurableFile() = default;

	DurableFile(const DurableFile &)            = delete;
	DurableFile &operator=(const DurableFile &) = delete;

	DurableFile(DurableFile &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

	DurableFile &operator=(DurableFile &&other) noexcept {
		if (this != &other) {
			close();
			fd_ = std::exchange(other.fd_, -1);
		}
		return *this;
	}

	~DurableFile() { close(); }

	/// Open \p path for appending (every write goes to the end), creating it if
	/// it is not there.
	[[nodiscard]] static std::expected<DurableFile, std::error_code>
	open_append(const std::filesystem::path &path);

	/// Create \p path, which must not exist (not even as a dangling link).
	[[nodiscard]] static std::expected<DurableFile, std::error_code>
	create_new(const std::filesystem::path &path);

	/// Open the file at \p path, which must exist.
	[[nodiscard]] static std::expected<DurableFile, std::error_code>
	open_existing(const std::filesystem::path &path, Access access);

	[[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

	/// Close it now. Does nothing if it is not open.
	void close() noexcept;

	/// Write all of \p data, carrying on after a short write or an interrupt.
	[[nodiscard]] std::error_code write_all(std::string_view data);

	/// Everything from the start to the end of the file.
	[[nodiscard]] std::expected<std::string, std::error_code> read_all() const;

	/// The size of the open file, whatever its name has become since.
	[[nodiscard]] std::expected<std::uintmax_t, std::error_code> size() const;

	/// Cut the open file to \p size bytes.
	[[nodiscard]] std::error_code truncate(std::uintmax_t size);

	/// Force the file's contents to stable storage. Safe to call from another
	/// thread than the one writing.
	[[nodiscard]] std::error_code sync();

	/**
	 * \brief Take an exclusive lock on the file without waiting for it.
	 *
	 * Held until this DurableFile is closed or the process ends, however it ends. The
	 * error is std::errc::resource_unavailable_try_again when someone else holds
	 * the lock. Each open of the same file is its own holder, so two Files in one
	 * process exclude each other too.
	 */
	[[nodiscard]] std::error_code try_lock();

private:
	explicit DurableFile(int fd) noexcept : fd_(fd) {}

	int fd_ = -1;
};

} // namespace redis::os
