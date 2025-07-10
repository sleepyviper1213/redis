#pragma once
#include "execute_error.hpp"
#include "parse_error.hpp"
#include "snapshot_error.hpp"

#include <variant>
using AppError = std::variant<ParseError, ExecuteError, SnapshotError>;

// template <>
// struct fmt::formatter<AppError> : formatter<string_view> {
// format_context::iterator format(AppError t, format_context &ctx) const;
//};
