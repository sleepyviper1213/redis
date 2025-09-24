#pragma once
#include <boost/asio/basic_socket.hpp>
#include <boost/asio/ip/basic_resolver.hpp>

#include <concepts>
#include <type_traits>

namespace redis::requirement {
template <typename T>
concept Protocol = requires(T t) { typename T::endpoint; };

template <typename T>
concept Endpoint =
	requires(T t) {
		requires Protocol<typename T::protocol_type>;
		{ t.protocol() } -> std::same_as<typename T::protocol_type>;
	} && std::destructible<T> && std::is_default_constructible_v<T> &&
	std::copy_constructible<T> && std::is_copy_assignable_v<T>;

template <typename T>
concept Socket = std::destructible<T> && std::move_constructible<T> &&
				 std::derived_from<T, boost::asio::basic_socket<T>>;
template <typename T>
concept AcceptableProtocol =
	requires { Socket<typename T::socket>; } && Protocol<T>;

template <typename T>
concept InternetProtocol = requires {
	std::same_as<typename T::resolver, boost::asio::ip::basic_resolver<T>>;
	{ T::v4() } -> std::same_as<T>;
	{ T::v6() } -> std::same_as<T>;
} && std::equality_comparable<T> && AcceptableProtocol<T>;
} // namespace redis::requirement
