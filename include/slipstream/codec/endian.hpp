#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace slipstream {

/// An integer stored little-endian regardless of host byte order.
template<std::integral T>
class little_endian {
public:
	little_endian() = default;
	little_endian(T value) noexcept { store(value); }

	operator T() const noexcept { return load(); }
	little_endian& operator=(T value) noexcept { store(value); return *this; }

private:
	void store(T value) noexcept {
		if constexpr (std::endian::native == std::endian::big) value = std::byteswap(value);
		std::memcpy(bytes_, &value, sizeof value);
	}

	T load() const noexcept {
		T value;
		std::memcpy(&value, bytes_, sizeof value);
		if constexpr (std::endian::native == std::endian::big) value = std::byteswap(value);
		return value;
	}

	std::byte bytes_[sizeof(T)];
};

using le_u16 = little_endian<std::uint16_t>;	// header length field only
using le_u32 = little_endian<std::uint32_t>;
using le_u64 = little_endian<std::uint64_t>;
using le_i32 = little_endian<std::int32_t>;
using le_i64 = little_endian<std::int64_t>;

// Byte alignment is what keeps the wire structs padding-free.
static_assert(alignof(le_u16) == 1 && sizeof(le_u16) == 2);
static_assert(alignof(le_u32) == 1 && sizeof(le_u32) == 4);
static_assert(alignof(le_u64) == 1 && sizeof(le_u64) == 8);
static_assert(alignof(le_i32) == 1 && sizeof(le_i32) == 4);
static_assert(alignof(le_i64) == 1 && sizeof(le_i64) == 8);
static_assert(std::is_trivially_copyable_v<le_u64>);

} // namespace slipstream
