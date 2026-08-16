#include "slipstream/replay/csv.hpp"

#include <charconv>
#include <cstring>

namespace slipstream::csv {

namespace {

constexpr std::size_t price_decimals = 4;	// price_scale is 10^4
static_assert(price_scale == 10'000);

template<class T>
bool parseUnsigned(std::string_view text, T& out) noexcept {
	if (text.empty()) return false;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
	return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

} // namespace

std::expected<Fields, ParseError> splitLine(std::string_view line) noexcept {
	Fields fields{};
	std::size_t count = 0;
	std::size_t start = 0;

	while (true) {
		const std::size_t comma = line.find(',', start);
		const std::size_t end = (comma == std::string_view::npos) ? line.size() : comma;
		if (count == fields.size()) return std::unexpected(ParseError::FieldCount);
		fields[count++] = line.substr(start, end - start);
		if (comma == std::string_view::npos) break;
		start = comma + 1;
	}

	if (count != fields.size()) return std::unexpected(ParseError::FieldCount);
	return fields;
}

std::expected<void, ParseError> validateHeader(const Fields& fields) noexcept {
	for (std::size_t i = 0; i < column_count; ++i) {
		if (fields[i] != column_names[i]) return std::unexpected(ParseError::BadHeader);
	}
	return {};
}

std::expected<std::uint64_t, ParseError> parseTimestamp(std::string_view text) noexcept {
	if (text.empty()) return std::unexpected(ParseError::BadTimestamp);

	if (text.find(':') == std::string_view::npos) {
		std::uint64_t nanoseconds = 0;
		if (!parseUnsigned(text, nanoseconds)) return std::unexpected(ParseError::BadTimestamp);
		return nanoseconds;
	}

	// HH:MM:SS with an optional fractional part of up to nine digits.
	if (text.size() < 8 || text[2] != ':' || text[5] != ':') {
		return std::unexpected(ParseError::BadTimestamp);
	}

	std::uint32_t hours = 0, minutes = 0, seconds = 0;
	if (!parseUnsigned(text.substr(0, 2), hours)
	 || !parseUnsigned(text.substr(3, 2), minutes)
	 || !parseUnsigned(text.substr(6, 2), seconds)) {
		return std::unexpected(ParseError::BadTimestamp);
	}
	if (hours > 23 || minutes > 59 || seconds > 59) return std::unexpected(ParseError::BadTimestamp);

	std::uint64_t fraction = 0;
	if (text.size() > 8) {
		if (text[8] != '.') return std::unexpected(ParseError::BadTimestamp);
		const std::string_view digits = text.substr(9);
		if (digits.empty() || digits.size() > 9) return std::unexpected(ParseError::BadTimestamp);
		if (!parseUnsigned(digits, fraction)) return std::unexpected(ParseError::BadTimestamp);
		for (std::size_t i = digits.size(); i < 9; ++i) fraction *= 10;
	}

	const std::uint64_t whole = (std::uint64_t{hours} * 3600 + minutes * 60 + seconds);
	return whole * 1'000'000'000ull + fraction;
}

std::expected<std::int64_t, ParseError> parsePrice(std::string_view text) noexcept {
	if (text.empty()) return std::unexpected(ParseError::BadPrice);

	bool negative = false;
	if (text.front() == '-') { negative = true; text.remove_prefix(1); }
	else if (text.front() == '+') { text.remove_prefix(1); }

	const std::size_t dot = text.find('.');
	const std::string_view whole_text = text.substr(0, dot);
	const std::string_view fraction_text =
		(dot == std::string_view::npos) ? std::string_view{} : text.substr(dot + 1);

	if (whole_text.empty() || fraction_text.size() > price_decimals) {
		return std::unexpected(ParseError::BadPrice);
	}

	std::int64_t whole = 0;
	if (!parseUnsigned(whole_text, whole)) return std::unexpected(ParseError::BadPrice);

	std::int64_t fraction = 0;
	if (!fraction_text.empty() && !parseUnsigned(fraction_text, fraction)) {
		return std::unexpected(ParseError::BadPrice);
	}
	// Right-pad so "87.4" is 4000 ten-thousandths, not 4.
	for (std::size_t i = fraction_text.size(); i < price_decimals; ++i) fraction *= 10;

	const std::int64_t scaled = whole * price_scale + fraction;
	return negative ? -scaled : scaled;
}

std::expected<std::uint32_t, ParseError> parseQuantity(std::string_view text) noexcept {
	std::uint32_t quantity = 0;
	if (!parseUnsigned(text, quantity)) return std::unexpected(ParseError::BadQuantity);
	return quantity;
}

std::expected<void, ParseError> parseSymbol(std::string_view text, char (&out)[12]) noexcept {
	if (text.empty() || text.size() > sizeof out) return std::unexpected(ParseError::BadSymbol);
	for (char character : text) {
		if (character < 0x20 || character > 0x7E) return std::unexpected(ParseError::BadSymbol);
	}
	std::memset(out, 0, sizeof out);
	std::memcpy(out, text.data(), text.size());
	return {};
}

std::expected<TradeMessage::AggressorType, ParseError> parseAggressor(std::string_view text) noexcept {
	if (text.size() > 1) return std::unexpected(ParseError::BadAggressor);
	if (text.size() == 0 || text[0] == '?') return TradeMessage::AggressorType::Unknown;
	if (text[0] == 'B') return TradeMessage::AggressorType::Buy;
	if (text[0] == 'S') return TradeMessage::AggressorType::Sell;
	return std::unexpected(ParseError::BadAggressor);
}

template<>
std::expected<QuoteMessage, ParseError> fromRow<QuoteMessage>(const Fields& fields) noexcept {
	if (!field(fields, Column::Price).empty()
		|| !field(fields, Column::Qty).empty()
		|| !field(fields, Column::Aggressor).empty())
	{
		return std::unexpected(ParseError::UnexpectedField);
	}

	QuoteMessage message{};
	if (const auto ok = parseSymbol(field(fields, Column::Symbol), message.symbol); !ok) {
		return std::unexpected(ok.error());
	}

	const auto timestamp = parseTimestamp(field(fields, Column::Timestamp));
	if (!timestamp) return std::unexpected(timestamp.error());
	const auto bid_price = parsePrice(field(fields, Column::BidPrice));
	if (!bid_price) return std::unexpected(bid_price.error());
	const auto ask_price = parsePrice(field(fields, Column::AskPrice));
	if (!ask_price) return std::unexpected(ask_price.error());
	const auto bid_quantity = parseQuantity(field(fields, Column::BidQty));
	if (!bid_quantity) return std::unexpected(bid_quantity.error());
	const auto ask_quantity = parseQuantity(field(fields, Column::AskQty));
	if (!ask_quantity) return std::unexpected(ask_quantity.error());

	message.ts_ns   = *timestamp;
	message.bid_px  = *bid_price;
	message.ask_px  = *ask_price;
	message.bid_qty = *bid_quantity;
	message.ask_qty = *ask_quantity;
	return message;
}

template<>
std::expected<TradeMessage, ParseError> fromRow<TradeMessage>(const Fields& fields) noexcept {
	if (!field(fields, Column::BidPrice).empty()
		|| !field(fields, Column::AskPrice).empty()
		|| !field(fields, Column::BidQty).empty()
		|| !field(fields, Column::AskQty).empty())
	{
		return std::unexpected(ParseError::UnexpectedField);
	}

	TradeMessage message{};
	if (const auto ok = parseSymbol(field(fields, Column::Symbol), message.symbol); !ok) {
		return std::unexpected(ok.error());
	}

	const auto timestamp = parseTimestamp(field(fields, Column::Timestamp));
	if (!timestamp) return std::unexpected(timestamp.error());
	const auto price = parsePrice(field(fields, Column::Price));
	if (!price) return std::unexpected(price.error());
	const auto quantity = parseQuantity(field(fields, Column::Qty));
	if (!quantity) return std::unexpected(quantity.error());
	const auto aggressor = parseAggressor(field(fields, Column::Aggressor));
	if (!aggressor) return std::unexpected(aggressor.error());

	message.ts_ns 		= *timestamp;
	message.qty   		= *quantity;
	message.px 			= *price;
	message.aggressor 	= *aggressor;
	// ids belong to the sender, not the row.
	return message;
}

} // namespace slipstream::csv
