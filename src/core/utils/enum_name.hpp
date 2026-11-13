#pragma once
// Enum names by reflection (reflect-cpp), as views into compile-time tables:
// nothing allocates, and an unknown value has no name rather than a number.

#include <rfl/enums.hpp>
#include <rfl/type_name_t.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

namespace redis {

namespace detail {
template <typename E>
inline constexpr auto enumerators = rfl::get_enumerator_array<E>();

/// The text of a one-value rfl::Literal, which only offers it at run time.
template <typename L>
struct LiteralText;

template <auto Text>
struct LiteralText<rfl::Literal<Text>> {
	static constexpr auto value = Text;
};

template <typename E>
inline constexpr auto qualified_type_name =
	LiteralText<rfl::type_name_t<E>>::value;
} // namespace detail

/// \return The name of the enumerator \p value, or an empty view if \p value
///         is not one.
template <typename E>
	requires std::is_enum_v<E>
[[nodiscard]] constexpr std::string_view enum_name(E value) noexcept {
	for (const auto &[name, enumerator] : detail::enumerators<E>)
		if (enumerator == value) return name;
	return {};
}

/// \return The enumerator named \p name, if there is one. Numbers are not
///         names.
template <typename E>
	requires std::is_enum_v<E>
[[nodiscard]] constexpr std::optional<E>
enum_cast(std::string_view name) noexcept {
	for (const auto &[candidate, enumerator] : detail::enumerators<E>)
		if (candidate == name) return enumerator;
	return std::nullopt;
}

/// \return The names of the enumerators of \p E, in declaration order.
template <typename E>
	requires std::is_enum_v<E>
[[nodiscard]] constexpr auto enum_names() noexcept {
	constexpr auto &all = detail::enumerators<E>;
	std::array<std::string_view, all.size()> names{};
	for (std::size_t i = 0; i < all.size(); ++i) names[i] = all[i].first;
	return names;
}

/// \return The unqualified name of \p E: "PersistKind" for
///         redis::PersistKind.
template <typename E>
	requires std::is_enum_v<E>
[[nodiscard]] constexpr std::string_view enum_type_name() noexcept {
	constexpr std::string_view name =
		detail::qualified_type_name<E>.string_view();
	constexpr auto colons = name.rfind("::");
	return colons == std::string_view::npos ? name : name.substr(colons + 2);
}

} // namespace redis
