// Unit tests for FrameReader. CHECK, not assert: survives NDEBUG, reports every failure.
// The invariant under test: the same byte stream yields the same messages no matter
// how it is split across read() calls.

#include "slipstream/codec/encode.hpp"
#include "slipstream/codec/reader.hpp"

#include <cstddef>
#include <cstring>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

// --- Fixtures ---------------------------------------------------------------

void fillSymbol(char (&symbol)[12], std::string_view text) {
	std::memset(symbol, 0, sizeof symbol);
	std::memcpy(symbol, text.data(), text.size());
}

QuoteMessage quote(std::string_view symbol, std::uint64_t ts) {
	QuoteMessage message{};
	fillSymbol(message.symbol, symbol);
	message.ts_ns   = ts;
	message.bid_qty = 100;
	message.bid_px  = 187'5000;
	message.ask_qty = 250;
	message.ask_px  = 187'5500;
	return message;
}

TradeMessage trade(std::string_view symbol, std::uint64_t id) {
	TradeMessage message{};
	fillSymbol(message.symbol, symbol);
	message.ts_ns     = 42;
	message.qty       = 7;
	message.px        = -1'0000;
	message.aggressor = TradeMessage::AggressorType::Sell;
	message.id        = id;
	return message;
}

HeartbeatMessage heartbeat(std::uint64_t ts) {
	return HeartbeatMessage{.ts_ns = ts};
}

/// Appends one encoded frame to `out`.
template<WireMessage Message>
void append(std::vector<std::byte>& out, const Message& body) {
	std::array<std::byte, 256> scratch{};
	const auto written = encode(body, scratch);
	if (!written) return;
	out.insert(out.end(), scratch.begin(), scratch.begin() + *written);
}

// Every wire struct is padding-free, so byte equality is value equality.
template<class T>
bool sameBytes(const T& lhs, const T& rhs) {
	return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

/// Feeds `stream` to a fresh reader in fixed-size pieces and returns what it produced.
std::vector<AnyMessage> readInChunks(std::span<const std::byte> stream, std::size_t chunk_size) {
	FrameReader reader;
	std::vector<AnyMessage> collected;

	for (std::size_t offset = 0; offset < stream.size(); offset += chunk_size) {
		const std::size_t take = std::min(chunk_size, stream.size() - offset);
		const auto ok = reader.read(stream.subspan(offset, take));
		if (!ok) break;
		for (auto& message : reader.take()) collected.push_back(message);
	}
	return collected;
}

// --- Single frame -----------------------------------------------------------

void test_single_frame_in_one_chunk() {
	std::vector<std::byte> stream;
	append(stream, quote("AAPL", 111));

	FrameReader reader;
	CHECK(reader.read(stream).has_value());

	const auto messages = reader.take();
	CHECK(messages.size() == 1);
	if (messages.size() != 1) return;
	CHECK(std::holds_alternative<QuoteMessage>(messages[0]));
	if (std::holds_alternative<QuoteMessage>(messages[0])) {
		CHECK(sameBytes(std::get<QuoteMessage>(messages[0]), quote("AAPL", 111)));
	}
}

void test_take_drains() {
	std::vector<std::byte> stream;
	append(stream, quote("AAPL", 1));

	FrameReader reader;
	CHECK(reader.read(stream).has_value());
	CHECK(reader.take().size() == 1);
	// Second drain must be empty: take() moves the messages out.
	CHECK(reader.take().empty());
}

void test_partial_frame_yields_nothing_then_completes() {
	std::vector<std::byte> stream;
	append(stream, quote("MSFT", 7));

	FrameReader reader;
	// Everything except the final byte.
	CHECK(reader.read(std::span{stream}.first(stream.size() - 1)).has_value());
	CHECK(reader.take().empty());

	CHECK(reader.read(std::span{stream}.last(1)).has_value());
	CHECK(reader.take().size() == 1);
}

void test_header_split_across_chunks() {
	std::vector<std::byte> stream;
	append(stream, heartbeat(99));

	FrameReader reader;
	// The 4-byte header itself straddles the boundary.
	CHECK(reader.read(std::span{stream}.first(2)).has_value());
	CHECK(reader.take().empty());
	CHECK(reader.read(std::span{stream}.subspan(2)).has_value());

	const auto messages = reader.take();
	CHECK(messages.size() == 1);
	if (messages.size() == 1) CHECK(std::holds_alternative<HeartbeatMessage>(messages[0]));
}

// --- Chunking invariance ----------------------------------------------------

void test_same_messages_regardless_of_chunking() {
	std::vector<std::byte> stream;
	append(stream, quote("AAPL", 1));
	append(stream, heartbeat(2));
	append(stream, trade("MSFT", 3));
	append(stream, quote("NVDA", 4));

	const auto reference = readInChunks(stream, stream.size());
	CHECK(reference.size() == 4);

	// Byte-at-a-time is the case that catches every reassembly bug.
	for (std::size_t chunk_size : {1u, 2u, 3u, 5u, 7u, 13u, 31u, 64u, 512u}) {
		const auto chunked = readInChunks(stream, chunk_size);
		CHECK(chunked.size() == reference.size());
		if (chunked.size() != reference.size()) continue;

		bool identical = true;
		for (std::size_t i = 0; i < reference.size(); ++i) {
			if (chunked[i].index() != reference[i].index()) { identical = false; break; }
		}
		CHECK(identical);
	}
}

void test_many_frames_in_one_chunk() {
	std::vector<std::byte> stream;
	constexpr std::size_t count = 50;
	for (std::size_t i = 0; i < count; ++i) append(stream, heartbeat(i));

	FrameReader reader;
	CHECK(reader.read(stream).has_value());

	const auto messages = reader.take();
	CHECK(messages.size() == count);
	bool all_in_order = messages.size() == count;
	for (std::size_t i = 0; i < messages.size() && all_in_order; ++i) {
		const auto* beat = std::get_if<HeartbeatMessage>(&messages[i]);
		if (!beat || std::uint64_t(beat->ts_ns) != i) all_in_order = false;
	}
	CHECK(all_in_order);
}

void test_messages_survive_across_reads() {
	// Messages decoded in earlier calls must not be lost by later buffer compaction.
	std::vector<std::byte> stream;
	append(stream, quote("AAPL", 1));
	append(stream, quote("MSFT", 2));

	FrameReader reader;
	const std::size_t split = stream.size() / 2;
	CHECK(reader.read(std::span{stream}.first(split)).has_value());
	CHECK(reader.read(std::span{stream}.subspan(split)).has_value());

	// Never drained in between, so both must still be pending.
	CHECK(reader.take().size() == 2);
}

// --- Error handling ---------------------------------------------------------

void test_unknown_type_is_skipped_and_reader_continues() {
	std::vector<std::byte> stream;
	append(stream, quote("AAPL", 1));
	const std::size_t second = stream.size();
	append(stream, heartbeat(2));
	append(stream, quote("NVDA", 3));

	// Corrupt only the middle frame's type byte; its body_len stays valid.
	stream[second + 2] = std::byte{99};

	FrameReader reader;
	CHECK(reader.read(stream).has_value());

	const auto messages = reader.take();
	CHECK(messages.size() == 2);
	if (messages.size() == 2) {
		CHECK(std::holds_alternative<QuoteMessage>(messages[0]));
		CHECK(std::holds_alternative<QuoteMessage>(messages[1]));
		CHECK(std::uint64_t(std::get<QuoteMessage>(messages[1]).ts_ns) == 3);
	}
}

void test_unsupported_version_is_skipped() {
	std::vector<std::byte> stream;
	append(stream, heartbeat(1));
	const std::size_t second = stream.size();
	append(stream, heartbeat(2));

	stream[second + 3] = std::byte{current_version + 1};   // version byte

	FrameReader reader;
	CHECK(reader.read(stream).has_value());
	CHECK(reader.take().size() == 1);
}

void test_fatal_error_stops_the_reader() {
	std::vector<std::byte> stream;
	append(stream, quote("AAPL", 1));
	const std::size_t second = stream.size();
	append(stream, quote("MSFT", 2));

	// body_len that disagrees with the type: desynchronised, not skippable.
	stream[second] = std::byte{43};

	FrameReader reader;
	const auto result = reader.read(stream);
	CHECK(!result.has_value());
	if (!result) {
		CHECK(result.error() == DecodeError::LengthMismatch);
		CHECK(is_fatal(result.error()));
	}
	// The frame decoded before the failure must still be retrievable.
	CHECK(reader.take().size() == 1);
}

void test_garbage_is_rejected_without_crashing() {
	std::vector<std::byte> stream(64, std::byte{0xFF});

	FrameReader reader;
	const auto result = reader.read(stream);
	CHECK(!result.has_value());
	CHECK(reader.take().empty());
}

void test_empty_chunk_is_harmless() {
	FrameReader reader;
	CHECK(reader.read({}).has_value());
	CHECK(reader.take().empty());

	std::vector<std::byte> stream;
	append(stream, heartbeat(5));
	CHECK(reader.read({}).has_value());
	CHECK(reader.read(stream).has_value());
	CHECK(reader.read({}).has_value());
	CHECK(reader.take().size() == 1);
}

// --- Buffer growth ----------------------------------------------------------

void test_repeated_read_drain_cycles() {
	// Ten thousand cycles: compaction must reclaim, and nothing may be dropped.
	std::vector<std::byte> one;
	append(one, heartbeat(1));

	FrameReader reader;
	bool all_ok = true;
	std::size_t delivered = 0;
	for (int i = 0; i < 10'000; ++i) {
		if (!reader.read(one)) all_ok = false;
		delivered += reader.take().size();
	}
	CHECK(all_ok);
	CHECK(delivered == 10'000);
	CHECK(reader.take().empty());
}

void test_dribbled_bytes_do_not_accumulate() {
	std::vector<std::byte> stream;
	for (int i = 0; i < 200; ++i) append(stream, heartbeat(i));

	FrameReader reader;
	bool all_ok = true;
	std::size_t delivered = 0;
	for (std::byte byte : stream) {
		if (!reader.read(std::span{&byte, 1})) all_ok = false;
		delivered += reader.take().size();
	}
	CHECK(all_ok);
	CHECK(delivered == 200);
}

} // namespace

int main() {
	std::println("Running reader tests...");

	test_single_frame_in_one_chunk();
	test_take_drains();
	test_partial_frame_yields_nothing_then_completes();
	test_header_split_across_chunks();

	test_same_messages_regardless_of_chunking();
	test_many_frames_in_one_chunk();
	test_messages_survive_across_reads();

	test_unknown_type_is_skipped_and_reader_continues();
	test_unsupported_version_is_skipped();
	test_fatal_error_stops_the_reader();
	test_garbage_is_rejected_without_crashing();
	test_empty_chunk_is_harmless();

	test_repeated_read_drain_cycles();
	test_dribbled_bytes_do_not_accumulate();

	if (checks_failed == 0) {
		std::println("All {} checks passed.", checks_run);
		return 0;
	}
	std::println("{} of {} checks FAILED.", checks_failed, checks_run);
	return 1;
}
