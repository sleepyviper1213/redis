#pragma once

#define TRY(expression)                                                        \
	({                                                                         \
		auto &&_res = (expression);                                            \
		if (!_res.has_value()) [[unlikely]]                                    \
			return std::unexpected(                                            \
				std::forward<decltype(_res)>(_res).error());                   \
		std::forward<decltype(_res)>(_res).value();                            \
	})

#define TRY_VALUE(expression)                                                  \
	({                                                                         \
		auto &&_res = (expression);                                            \
		if (!_res.has_value()) [[unlikely]] {                                  \
			logger_->trace("Error: {:?}", _res.error());                       \
			return Value::from_simple_error(fmt::format("{}", _res.error()));  \
		}                                                                      \
		std::forward<decltype(_res)>(_res).value();                            \
	})

#define TRY_FROM(expr, E2)                                                      \
	({                                                                          \
		auto &&_res = (expr);                                                   \
		using _T    = typename std::remove_cvref_t<decltype(_res)>::value_type; \
		using _E1   = typename std::remove_cvref_t<decltype(_res)>::error_type; \
		if (!_res.has_value()) [[unlikely]] {                                   \
			if constexpr (std::convertible_to<_E1, E2>)                         \
				return std::unexpected<E2>(E2(std::move(_res).error()));        \
			else if constexpr (FromConvertible<_E1, E2>)                        \
				return std::unexpected<E2>(                                     \
					From<_E1, E2>::convert(std::move(_res).error()));           \
		}                                                                       \
		std::move(_res).value();                                                \
	})

#define UNWRAP(opt_expr)                                                       \
	({                                                                         \
		auto &&_opt = (opt_expr);                                              \
		if (!_opt.has_value()) return std::nullopt;                            \
		std::forward<decltype(_opt)>(_opt).value();                            \
	})
