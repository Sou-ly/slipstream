#pragma once

#include "endian.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace slipstream {

/// Fixed-point divisor: every price field is scaled by this.
inline constexpr std::int64_t price_scale = 10'000;

/// Bumped whenever any message's wire layout changes incompatibly.
inline constexpr std::uint8_t current_version = 1;

enum class MessageType : std::uint8_t {
	Quote			= 1,
	Trade 			= 2,
	Heartbeat		= 3,
	SessionControl	= 4,
    NewOrder        = 10,
    ExecReport      = 11,
};

inline constexpr std::array<MessageType, 6> all_message_types {
	MessageType::Quote,			MessageType::Trade,		MessageType::Heartbeat,
	MessageType::SessionControl, MessageType::NewOrder,	MessageType::ExecReport,
};

/// One bit per valid enumerator, derived from all_message_types so it cannot drift.
inline constexpr std::uint64_t message_type_mask = [] {
	std::uint64_t mask = 0;
	for (auto type : all_message_types) mask |= std::uint64_t{1} << static_cast<std::uint8_t>(type);
	return mask;
}();

/// Validates the raw byte before it is allowed into the type system.
constexpr bool is_valid_message_type(std::uint8_t value) noexcept {
	return value < 64 && ((message_type_mask >> value) & 1u);
}

/// Layout every wire type needs: byte-aligned, padding-free, memcpy-able.
template<class T>
concept WireLayout =
    std::is_trivially_copyable_v<T> &&
    std::is_standard_layout_v<T> &&
    alignof(T) == 1;

/// A payload, which also carries the type tag encode stamps into the header.
template<class T>
concept WireMessage = WireLayout<T> &&
    std::same_as<decltype(T::type), const MessageType>;

struct WireHeader {
	le_u16			body_len;	// payload bytes following this header
	MessageType		msg_type;
	std::uint8_t	version;
};
static_assert(WireLayout<WireHeader>);
static_assert(sizeof(WireHeader) == 4);

/// A complete frame. Composition, not inheritance, which would lose standard layout.
template<WireMessage Message>
struct WireFrame {
	WireHeader	header;
	Message		body;
};

struct QuoteMessage {
    static constexpr auto type = MessageType::Quote;
	char		symbol[12]; // ASCII, null-padded, not null-terminated if full
	le_u64		ts_ns;		// nanoseconds since UNIX epoch
	le_u32		bid_qty;
	le_i64 		bid_px;  	// scaled by price_scale
	le_u32 		ask_qty;
	le_i64 		ask_px;  	// scaled by price_scale
};
static_assert(WireMessage<QuoteMessage>);
static_assert(sizeof(QuoteMessage) == 44);

struct TradeMessage {
    static constexpr auto type = MessageType::Trade;
    enum class AggressorType : char {
    	Buy		= 'B',
    	Sell	= 'S',
    	Unknown = '?',
    };
	char				symbol[12]; // ASCII, null-padded, not null-terminated if full
	le_u64				ts_ns;		// nanoseconds since UNIX epoch
	le_u32				qty;
	le_i64				px;			// scaled by price_scale
	AggressorType		aggressor;
	le_u64				id;			// trade identifier
};
static_assert(WireMessage<TradeMessage>);
static_assert(sizeof(TradeMessage) == 41);

struct HeartbeatMessage {
    static constexpr auto type = MessageType::Heartbeat;
	le_u64 ts_ns;
};
static_assert(WireMessage<HeartbeatMessage>);
static_assert(sizeof(HeartbeatMessage) == 8);

struct SessionControlMessage {
    static constexpr auto type = MessageType::SessionControl;
    enum class SessionState : std::uint8_t {
    	Open = 0,
    	Halt = 1,
    	Close = 2
    };
	le_u64          ts_ns;
	SessionState    state;
};
static_assert(WireMessage<SessionControlMessage>);
static_assert(sizeof(SessionControlMessage) == 9);

struct NewOrderMessage {
    static constexpr auto type = MessageType::NewOrder;
    enum class Status   : char { Accepted = 'A', Refused = 'R' };
    enum class Side     : char { Buy = 'B', Sell = 'S' };
    le_u64      client_order_id;
    char        symbol[12];
    Status      status;
    le_u64      ts_ns;
    le_u64      trade_id;
    Side        side;
    le_u32      qty;
    le_i64      limit;      // scaled by price_scale
};
static_assert(WireMessage<NewOrderMessage>);
static_assert(sizeof(NewOrderMessage) == 50);

struct ExecReportMessage {
    static constexpr auto type = MessageType::ExecReport;
    enum class Status : std::uint8_t {
        Ack     = 1,
        Fill    = 2,
        Partial = 3,
        Reject  = 4
    };
    // None is 0 so a zeroed field means "no error" rather than a non-enumerator.
    enum class ErrorCode : std::uint8_t {
        None        = 0,
        Risk        = 1,
        Price       = 2,
        Size        = 3,
        Throttle    = 4
    };
    le_u64      client_order_id;
    le_u64      ts_ns;
    Status      status;
    le_u32      filled_qty;
    le_i64      avg_px;     // scaled by price_scale
    ErrorCode   reason_code;
};
static_assert(WireMessage<ExecReportMessage>);
static_assert(sizeof(ExecReportMessage) == 30);

/// Largest payload, so a decoder can reject an absurd body_len up front.
inline constexpr std::size_t max_body_len = std::max({
	sizeof(QuoteMessage),			sizeof(TradeMessage),		sizeof(HeartbeatMessage),
	sizeof(SessionControlMessage),	sizeof(NewOrderMessage),	sizeof(ExecReportMessage),
});

// Only these catch padding between header and payload.
static_assert(WireLayout<WireFrame<QuoteMessage>>);
static_assert(sizeof(WireFrame<QuoteMessage>)			== sizeof(WireHeader) + sizeof(QuoteMessage));
static_assert(sizeof(WireFrame<TradeMessage>)			== sizeof(WireHeader) + sizeof(TradeMessage));
static_assert(sizeof(WireFrame<HeartbeatMessage>)		== sizeof(WireHeader) + sizeof(HeartbeatMessage));
static_assert(sizeof(WireFrame<SessionControlMessage>)	== sizeof(WireHeader) + sizeof(SessionControlMessage));
static_assert(sizeof(WireFrame<NewOrderMessage>)		== sizeof(WireHeader) + sizeof(NewOrderMessage));
static_assert(sizeof(WireFrame<ExecReportMessage>)		== sizeof(WireHeader) + sizeof(ExecReportMessage));

} // namespace slipstream
