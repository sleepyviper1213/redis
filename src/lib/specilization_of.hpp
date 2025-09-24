#pragma once
template <typename T, template <typename...> typename U>
inline constexpr bool is_specialization_of = false;
template <template <typename...> typename U, typename... Us>
inline constexpr bool is_specialization_of<U<Us...>, U> = true;

template <typename T, template <typename...> typename S>
concept specialization_of = is_specialization_of<T, S>;
