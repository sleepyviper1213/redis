#pragma once

/**
 * @brief Utility type to combine multiple lambda overloads into a single
 * visitor.
 *
 * This is typically used with `std::visit` for `std::variant` to allow multiple
 * callable overloads (like `std::visit(overloaded{...}, variant)`).
 *
 * @tparam Ts Variadic list of lambda types to inherit from.
 *
 * @note As of C++20, class template argument deduction (CTAD) can infer this,
 * so the deduction guide is not strictly needed.
 *
 * @see https://en.cppreference.com/w/cpp/utility/variant/visit2
 */
template <class... Ts>
struct overloaded : Ts... {
	using Ts::operator()...;
};
