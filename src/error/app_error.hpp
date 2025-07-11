#pragma once
#include "execute_error.hpp"
#include "parse_error.hpp"
#include "snapshot_error.hpp"

#include <variant>
using AppError = std::variant<ParseError, ExecuteError, SnapshotError>;
