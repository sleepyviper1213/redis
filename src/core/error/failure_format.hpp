#pragma once

#include "display_debug.hpp"
#include "failure.hpp"

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redis {

/// The Debug form of a Failure: the kind, then only the fields that are set,
/// like a struct whose other fields are at their defaults. One line:
///
///   PersistKind::CannotOpen { cause: generic:13 "Permission denied", path: "x"
///   }
///
/// and with \p pretty one field to a line, indented.
template <typename Kind>
std::string debug_repr(const Failure<Kind> &error, bool pretty) {
	std::string name = fmt::format("{}::", magic_enum::enum_type_name<Kind>());
	if (const auto enumerator = magic_enum::enum_name(error.kind);
		!enumerator.empty())
		name += enumerator;
	else name += fmt::format("{}", static_cast<std::size_t>(error.kind));

	std::vector<DebugField> fields;
	if (error.cause)
		fields.emplace_back("cause",
							fmt::format("{}:{} {:?}",
										error.cause.category().name(),
										error.cause.value(),
										error.cause.message()));
	if (!error.path.empty())
		fields.emplace_back("path", fmt::format("{:?}", error.path.string()));
	if (error.at != 0) fields.emplace_back("at", fmt::format("{}", error.at));
	if (error.code != 0)
		fields.emplace_back("code", fmt::format("{}", error.code));
	if (!error.quoted.empty())
		fields.emplace_back("quoted", fmt::format("{:?}", error.quoted));

	return debug_struct(name, fields, pretty);
}

} // namespace redis

/// Every Failure formats as Display or Debug. A module supplies the Display
/// wording as `std::string display(const Failure<ItsKind> &)`.
template <typename Kind>
struct fmt::formatter<redis::Failure<Kind>>
	: redis::DisplayDebugFormatter<redis::Failure<Kind>> {};
