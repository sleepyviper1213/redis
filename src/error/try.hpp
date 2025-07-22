#pragma once

#define TRY(expression)                                                        \
	({                                                                         \
		auto &&_res = (expression);                                            \
		if (!_res.has_value()) [[unlikely]]                                    \
			return std::unexpected(                                            \
				std::forward<decltype(_res)>(_res).error());                   \
		std::forward<decltype(_res)>(_res).value();                            \
	})
