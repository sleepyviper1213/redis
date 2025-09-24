/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "assertions.hpp"
#include "empty.hpp"
#include "macro.hpp"
#include "specilization_of.hpp"

#include <concepts>
#include <errno.h>
#include <string_view>
#include <type_traits>

namespace AK {

class [[nodiscard]] Error {
public:
	constexpr Error(Error &&)            = default;
	constexpr Error &operator=(Error &&) = default;

	constexpr static Error from_errno(int code) {
		VERIFY(code != 0);
		return Error(code);
	}

	// NOTE: For calling this method from within kernel code, we will simply
	// print the error message and return the errno code. For calling this
	// method from userspace programs, we will simply return from the
	// Error::from_string_view method!
	static Error from_string_view_or_print_error_and_return_errno(
		std::string_view string_literal, int code);

#ifndef KERNEL
	constexpr static Error from_syscall(std::string_view syscall_name, int rc) {
		return Error(syscall_name, rc);
	}

	constexpr static Error from_string_view(std::string_view string_literal) {
		return Error(string_literal);
	}

	// `Error::from_string_view(ByteString::formatted(...))` is a somewhat
	// common mistake, which leads to a UAF situation. If your string outlives
	// this error and _isn't_ a temporary being passed to this function,
	// explicitly call .view() on it to resolve to the std::string_view
	// overload.
	template <std::same_as<std::string> T>
	constexpr static Error from_string_view(T) = delete;

#endif

	constexpr static Error copy(Error const &error) { return Error(error); }

#ifndef KERNEL
	// NOTE: Prefer `from_string_literal` when directly typing out an error
	// message:
	//
	//     return Error::from_string_literal("Class: Some failure");
	//
	// If you need to return a static string based on a dynamic condition (like
	// picking an error from an array), then prefer `from_string_view` instead.
	template <size_t N>
	constexpr static Error
	from_string_literal(const char (&string_literal)[N]) {
		return from_string_view(std::string_view{string_literal, N - 1});
	}

#endif

	constexpr bool operator==(Error const &other) const {
#ifdef KERNEL
		return m_code == other.m_code;
#else
		return m_code == other.m_code &&
			   m_string_literal == other.m_string_literal &&
			   m_syscall == other.m_syscall;
#endif
	}

	constexpr int code() const { return m_code; }

	constexpr bool is_errno() const { return m_code != 0; }
#ifndef KERNEL
	constexpr bool is_syscall() const { return m_syscall; }

	constexpr std::string_view string_literal() const {
		return m_string_literal;
	}
#endif

protected:
	constexpr explicit Error(int code) : m_code(code) {}

private:
#ifndef KERNEL
	constexpr Error(std::string_view string_literal)
		: m_string_literal(string_literal) {}

	constexpr Error(std::string_view syscall_name, int rc)
		: m_string_literal(syscall_name), m_code(-rc), m_syscall(true) {}
#endif

	constexpr Error(Error const &)            = default;
	constexpr Error &operator=(const Error &) = default;

#ifndef KERNEL
	std::string_view m_string_literal;
#endif

	int m_code{0};

#ifndef KERNEL
	bool m_syscall{false};
#endif
};
} // namespace AK
