#pragma once

#include <concepts>
#include <format>
#include <string_view>
#include <type_traits>

namespace slipstream {

/// An error enum with a describe() overload findable by ADL.
template<class E>
concept DescribableError = std::is_enum_v<E> &&
	requires(E error) { { describe(error) } -> std::convertible_to<std::string_view>; };

} // namespace slipstream

/// Lets std::print and std::format render any error enum with no per-enum code.
template<slipstream::DescribableError E>
struct std::formatter<E> : std::formatter<std::string_view> {
	auto format(E error, auto& ctx) const {
		return std::formatter<std::string_view>::format(describe(error), ctx);
	}
};
