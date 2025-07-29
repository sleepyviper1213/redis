#pragma once
#include "error/error_or.hpp"
#include "primitive/reply.hpp"

#include <fmt/base.h>

#include <string>

/**
 * @brief Formats an `ErrorOr<Reply>` result for display or logging.
 *
 * If the `ErrorOr` contains a valid result of type `Reply`, the result is
 * converted to a formatted string. If it contains an error, the error is
 * formatted instead.
 *
 * @param x The `ErrorOr<Reply>` instance to format.
 * @return A formatted string representation of the contained value or error.
 *
 * @see ErrorOr, Reply
 */
std::string format_as(const ErrorOr<redis::Reply> &x);
