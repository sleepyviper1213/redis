#pragma once

// Variadic so that a braced initialiser with commas ({.kind = ..., .path = ...})
// passes through as one error.
#define FAILED(...) return std::unexpected { __VA_ARGS__ }
