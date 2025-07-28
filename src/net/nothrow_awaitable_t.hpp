#pragma once
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>

/**
 * Actually, the error handling style returing a tuple of (ActualValue, Err) is
 * too boilerplate. Just prefer the ErrorOr error handling concept in general,
 * or C++23 std::expected for monadic functions or Boost Result/Outcome for TRY
 * macro and coroutine integration in particular. Although it's totally a mess.
 * Or implement my own TRY macro to mimic Rust's Result error propagation.
 * Otherwise, check out C++26's P0963 Structured binding declaration as a
 * condition
 * */

namespace net = boost::asio;

using nothrow_awaitable_t = net::as_tuple_t<net::use_awaitable_t<>>;
BOOST_ASIO_INLINE_VARIABLE constexpr nothrow_awaitable_t nothrow_awaitable;
