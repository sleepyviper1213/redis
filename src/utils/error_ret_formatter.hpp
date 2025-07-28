#pragma once
#include "error/error_or.hpp"
#include "primitive/ret.hpp"

#include <fmt/base.h>

#include <string>

/**
 * @brief Formats an `ErrorOr<Ret>` result for display or logging.
 *
 * If the `ErrorOr` contains a valid result of type `Ret`, the result is
 * converted to a formatted string. If it contains an error, the error is
 * formatted instead.
 *
 * @param x The `ErrorOr<Ret>` instance to format.
 * @return A formatted string representation of the contained value or error.
 *
 * @see ErrorOr, Ret
 */
std::string format_as(const ErrorOr<Ret> &x);
std::string format_as(const ErrorOr<Ret> &x);
