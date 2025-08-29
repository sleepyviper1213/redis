#pragma once

#define BAIL(...)                                                              \
	return std::unexpected { std::in_place, __VA_ARGS__ }

#define FAILED(error)                                                          \
	return std::unexpected { error }

// Check optional and unwrap or return error
#define OK_OR(opt, ...)                                                        \
	({                                                                         \
		if (!(opt).has_value()) {                                              \
			return std::unexpected{std::in_place, __VA_ARGS__};                \
		}                                                                      \
		*(opt);                                                                \
	})
