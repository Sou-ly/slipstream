// Unit tests for libslipstream_codec. CHECK, not assert: survives NDEBUG, reports every failure.

#include "slipstream/codec/decode.hpp"
#include "slipstream/codec/encode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <print>
#include <span>
#include <string_view>

using namespace slipstream;

namespace {

int checks_run = 0;
int checks_failed = 0;

void report(bool ok, std::string_view expression, std::string_view file, int line) {
	++checks_run;
	if (!ok) {
		++checks_failed;
		std::println("  FAIL  {}:{}  {}", file, line, expression);
	}
}

#define CHECK(expr) report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

// No padding anywhere, so byte equality is exactly value equality.
template<class T>
bool same_bytes(const T& lhs, const T& rhs) {
	return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

void fill_symbol(char (&symbol)[12], std::string_view text) {
	std::memset(symbol, 0, sizeof symbol);
	std::memcpy(symbol, text.data(), text.size());
}

// --- Sample messages --------------------------------------------------------

QuoteMessage sample_quote() {
	QuoteMessage message{};
	fill_symbol(message.symbol, "AAPL");
	message.ts_ns   = 1'700'000'000'000'000'000ull;
	message.bid_qty = 100;
	message.bid_px  = 187'5000;   // grouped <units>'<fraction>
	message.ask_qty = 250;
	message.ask_px  = 187'5500;
	return message;
}

TradeMessage sample_trade() {
	TradeMessage message{};
	fill_symbol(message.symbol, "MSFT");
	message.ts_ns     = 42;
	message.qty       = 7;
	message.px        = -1'0000;   // negative, to exercise signed round-tripping
	message.aggressor = TradeMessage::AggressorType::Sell;
	message.id        = 999;
	return message;
}

HeartbeatMessage sample_heartbeat() {
	return HeartbeatMessage{.ts_ns = 123456};
}

SessionControlMessage sample_session() {
	SessionControlMessage message{};
	message.ts_ns = 7;
	message.state = SessionControlMessage::SessionState::Halt;
	return message;
}

NewOrderMessage sample_new_order() {
	NewOrderMessage message{};
	message.client_order_id = 55;
	fill_symbol(message.symbol, "NVDA");
	message.status   = NewOrderMessage::Status::Accepted;
	message.ts_ns    = 8;
	message.trade_id = 9;
	message.side     = NewOrderMessage::Side::Buy;
	message.qty      = 10;
	message.limit    = 500'0000;
	return message;
}

ExecReportMessage sample_exec_report() {
	ExecReportMessage message{};
	message.client_order_id = 55;
	message.ts_ns           = 11;
	message.status          = ExecReportMessage::Status::Partial;
	message.filled_qty      = 4;
	message.avg_px          = -25'0000;
	message.reason_code     = ExecReportMessage::ErrorCode::None;
	return message;
}

// --- little_endian ----------------------------------------------------------

void test_little_endian_byte_order() {
	le_u32 value = 0x11223344u;
	std::byte raw[sizeof value];
	std::memcpy(raw, &value, sizeof value);

	// Least significant byte first, on any host.
	CHECK(raw[0] == std::byte{0x44});
	CHECK(raw[1] == std::byte{0x33});
	CHECK(raw[2] == std::byte{0x22});
	CHECK(raw[3] == std::byte{0x11});
	CHECK(static_cast<std::uint32_t>(value) == 0x11223344u);
}

void test_little_endian_round_trip() {
	CHECK(static_cast<std::uint64_t>(le_u64{0}) == 0);
	CHECK(static_cast<std::uint64_t>(le_u64{UINT64_MAX}) == UINT64_MAX);
	CHECK(static_cast<std::int64_t>(le_i64{-1}) == -1);
	CHECK(static_cast<std::int64_t>(le_i64{INT64_MIN}) == INT64_MIN);
	CHECK(static_cast<std::int64_t>(le_i64{INT64_MAX}) == INT64_MAX);
	CHECK(static_cast<std::uint16_t>(le_u16{0xBEEF}) == 0xBEEF);
}

void test_little_endian_compares_by_value() {
	// Not by bytes: memcmp would order 256 before 1 on a little-endian host.
	CHECK(le_u64{5} == le_u64{5});
	CHECK(le_u64{1} < le_u64{256});
	CHECK(le_i64{-1} < le_i64{1});
	CHECK(le_u64{5} == 5u);
}

// --- Type table -------------------------------------------------------------

void test_message_type_validity() {
	for (auto type : all_message_types) {
		CHECK(is_valid_message_type(static_cast<std::uint8_t>(type)));
	}
	CHECK(!is_valid_message_type(0));
	CHECK(!is_valid_message_type(5));
	CHECK(!is_valid_message_type(9));
	CHECK(!is_valid_message_type(12));
	CHECK(!is_valid_message_type(255));
}

// --- Encoding ---------------------------------------------------------------

void test_encode_writes_expected_header() {
	std::array<std::byte, 128> buffer{};
	const auto written = encode(sample_quote(), buffer);

	CHECK(written.has_value());
	CHECK(*written == sizeof(WireHeader) + sizeof(QuoteMessage));

	CHECK(buffer[0] == std::byte{44});   // body_len, low byte
	CHECK(buffer[1] == std::byte{0});    // body_len, high byte
	CHECK(buffer[2] == std::byte{static_cast<std::uint8_t>(MessageType::Quote)});
	CHECK(buffer[3] == std::byte{current_version});

	// Payload begins immediately after the header, no padding.
	CHECK(buffer[4] == std::byte{'A'});
	CHECK(buffer[5] == std::byte{'A'});
	CHECK(buffer[6] == std::byte{'P'});
	CHECK(buffer[7] == std::byte{'L'});
	CHECK(buffer[8] == std::byte{0});
}

void test_encode_rejects_small_buffer() {
	constexpr std::size_t needed = sizeof(WireHeader) + sizeof(QuoteMessage);
	std::array<std::byte, needed> exact{};
	std::array<std::byte, needed - 1> one_short{};

	CHECK(encode(sample_quote(), exact).has_value());

	const auto failed = encode(sample_quote(), one_short);
	CHECK(!failed.has_value());
	CHECK(failed.error() == EncodeError::BufferSize);
}

void test_encode_does_not_overrun() {
	constexpr std::size_t needed = sizeof(WireHeader) + sizeof(QuoteMessage);
	std::array<std::byte, needed + 16> buffer{};
	buffer.fill(std::byte{0xAA});

	const auto written = encode(sample_quote(), buffer);
	CHECK(written.has_value());

	bool canary_intact = true;
	for (std::size_t i = needed; i < buffer.size(); ++i) {
		if (buffer[i] != std::byte{0xAA}) canary_intact = false;
	}
	CHECK(canary_intact);
}

// --- Round trips ------------------------------------------------------------

template<WireMessage Message>
void round_trip(const Message& original) {
	std::array<std::byte, 256> buffer{};
	const auto written = encode(original, buffer);
	CHECK(written.has_value());
	if (!written) return;

	const auto decoded = try_decode(std::span<const std::byte>{buffer}.first(*written));
	CHECK(decoded.has_value());
	if (!decoded) return;

	CHECK(decoded->consumed == *written);
	CHECK(decoded->header.msg_type == Message::type);
	CHECK(decoded->header.body_len == sizeof(Message));
	CHECK(decoded->header.version == current_version);
	CHECK(std::holds_alternative<Message>(decoded->message));
	if (std::holds_alternative<Message>(decoded->message)) {
		CHECK(same_bytes(std::get<Message>(decoded->message), original));
	}
}

void test_round_trip_all_types() {
	round_trip(sample_quote());
	round_trip(sample_trade());
	round_trip(sample_heartbeat());
	round_trip(sample_session());
	round_trip(sample_new_order());
	round_trip(sample_exec_report());
}

// --- Framing errors ---------------------------------------------------------

void test_decode_incomplete() {
	std::array<std::byte, 256> buffer{};
	const auto written = *encode(sample_quote(), buffer);
	const std::span<const std::byte> frame{buffer};

	// Nothing at all.
	{
		const auto result = try_decode(std::span<const std::byte>{});
		CHECK(!result.has_value() && result.error() == DecodeError::Incomplete);
	}
	// Part of a header.
	for (std::size_t length = 1; length < sizeof(WireHeader); ++length) {
		const auto result = try_decode(frame.first(length));
		CHECK(!result.has_value() && result.error() == DecodeError::Incomplete);
	}
	// Full header, truncated body, including one byte short of complete.
	for (std::size_t length = sizeof(WireHeader); length < written; ++length) {
		const auto result = try_decode(frame.first(length));
		CHECK(!result.has_value() && result.error() == DecodeError::Incomplete);
	}
	// Exactly complete.
	CHECK(try_decode(frame.first(written)).has_value());
}

void test_decode_unknown_type() {
	std::array<std::byte, 256> buffer{};
	const auto written = *encode(sample_quote(), buffer);
	buffer[2] = std::byte{99};   // msg_type

	const auto result = try_decode(std::span<const std::byte>{buffer}.first(written));
	CHECK(!result.has_value());
	CHECK(result.error() == DecodeError::UnknownMessageType);
	CHECK(!is_fatal(DecodeError::UnknownMessageType));   // recoverable: skip and resync
}

void test_decode_bad_version() {
	std::array<std::byte, 256> buffer{};
	const auto written = *encode(sample_quote(), buffer);
	buffer[3] = std::byte{current_version + 1};

	const auto result = try_decode(std::span<const std::byte>{buffer}.first(written));
	CHECK(!result.has_value());
	CHECK(result.error() == DecodeError::UnsupportedVersion);
}

void test_decode_length_mismatch() {
	std::array<std::byte, 256> buffer{};
	const auto written = *encode(sample_quote(), buffer);
	buffer[0] = std::byte{43};   // body_len one short of sizeof(QuoteMessage)

	const auto result = try_decode(std::span<const std::byte>{buffer}.first(written));
	CHECK(!result.has_value());
	CHECK(result.error() == DecodeError::LengthMismatch);
	CHECK(is_fatal(DecodeError::LengthMismatch));   // stream is desynchronised
}

void test_decode_absurd_length_does_not_wait() {
	// A corrupt length must be rejected outright, not awaited. Only 4 bytes supplied.
	std::array<std::byte, sizeof(WireHeader)> header_only{};
	header_only[0] = std::byte{0xFF};
	header_only[1] = std::byte{0xFF};   // body_len = 65535
	header_only[2] = std::byte{static_cast<std::uint8_t>(MessageType::Quote)};
	header_only[3] = std::byte{current_version};

	const auto result = try_decode(header_only);
	CHECK(!result.has_value());
	CHECK(result.error() == DecodeError::MessageTooLarge);
}

// --- Field validation -------------------------------------------------------

template<WireMessage Message>
void expect_invalid_after(const Message& original, std::size_t body_offset, std::byte corrupt) {
	std::array<std::byte, 256> buffer{};
	const auto written = *encode(original, buffer);
	buffer[sizeof(WireHeader) + body_offset] = corrupt;

	const auto result = try_decode(std::span<const std::byte>{buffer}.first(written));
	CHECK(!result.has_value());
	if (!result) CHECK(result.error() == DecodeError::InvalidField);
}

void test_decode_rejects_invalid_fields() {
	expect_invalid_after(sample_trade(), offsetof(TradeMessage, aggressor), std::byte{'X'});
	expect_invalid_after(sample_session(), offsetof(SessionControlMessage, state), std::byte{7});
	expect_invalid_after(sample_new_order(), offsetof(NewOrderMessage, side), std::byte{'Z'});
	expect_invalid_after(sample_new_order(), offsetof(NewOrderMessage, status), std::byte{'Q'});
	expect_invalid_after(sample_exec_report(), offsetof(ExecReportMessage, status), std::byte{9});
	expect_invalid_after(sample_exec_report(), offsetof(ExecReportMessage, reason_code), std::byte{200});

	// Non-printable byte inside the symbol.
	expect_invalid_after(sample_quote(), offsetof(QuoteMessage, symbol), std::byte{0x01});
	// Text after the NUL padding has started.
	expect_invalid_after(sample_quote(), offsetof(QuoteMessage, symbol) + 11, std::byte{'X'});
}

void test_zeroed_buffer_is_not_a_valid_frame() {
	// Version is checked before type, so a zeroed header trips UnsupportedVersion.
	std::array<std::byte, 64> zeros{};
	const auto result = try_decode(zeros);
	CHECK(!result.has_value());
	CHECK(result.error() == DecodeError::UnsupportedVersion);

	// With a valid version, the zero type byte is what rejects it.
	zeros[3] = std::byte{current_version};
	const auto typed = try_decode(zeros);
	CHECK(!typed.has_value());
	CHECK(typed.error() == DecodeError::UnknownMessageType);
}

// --- Streaming --------------------------------------------------------------

void test_decode_consumes_one_frame_at_a_time() {
	std::array<std::byte, 512> buffer{};
	std::span<std::byte> cursor{buffer};

	const auto first  = *encode(sample_quote(), cursor);
	cursor = cursor.subspan(first);
	const auto second = *encode(sample_heartbeat(), cursor);
	cursor = cursor.subspan(second);
	const auto third  = *encode(sample_trade(), cursor);

	const std::size_t total = first + second + third;
	std::span<const std::byte> in = std::span<const std::byte>{buffer}.first(total);

	const auto quote = try_decode(in);
	CHECK(quote.has_value());
	CHECK(quote && std::holds_alternative<QuoteMessage>(quote->message));
	CHECK(quote && quote->consumed == first);
	if (quote) in = in.subspan(quote->consumed);

	const auto heartbeat = try_decode(in);
	CHECK(heartbeat.has_value());
	CHECK(heartbeat && std::holds_alternative<HeartbeatMessage>(heartbeat->message));
	if (heartbeat) in = in.subspan(heartbeat->consumed);

	const auto trade = try_decode(in);
	CHECK(trade.has_value());
	CHECK(trade && std::holds_alternative<TradeMessage>(trade->message));
	if (trade) in = in.subspan(trade->consumed);

	CHECK(in.empty());
}

void test_partial_second_frame_reports_incomplete() {
	std::array<std::byte, 512> buffer{};
	const auto first = *encode(sample_quote(), buffer);
	const auto second = *encode(sample_trade(), std::span<std::byte>{buffer}.subspan(first));

	// Everything except the last byte of the second frame.
	std::span<const std::byte> in = std::span<const std::byte>{buffer}.first(first + second - 1);

	const auto quote = try_decode(in);
	CHECK(quote.has_value());
	if (quote) in = in.subspan(quote->consumed);

	const auto partial = try_decode(in);
	CHECK(!partial.has_value());
	CHECK(partial.error() == DecodeError::Incomplete);
}

// --- Fixed point ------------------------------------------------------------

void test_price_scaling_round_trips() {
	QuoteMessage message = sample_quote();
	CHECK(static_cast<std::int64_t>(message.bid_px) == 187'5000);
	CHECK(static_cast<std::int64_t>(message.bid_px) / price_scale == 187);
	CHECK(static_cast<std::int64_t>(message.bid_px) % price_scale == 5000);

	TradeMessage negative = sample_trade();
	CHECK(static_cast<std::int64_t>(negative.px) == -10000);
}

} // namespace

int main() {
	std::println("Running codec tests...");

	test_little_endian_byte_order();
	test_little_endian_round_trip();
	test_little_endian_compares_by_value();
	test_message_type_validity();

	test_encode_writes_expected_header();
	test_encode_rejects_small_buffer();
	test_encode_does_not_overrun();

	test_round_trip_all_types();

	test_decode_incomplete();
	test_decode_unknown_type();
	test_decode_bad_version();
	test_decode_length_mismatch();
	test_decode_absurd_length_does_not_wait();

	test_decode_rejects_invalid_fields();
	test_zeroed_buffer_is_not_a_valid_frame();

	test_decode_consumes_one_frame_at_a_time();
	test_partial_second_frame_reports_incomplete();

	test_price_scaling_round_trips();

	if (checks_failed == 0) {
		std::println("All {} checks passed.", checks_run);
		return 0;
	}
	std::println("{} of {} checks FAILED.", checks_failed, checks_run);
	return 1;
}
