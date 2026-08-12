#pragma once

#include "message.hpp"

#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <variant>

namespace slipstream {

enum class DecodeError : std::uint8_t {
	Incomplete			= 0,	// not a failure: call again with more bytes
	UnknownMessageType	= 1,
	UnsupportedVersion	= 2,
	LengthMismatch		= 3,
	MessageTooLarge		= 4,
	InvalidField		= 5,
};

/// True when the stream is desynchronised and the connection must be dropped.
constexpr bool is_fatal(DecodeError error) noexcept {
	return error == DecodeError::LengthMismatch || error == DecodeError::MessageTooLarge;
}

constexpr bool is_supported_version(std::uint8_t version) noexcept {
	return version == current_version;
}

using AnyMessage = std::variant<
	QuoteMessage, TradeMessage, HeartbeatMessage,
	SessionControlMessage, NewOrderMessage, ExecReportMessage>;

struct Decoded {
	WireHeader	header;
	AnyMessage	message;
	std::size_t	consumed;	// frame size, so callers can do in = in.subspan(consumed)
};

namespace detail {

/// Printable ASCII, left-justified, NUL-padded, with nothing after the first NUL.
constexpr bool valid_symbol(const char (&symbol)[12]) noexcept {
	bool padding = false;
	for (char character : symbol) {
		if (character == '\0') {
			padding = true;
		} else if (padding || character < 0x20 || character > 0x7E) {
			return false;
		}
	}
	return true;
}

} // namespace detail

// Wire bytes are hostile: every enum field is checked before anything reads it.

constexpr bool validate(const QuoteMessage& message) noexcept {
	return detail::valid_symbol(message.symbol);
}

constexpr bool validate(const TradeMessage& message) noexcept {
	using Aggressor = TradeMessage::AggressorType;
	return detail::valid_symbol(message.symbol)
		&& (message.aggressor == Aggressor::Buy
		 || message.aggressor == Aggressor::Sell
		 || message.aggressor == Aggressor::Unknown);
}

constexpr bool validate(const HeartbeatMessage&) noexcept {
	return true;
}

constexpr bool validate(const SessionControlMessage& message) noexcept {
	using State = SessionControlMessage::SessionState;
	return message.state == State::Open || message.state == State::Halt || message.state == State::Close;
}

constexpr bool validate(const NewOrderMessage& message) noexcept {
	using Status = NewOrderMessage::Status;
	using Side   = NewOrderMessage::Side;
	return detail::valid_symbol(message.symbol)
		&& (message.status == Status::Accepted || message.status == Status::Refused)
		&& (message.side == Side::Buy || message.side == Side::Sell);
}

constexpr bool validate(const ExecReportMessage& message) noexcept {
	using Status = ExecReportMessage::Status;
	using Error  = ExecReportMessage::ErrorCode;
	const bool status_ok = message.status == Status::Ack || message.status == Status::Fill
						|| message.status == Status::Partial || message.status == Status::Reject;
	const bool reason_ok = message.reason_code == Error::None || message.reason_code == Error::Risk
						|| message.reason_code == Error::Price || message.reason_code == Error::Size
						|| message.reason_code == Error::Throttle;
	return status_ok && reason_ok;
}

/// Decodes one known payload type. `body` may extend past this frame, hence >=.
template<WireMessage Message>
std::expected<Decoded, DecodeError> try_decode_as(const WireHeader& header,
                                                  std::span<const std::byte> body) noexcept {
	if (header.body_len != sizeof(Message)) {
		return std::unexpected(DecodeError::LengthMismatch);
	}
	if (body.size() < sizeof(Message)) {
		return std::unexpected(DecodeError::Incomplete);
	}

	Message message{};
	std::memcpy(&message, body.data(), sizeof(Message));

	if (!validate(message)) {
		return std::unexpected(DecodeError::InvalidField);
	}
	return Decoded{header, AnyMessage{message}, sizeof(WireHeader) + sizeof(Message)};
}

/// Reads at most one frame from the front of `in`, never past `consumed` bytes.
std::expected<Decoded, DecodeError> try_decode(std::span<const std::byte> in) noexcept;

} // namespace slipstream
