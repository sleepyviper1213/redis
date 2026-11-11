#pragma once

#include "core/utils/format_spec.hpp"

#include <fmt/format.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redis {

/// One field of a value's Debug form: its name and its already-formatted value.
using DebugField = std::pair<std::string_view, std::string>;

/// Lay out `Name { a: 1, b: 2 }`, or with \p pretty one field to a line. A field
/// whose value spans several lines (a nested pretty value) is indented under
/// its name. No fields: just the name, as Rust prints a unit variant.
inline std::string debug_struct(std::string_view name,
								const std::vector<DebugField> &fields,
								bool pretty) {
	if (fields.empty()) return std::string(name);

	std::string out(name);
	if (pretty) {
		out += " {\n";
		for (const auto &[field, value] : fields) {
			out += "    ";
			out += field;
			out += ": ";
			for (const char c : value) {
				out += c;
				if (c == '\n') out += "    ";
			}
			out += ",\n";
		}
		out += '}';
	} else {
		out += " { ";
		bool first = true;
		for (const auto &[field, value] : fields) {
			if (!first) out += ", ";
			first = false;
			out += field;
			out += ": ";
			out += value;
		}
		out += " }";
	}
	return out;
}

/**
 * \brief Rust's two ways of formatting a value, for a type that has both:
 *
 *   `{}`     Display: its sentence, for people. Takes the whole string
 *            format specification: `{:>30}`, `{:*^28}`, `{:.10}`.
 *   `{:?}`   Debug: its structure on one line, for developers.
 *   `{:#?}`  Debug over several lines (Rust's alternate flag).
 *
 * The type supplies two functions, found by argument-dependent lookup:
 *
 *   std::string display(const T &);
 *   std::string debug_repr(const T &, bool pretty);
 *
 * and opts in by deriving its fmt::formatter from this:
 *
 *   template <> struct fmt::formatter<redis::MyError>
 *       : redis::DisplayDebugFormatter<redis::MyError> {};
 */
template <typename T>
struct DisplayDebugFormatter : fmt::formatter<std::string_view> {
	enum class Mode : unsigned char { Display, Debug, Pretty };

	Mode mode = Mode::Display;

	constexpr auto parse(fmt::format_parse_context &ctx)
		-> fmt::format_parse_context::iterator {
		const auto *const it = ctx.begin();
		if (it == ctx.end() || (*it != '?' && *it != '#')) {
			// Display: whatever a string accepts.
			return fmt::formatter<std::string_view>::parse(ctx);
		}

		// Debug is `?`, or `#?` for the several-line form, and takes no width or
		// precision: nothing may follow the `?`.
		const bool alternate = *it == '#';
		if (alternate) ctx.advance_to(it + 1);
		if (parse_presentation(ctx, "?") != '?')
			fmt::report_error("invalid format specifier");
		mode = alternate ? Mode::Pretty : Mode::Debug;
		return ctx.begin();
	}

	template <typename FormatContext>
	auto format(const T &value, FormatContext &ctx) const
		-> decltype(ctx.out()) {
		using Base = fmt::formatter<std::string_view>;
		switch (mode) {
		case Mode::Display: return Base::format(display(value), ctx);
		case Mode::Debug: return Base::format(debug_repr(value, false), ctx);
		case Mode::Pretty: return Base::format(debug_repr(value, true), ctx);
		}
		return ctx.out();
	}
};

} // namespace redis
