#include "save_file.hpp"

#include "child_process.hpp"

#include <fmt/format.h>
#include <fmt/std.h> // IWYU pragma: keep (std::filesystem::path)

#include <array>
#include <cstddef>
#include <streambuf>
#include <utility>

namespace redis::os {
namespace {
namespace fs = std::filesystem;

/// A stream buffer that writes to a DurableFile, so a serializer made for
/// std::ostream can write into it. Flushing it (std::ostream::flush) hands the
/// buffer to the file; it does not fsync.
class FileBuf final : public std::streambuf {
public:
	explicit FileBuf(DurableFile &file) : file_(file) { reset_buffer(); }

	/// Write what is buffered to the file.
	[[nodiscard]] std::error_code flush_pending() {
		const auto pending = static_cast<std::size_t>(pptr() - pbase());
		if (pending == 0) return {};
		if (const auto err =
				file_.write_all(std::string_view(pbase(), pending))) {
			error_ = err;
			return err;
		}
		reset_buffer();
		return {};
	}

	/// The first write error, if there was one.
	[[nodiscard]] std::error_code error() const noexcept { return error_; }

protected:
	int_type overflow(int_type ch) override {
		if (flush_pending()) return traits_type::eof();
		if (!traits_type::eq_int_type(ch, traits_type::eof())) {
			*pptr() = traits_type::to_char_type(ch);
			pbump(1);
		}
		return traits_type::not_eof(ch);
	}

	int sync() override { return flush_pending() ? -1 : 0; }

private:
	static constexpr std::size_t BUFFER_SIZE = std::size_t{64} * 1024;

	void reset_buffer() {
		setp(buffer_.data(), buffer_.data() + buffer_.size());
	}

	DurableFile
		&file_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
	std::error_code error_;
	std::array<char, BUFFER_SIZE> buffer_{};
};
} // namespace

// The file is declared before the buffer that refers to it, and the buffer
// before the stream that refers to that, so they are built (and destroyed) in
// the order they need.
struct SaveFile::Impl {
	Impl(DurableFile opened, fs::path target_path, fs::path temp_path)
		: file(std::move(opened)),
		  buf(file),
		  stream(&buf),
		  target(std::move(target_path)),
		  temp(std::move(temp_path)) {}

	DurableFile file;
	FileBuf buf;
	std::ostream stream;
	fs::path target;
	fs::path temp;
	bool committed = false;
};

SaveFile::SaveFile(DurableFile file, fs::path target, fs::path temp)
	: impl_(std::in_place, std::move(file), std::move(target), std::move(temp)) {}

SaveFile::SaveFile(SaveFile &&) noexcept = default;

SaveFile::~SaveFile() {
	// Moved from, or committed: nothing to clean up.
	if (impl_.valueless_after_move() || impl_->committed) return;
	impl_->file.close(); // a file that is open cannot be removed everywhere
	std::error_code ignored;
	fs::remove(impl_->temp, ignored);
}

std::expected<SaveFile, SaveError> SaveFile::open(const fs::path &target,
													 const fs::path &temp) {
	// The temporary's name is predictable, so whatever is there (a leftover of
	// a crash, or a link someone put in its place) goes, and the file is
	// created anew: that fails rather than open anything that exists, and never
	// follows a link.
	std::error_code ignored;
	fs::remove(temp, ignored);
	auto created = DurableFile::create_new(temp);
	if (!created)
		return std::unexpected(SaveError{.kind  = SaveKind::Create,
											.cause = created.error(),
											.path  = target});
	return SaveFile(std::move(*created), target, temp);
}

std::ostream &SaveFile::stream() noexcept { return impl_->stream; }

std::expected<void, SaveError> SaveFile::write(std::string_view data) {
	impl_->stream.write(data.data(), static_cast<std::streamsize>(data.size()));
	if (!impl_->stream)
		return std::unexpected(SaveError{.kind  = SaveKind::Write,
											.cause = impl_->buf.error(),
											.path  = impl_->target});
	return {};
}

std::expected<void, SaveError> SaveFile::commit() {
	Impl &self = *impl_;
	// What failed, for the step that did.
	const auto failed = [&self](SaveKind kind, std::error_code why) {
		return std::unexpected(
			SaveError{.kind = kind, .cause = why, .path = self.target});
	};

	self.stream.flush();
	if (!self.stream) return failed(SaveKind::Write, self.buf.error());
	if (const auto err = self.file.sync())
		return failed(SaveKind::Sync, err);
	self.file.close(); // a file that is open cannot be renamed everywhere

	std::error_code why;
	fs::rename(self.temp, self.target, why);
	if (why) return failed(SaveKind::Rename, why);
	// In place from here: the temporary no longer exists to be cleaned up.
	self.committed = true;

	if (why = sync_directory(self.target.parent_path()); why)
		return failed(SaveKind::SyncDirectory, why);
	return {};
}

std::string_view step_name(SaveKind kind) noexcept {
	switch (kind) {
	case SaveKind::Create: return "create";
	case SaveKind::Write: return "write";
	case SaveKind::Sync: return "fsync";
	case SaveKind::Rename: return "rename";
	case SaveKind::SyncDirectory: return "fsync of the directory";
	}
	return "save";
}

std::string display(const SaveError &error) {
	return fmt::format("cannot write {}: {} failed{}",
					   error.path,
					   step_name(error.kind),
					   error.cause ? fmt::format(": {}", error.cause.message())
								   : std::string{});
}

} // namespace redis::os
