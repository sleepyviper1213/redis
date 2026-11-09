#pragma once
#include "core/utils/indirect.hpp"
#include "durable_file.hpp"
#include "save_error.hpp"

#include <expected>
#include <filesystem>
#include <ostream>
#include <string_view>

namespace redis::os {

/**
 * \brief Replace a file as a whole, so a crash leaves the old content or the
 * new, never half of either (the counterpart of Qt's QSaveFile).
 *
 * The new content goes to a fresh temporary file beside the target: created
 * private, never through a link, whatever a crash or someone else left under
 * that name. commit() makes it durable (fsync), renames it over the target and
 * syncs the directory, so the rename survives a power cut too. A step that
 * fails is a SaveError naming the step and the target. A SaveFile dropped without a
 * successful commit() removes its temporary file and leaves the target as it
 * was.
 *
 * For a serializer that wants a std::ostream (cereal), stream() writes through
 * a buffer straight into the file; write() is the same thing for a string.
 * Nothing is visible at the target before commit().
 */
class SaveFile {
public:
	/// Begin replacing \p target, writing to \p temp, which must be in the same
	/// directory (a rename cannot cross filesystems).
	[[nodiscard]] static std::expected<SaveFile, SaveError>
	open(const std::filesystem::path &target,
		 const std::filesystem::path &temp);

	SaveFile(SaveFile &&) noexcept;
	SaveFile &operator=(SaveFile &&) = delete;
	~SaveFile();

	/// For code that writes to a stream. After a failed write the stream is
	/// bad; commit() then reports it.
	[[nodiscard]] std::ostream &stream() noexcept;

	[[nodiscard]] std::expected<void, SaveError> write(std::string_view data);

	/// Make the new content the target's. Do not use the SaveFile afterwards.
	[[nodiscard]] std::expected<void, SaveError> commit();

private:
	struct Impl;
	SaveFile(DurableFile file, std::filesystem::path target,
			 std::filesystem::path temp);

	/// The state, on the heap: the stream refers to the buffer and the buffer to
	/// the file, so it must not move when the SaveFile does.
	indirect<Impl> impl_;
};

} // namespace redis::os
