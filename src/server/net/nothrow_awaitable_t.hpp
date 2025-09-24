#pragma once
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/default_completion_token.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace redis {

/**
 * Actually, the error handling style returing a tuple of (ActualValue, Err) is
 * too boilerplate. Just prefer the ErrorOr error handling concept in general,
 * or C++23 std::expected for monadic functions or Boost Result/Outcome for TRY
 * macro and coroutine integration in particular. Although it's totally a mess.
 * Or implement my own TRY macro to mimic Rust's Result error propagation.
 * Otherwise, check out C++26's P0963 Structured binding declaration as a
 * condition
 */
using nothrow_awaitable_t =
	boost::asio::as_tuple_t<boost::asio::use_awaitable_t<>>;
} // namespace redis

template <>
struct boost::asio::default_completion_token<boost::asio::any_io_executor> {
	using type = redis::nothrow_awaitable_t;
};
