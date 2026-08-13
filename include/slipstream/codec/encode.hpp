#pragma once

#include "message.hpp"
#include <slipstream/error/error.hpp>

#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>

namespace slipstream {

enum class EncodeError : std::uint8_t {
	BufferSize = 0,
};

// No default: -Werror=switch catches an enumerator added without a message.
constexpr std::string_view describe(EncodeError error) noexcept {
	switch (error) {
		case EncodeError::BufferSize:	return "buffer is too small";
	}
	return "unknown encode error";
}
static_assert(DescribableError<EncodeError>);

/// Writes one payload into `out` as a frame with a header derived from Message::type.
template<WireMessage Message>
std::expected<std::size_t, EncodeError> encode(const Message& body, std::span<std::byte> out) noexcept {
	static_assert(sizeof(Message) <= max_body_len);
	static_assert(sizeof(Message) <= UINT16_MAX);

	constexpr std::size_t frame_size = sizeof(WireFrame<Message>);
	if (out.size() < frame_size) {
		return std::unexpected(EncodeError::BufferSize);
	}

	WireFrame<Message> frame{};
	frame.header.body_len = static_cast<std::uint16_t>(sizeof(Message));
	frame.header.msg_type = Message::type;
	frame.header.version  = current_version;
	frame.body            = body;

	std::memcpy(out.data(), &frame, frame_size);
	return frame_size;
}

} // namespace slipstream
