#include "server_error.hpp"

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>

#include <type_traits>
#include <variant>
#include <vector>

namespace redis {
namespace {
/// The source's own sentence, whichever kind of error it is.
std::string describe(const decltype(ServerError::source) &source) {
	return std::visit(
		[]<typename T>(const T &error) -> std::string {
			if constexpr (std::is_same_v<std::decay_t<T>, std::monostate>)
				return {};
			else return fmt::format("{}", error);
		},
		source);
}
} // namespace

std::string display(const ServerError &error) {
	switch (error.kind) {
	case ServerKind::LoadFailed:
	case ServerKind::ListenFailed: return describe(error.source);
	case ServerKind::WorkerFailed: return error.detail;
	case ServerKind::FinalSaveFailed:
		return fmt::format("final snapshot failed: {}", describe(error.source));
	}
	return "server error";
}

std::string debug_repr(const ServerError &error, bool pretty) {
	std::vector<DebugField> fields;
	std::visit(
		[&]<typename T>(const T &source) {
			if constexpr (!std::is_same_v<std::decay_t<T>, std::monostate>)
				fields.emplace_back("source",
									pretty ? fmt::format("{:#?}", source)
										   : fmt::format("{:?}", source));
		},
		error.source);
	if (!error.detail.empty())
		fields.emplace_back("detail", fmt::format("{:?}", error.detail));

	const auto kind = magic_enum::enum_name(error.kind);
	return debug_struct(fmt::format("ServerKind::{}", kind), fields, pretty);
}

} // namespace redis
