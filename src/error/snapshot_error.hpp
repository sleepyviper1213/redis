#pragma once
#include <fmt/base.h>
enum class SnapshotError { FILE_NOT_FOUND };

template <>
struct fmt::formatter<SnapshotError> : formatter<string_view> {
	format_context::iterator format(SnapshotError t, format_context &ctx) const;
};
