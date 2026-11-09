#pragma once
#include "core/error/failure_format.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace redis::os {

/// The step of replacing a file as a whole that failed. See SaveFile.
enum class SaveKind : std::uint8_t {
	Create,        ///< the temporary file could not be created
	Write,         ///< the content could not be written
	Sync,          ///< the temporary file could not be made durable
	Rename,        ///< the temporary file could not replace the target
	SyncDirectory, ///< the rename could not be made durable
};

/// [path = the file being replaced, cause (empty when only "failed")]
using SaveError = Failure<SaveKind>;

/// The step as a sentence names it: "create", "fsync", ...
[[nodiscard]] std::string_view step_name(SaveKind kind) noexcept;

/// "cannot write <path>: <step> failed: <reason>"
[[nodiscard]] std::string display(const SaveError &error);

} // namespace redis::os
