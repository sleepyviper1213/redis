
#pragma once

#define TRY(expression)                                                        \
	({                                                                         \
		auto &&_temporary_result = (expression);                               \
		if (!_temporary_result.has_value()) [[unlikely]]                       \
			return std::unexpected(std::move(_temporary_result.error()));      \
		std::move(_temporary_result.value());                                  \
	})
