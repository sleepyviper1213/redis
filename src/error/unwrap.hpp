#pragma once
#define UNWRAP(opt_expr)                                                       \
	({                                                                         \
		auto &&_opt = (opt_expr);                                              \
		if (!_opt.has_value()) return std::nullopt;                            \
		std::forward<decltype(_opt)>(_opt).value();                            \
	})
