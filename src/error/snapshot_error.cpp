#include "snapshot_error.hpp"

#include <utility>

auto fmt::formatter<SnapshotError>::format(
	SnapshotError c, format_context &ctx) const -> format_context::iterator {
	string_view name = "unknown";
	switch (c) {
		using enum SnapshotError;
	case FILE_NOT_FOUND: name = "FILE_NOT_FOUND"; break;

	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}