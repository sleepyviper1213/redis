#include "app_error.hpp"

#include <fmt/format.h>

// auto fmt::formatter<AppError>::format(AppError c, format_context &ctx) const
//-> format_context::iterator {
// string_view name = "unknown";
// switch (c) {
// using enum AppError;
// case PARSE_ERROR: name = "PARSE_ERROR"; break;
// case UNKNOWN: name = "UNKNOWN"; break;
// case EXIT: name = "EXIT"; break;
// case SAVE_INVALID_ARGUMENTS: name = "SAVE_INVALID_ARGUMENTS"; break;
// case SNAPSHOT_CREATION_FAILED: name = "SNAPSHOT_CREATION_FAILED"; break;
// case SNAPSHOT_RESTORATION_FAILED:
// name = "SNAPSHOT_RESTORATION_FAILED";
// break;
// default: std::unreachable();
// }
// return formatter<string_view>::format(name, ctx);
//}
