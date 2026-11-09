#pragma once
#include <span>
#include <string_view>

namespace redis {

/// A command's arguments; args[0] is the command name itself. They view the
/// client's input buffer, so a command that keeps one must copy it.
using Args = std::span<const std::string_view>;

} // namespace redis
