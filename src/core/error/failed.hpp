#pragma once

// Not called FAILED: the Windows SDK defines a FAILED(hr) macro.
// Variadic so that a braced initialiser with commas ({.kind = ..., .path =
// ...}) passes through as one error.
#define REDIS_FAILED(...)                                                      \
	return std::unexpected { __VA_ARGS__ }
