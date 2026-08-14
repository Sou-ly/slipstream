#include "slipstream/codec/decode.hpp"

namespace slipstream {

std::expected<Decoded, DecodeError> try_decode(std::span<const std::byte> in) noexcept {
	if (in.size() < sizeof(WireHeader)) {
		return std::unexpected(DecodeError::Incomplete);
	}

	WireHeader header{};
	std::memcpy(&header, in.data(), sizeof(WireHeader));

	const std::size_t body_len = header.body_len;
	if (body_len > max_body_len) {
		return std::unexpected(DecodeError::MessageTooLarge);
	}

	if (in.size() - sizeof(WireHeader) < body_len) {
		return std::unexpected(DecodeError::Incomplete);
	}

	if (!is_supported_version(header.version)) {
		return std::unexpected(DecodeError::UnsupportedVersion);
	}

	// Checked as a raw byte, before anything switches on the enum.
	if (!is_valid_message_type(static_cast<std::uint8_t>(header.msg_type))) {
		return std::unexpected(DecodeError::UnknownMessageType);
	}

	const std::span<const std::byte> body = in.subspan(sizeof(WireHeader));

	// No default: -Werror=switch rejects any MessageType added without a case.
	switch (header.msg_type) {
		case MessageType::Quote:			return try_decode_as<QuoteMessage>(header, body);
		case MessageType::Trade:			return try_decode_as<TradeMessage>(header, body);
		case MessageType::Heartbeat:		return try_decode_as<HeartbeatMessage>(header, body);
		case MessageType::SessionControl:	return try_decode_as<SessionControlMessage>(header, body);
		case MessageType::NewOrder:			return try_decode_as<NewOrderMessage>(header, body);
		case MessageType::ExecReport:		return try_decode_as<ExecReportMessage>(header, body);
	}

	// Unreachable, but an error beats std::unreachable() if the mask ever drifts.
	return std::unexpected(DecodeError::UnknownMessageType);
}

} // namespace slipstream
