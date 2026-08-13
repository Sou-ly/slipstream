#pragma once

#include <slipstream/codec/message.hpp>
#include <slipstream/error/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace slipstream::csv {

enum class ParseError : std::uint8_t {
	StreamUnreadable	= 0,
	MissingHeader		= 1,
	BadHeader			= 2,
	FieldCount			= 3,
	BadTimestamp		= 4,
	BadPrice			= 5,
	BadQuantity			= 6,
	BadSymbol			= 7,
	UnexpectedField		= 8,
};

// No default: -Werror=switch catches an enumerator added without a message.
constexpr std::string_view describe(ParseError error) noexcept {
	switch (error) {
		case ParseError::StreamUnreadable:	return "stream unreadable";
		case ParseError::MissingHeader:		return "missing header row";
		case ParseError::BadHeader:			return "unexpected column name";
		case ParseError::FieldCount:		return "wrong number of fields";
		case ParseError::BadTimestamp:		return "malformed timestamp";
		case ParseError::BadPrice:			return "malformed price";
		case ParseError::BadQuantity:		return "malformed quantity";
		case ParseError::BadSymbol:			return "malformed symbol";
		case ParseError::UnexpectedField:	return "field set for the wrong row type";
	}
	return "unknown parse error";
}
static_assert(DescribableError<ParseError>);

enum class Column : std::size_t {
	Timestamp	= 0,
	Type		= 1,
	Symbol		= 2,
	BidPrice	= 3,
	BidQty		= 4,
	AskPrice	= 5,
	AskQty		= 6,
	Price		= 7,
	Qty			= 8,
};

inline constexpr std::size_t column_count = 9;

inline constexpr std::array<std::string_view, column_count> column_names {
	"Timestamp", "Type", "Symbol", "BidPrice", "BidQty", "AskPrice", "AskQty", "Price", "Qty",
};

/// Views into the caller's line buffer, valid only while that line is alive.
using Fields = std::array<std::string_view, column_count>;

constexpr std::string_view field(const Fields& fields, Column column) noexcept {
	return fields[static_cast<std::size_t>(column)];
}

std::expected<Fields, ParseError> splitLine(std::string_view line) noexcept;
std::expected<void, ParseError> validateHeader(const Fields& fields) noexcept;

/// "HH:MM:SS.mmm" or a bare nanosecond count.
std::expected<std::uint64_t, ParseError> parseTimestamp(std::string_view text) noexcept;
/// Decimal to fixed point, scaled by price_scale, without going through a double.
std::expected<std::int64_t, ParseError> parsePrice(std::string_view text) noexcept;
std::expected<std::uint32_t, ParseError> parseQuantity(std::string_view text) noexcept;
std::expected<void, ParseError> parseSymbol(std::string_view text, char (&out)[12]) noexcept;

/// The CSV Type column that produces this message. Specialised only where one exists.
template<WireMessage Message> constexpr char csvRowCode();
template<> constexpr char csvRowCode<QuoteMessage>()    { return 'Q'; }
template<> constexpr char csvRowCode<NewOrderMessage>() { return 'T'; }

template<WireMessage Message>
std::expected<Message, ParseError> fromRow(const Fields& fields) noexcept;

template<> std::expected<QuoteMessage, ParseError>    fromRow<QuoteMessage>(const Fields&) noexcept;
template<> std::expected<NewOrderMessage, ParseError> fromRow<NewOrderMessage>(const Fields&) noexcept;

/// Reads every row whose Type column matches csvRowCode<Message>().
template<WireMessage Message>
std::expected<std::vector<Message>, ParseError> tryParse(std::istream& in) {
	if (!in) return std::unexpected(ParseError::StreamUnreadable);

	constexpr char code = csvRowCode<Message>();
	std::vector<Message> messages;
	std::string line;
	bool header_seen = false;

	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty() || line.front() == '#') continue;

		const auto fields = splitLine(line);
		if (!fields) return std::unexpected(fields.error());

		if (!header_seen) {
			header_seen = true;
			if (const auto ok = validateHeader(*fields); !ok) return std::unexpected(ok.error());
			continue;
		}

		const std::string_view type = field(*fields, Column::Type);
		if (type.size() != 1 || type[0] != code) continue;

		const auto message = fromRow<Message>(*fields);
		if (!message) return std::unexpected(message.error());
		messages.push_back(*message);
	}

	if (!header_seen) return std::unexpected(ParseError::MissingHeader);
	return messages;
}

} // namespace slipstream::csv
