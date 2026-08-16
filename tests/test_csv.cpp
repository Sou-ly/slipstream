// Unit tests for the CSV reader. CHECK, not assert: survives NDEBUG, reports every failure.

#include "slipstream/replay/csv.hpp"

#include <print>
#include <sstream>
#include <string>
#include <string_view>

using namespace slipstream;
using namespace slipstream::csv;

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

constexpr std::string_view header =
	"Timestamp,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty,Aggressor\n";

std::string withHeader(std::string_view body) {
	return std::string{header} + std::string{body};
}

std::string symbolOf(const char (&symbol)[12]) {
	std::size_t length = 0;
	while (length < 12 && symbol[length] != '\0') ++length;
	return std::string{symbol, length};
}

// --- Field helpers ----------------------------------------------------------

void test_column_names_match_the_layout() {
	CHECK(column_count == 10);
	CHECK(column_names[static_cast<std::size_t>(Column::Aggressor)] == "Aggressor");
}

void test_split_line_counts_trailing_empty_fields() {
	// The bug getline hides: a quote line ending in ",,," still has ten fields.
	const auto fields = splitLine("09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,,");
	CHECK(fields.has_value());
	if (!fields) return;
	CHECK(field(*fields, Column::Timestamp) == "09:30:00.003");
	CHECK(field(*fields, Column::Type) == "Q");
	CHECK(field(*fields, Column::AskQty) == "50");
	CHECK(field(*fields, Column::Price).empty());
	CHECK(field(*fields, Column::Qty).empty());
	CHECK(field(*fields, Column::Aggressor).empty());
}

void test_split_line_keeps_the_trade_columns() {
	const auto fields = splitLine("09:30:00.190,T,SYNTH2,,,,,248.53,65,B");
	CHECK(fields.has_value());
	if (!fields) return;
	CHECK(field(*fields, Column::BidPrice).empty());
	CHECK(field(*fields, Column::AskQty).empty());
	CHECK(field(*fields, Column::Price) == "248.53");
	CHECK(field(*fields, Column::Qty) == "65");
	CHECK(field(*fields, Column::Aggressor) == "B");
}

void test_split_line_rejects_wrong_field_count() {
	CHECK(!splitLine("a,b,c").has_value());
	CHECK(splitLine("a,b,c").error() == ParseError::FieldCount);
	// Nine fields is the old pre-Aggressor layout: it must no longer be accepted.
	CHECK(!splitLine("a,b,c,d,e,f,g,h,i").has_value());
	CHECK(splitLine("a,b,c,d,e,f,g,h,i").error() == ParseError::FieldCount);
	CHECK(!splitLine("a,b,c,d,e,f,g,h,i,j,k").has_value());
	CHECK(splitLine("a,b,c,d,e,f,g,h,i,j,k").error() == ParseError::FieldCount);
}

void test_validate_header() {
	const auto good = splitLine("Timestamp,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty,Aggressor");
	CHECK(good.has_value() && validateHeader(*good).has_value());

	const auto bad = splitLine("Time,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty,Aggressor");
	CHECK(bad.has_value());
	if (bad) CHECK(!validateHeader(*bad).has_value());

	// Right column count, wrong name in the last slot.
	const auto renamed = splitLine("Timestamp,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty,Side");
	CHECK(renamed.has_value());
	if (renamed) {
		const auto result = validateHeader(*renamed);
		CHECK(!result.has_value());
		if (!result) CHECK(result.error() == ParseError::BadHeader);
	}
}

void test_parse_timestamp() {
	// 09:30:00.003 == 34200.003 s
	CHECK(parseTimestamp("09:30:00.003") == 34'200'003'000'000ull);
	CHECK(parseTimestamp("00:00:00.000") == 0ull);
	CHECK(parseTimestamp("23:59:59.999") == 86'399'999'000'000ull);
	// Fraction is right-padded, so .1 is 100 ms and not 1 ns.
	CHECK(parseTimestamp("00:00:00.1") == 100'000'000ull);
	CHECK(parseTimestamp("12345") == 12'345ull);

	CHECK(!parseTimestamp("").has_value());
	CHECK(!parseTimestamp("24:00:00.000").has_value());
	CHECK(!parseTimestamp("09:60:00.000").has_value());
	CHECK(!parseTimestamp("09:30:60.000").has_value());
	CHECK(!parseTimestamp("09-30-00.000").has_value());
	CHECK(!parseTimestamp("09:30:00.0000000001").has_value());
	CHECK(!parseTimestamp("aa:bb:cc.ddd").has_value());
}

void test_parse_price_is_exact() {
	CHECK(parsePrice("101.23") == 1'012'300);
	CHECK(parsePrice("87.4") == 874'000);      // right-padded, not 87.0004
	CHECK(parsePrice("1250") == 12'500'000);
	CHECK(parsePrice("0.0001") == 1);
	CHECK(parsePrice("-1.5") == -15'000);
	CHECK(parsePrice("0") == 0);

	CHECK(!parsePrice("").has_value());
	CHECK(!parsePrice(".5").has_value());
	CHECK(!parsePrice("1.23456").has_value());   // more precision than the scale holds
	CHECK(!parsePrice("12x.5").has_value());
	CHECK(!parsePrice("1.2.3").has_value());
}

void test_parse_symbol() {
	char symbol[12];
	CHECK(parseSymbol("SYNTH1", symbol).has_value());
	CHECK(symbolOf(symbol) == "SYNTH1");
	// Padding must be NUL all the way to the end.
	CHECK(symbol[6] == '\0' && symbol[11] == '\0');

	CHECK(parseSymbol("ABCDEFGHIJKL", symbol).has_value());   // exactly 12, no terminator
	CHECK(!parseSymbol("ABCDEFGHIJKLM", symbol).has_value());
	CHECK(!parseSymbol("", symbol).has_value());
	CHECK(!parseSymbol("AB\x01Z", symbol).has_value());
}

void test_parse_aggressor() {
	using Aggressor = TradeMessage::AggressorType;

	CHECK(parseAggressor("B") == Aggressor::Buy);
	CHECK(parseAggressor("S") == Aggressor::Sell);
	CHECK(parseAggressor("?") == Aggressor::Unknown);
	// A feed that does not tag the aggressor leaves the column empty.
	CHECK(parseAggressor("") == Aggressor::Unknown);

	CHECK(!parseAggressor("b").has_value());   // case-sensitive on purpose
	CHECK(!parseAggressor("X").has_value());
	CHECK(!parseAggressor("BUY").has_value());

	// Its own error code, so the diagnostic does not say "malformed symbol".
	CHECK(parseAggressor("X").error() == ParseError::BadAggressor);
	CHECK(parseAggressor("BUY").error() == ParseError::BadAggressor);
	CHECK(describe(ParseError::BadAggressor) == "malformed aggressor");
}

// --- Row conversion ---------------------------------------------------------

void test_quote_row() {
	const auto fields = splitLine("09:30:00.215,Q,SYNTH1,101.23,175,101.25,150,,,");
	CHECK(fields.has_value());
	if (!fields) return;

	const auto quote = fromRow<QuoteMessage>(*fields);
	CHECK(quote.has_value());
	if (!quote) return;

	CHECK(symbolOf(quote->symbol) == "SYNTH1");
	CHECK(quote->ts_ns == 34'200'215'000'000ull);
	CHECK(quote->bid_px == 1'012'300);
	CHECK(quote->ask_px == 1'012'500);
	CHECK(quote->bid_qty == 175u);
	CHECK(quote->ask_qty == 150u);
}

void test_trade_row() {
	const auto fields = splitLine("09:30:00.190,T,SYNTH2,,,,,248.53,65,B");
	CHECK(fields.has_value());
	if (!fields) return;

	const auto trade = fromRow<TradeMessage>(*fields);
	CHECK(trade.has_value());
	if (!trade) return;

	CHECK(symbolOf(trade->symbol) == "SYNTH2");
	CHECK(trade->ts_ns == 34'200'190'000'000ull);
	CHECK(trade->px == 2'485'300);
	CHECK(trade->qty == 65u);
	CHECK(trade->aggressor == TradeMessage::AggressorType::Buy);
	// The id belongs to the sender, not the row: it stays zero here.
	CHECK(trade->id == 0ull);
}

void test_trade_row_sell_and_untagged() {
	const auto sell = splitLine("09:30:02.051,T,SYNTH4,,,,,34.22,225,S");
	CHECK(sell.has_value());
	if (sell) {
		const auto trade = fromRow<TradeMessage>(*sell);
		CHECK(trade.has_value());
		if (trade) {
			CHECK(trade->px == 342'200);
			CHECK(trade->qty == 225u);
			CHECK(trade->aggressor == TradeMessage::AggressorType::Sell);
		}
	}

	const auto untagged = splitLine("09:30:02.051,T,SYNTH4,,,,,34.22,225,");
	CHECK(untagged.has_value());
	if (untagged) {
		const auto trade = fromRow<TradeMessage>(*untagged);
		CHECK(trade.has_value());
		if (trade) CHECK(trade->aggressor == TradeMessage::AggressorType::Unknown);
	}
}

void test_trade_row_rejects_bad_aggressor() {
	const auto fields = splitLine("09:30:00.190,T,SYNTH2,,,,,248.53,65,X");
	CHECK(fields.has_value());
	if (!fields) return;

	const auto trade = fromRow<TradeMessage>(*fields);
	CHECK(!trade.has_value());
	if (!trade) CHECK(trade.error() == ParseError::BadAggressor);
}

void test_row_rejects_fields_from_the_other_type() {
	// A quote row must not carry Price/Qty, and a trade row must not carry a book.
	const auto quote_with_price = splitLine("09:30:00.1,Q,S,1.00,1,1.01,1,5.00,3,B");
	CHECK(quote_with_price.has_value());
	if (quote_with_price) {
		const auto result = fromRow<QuoteMessage>(*quote_with_price);
		CHECK(!result.has_value());
		if (!result) CHECK(result.error() == ParseError::UnexpectedField);
	}

	const auto trade_with_book = splitLine("09:30:00.1,T,S,1.00,1,1.01,1,5.00,3,B");
	CHECK(trade_with_book.has_value());
	if (trade_with_book) {
		const auto result = fromRow<TradeMessage>(*trade_with_book);
		CHECK(!result.has_value());
		if (!result) CHECK(result.error() == ParseError::UnexpectedField);
	}
}

void test_row_rejects_each_foreign_column_on_its_own() {
	// One stray column is enough: the guards must not depend on a whole book being present.
	const auto quote_with_aggressor = splitLine("09:30:00.1,Q,S,1.00,1,1.01,1,,,B");
	CHECK(quote_with_aggressor.has_value());
	if (quote_with_aggressor) {
		const auto result = fromRow<QuoteMessage>(*quote_with_aggressor);
		CHECK(!result.has_value());
		if (!result) CHECK(result.error() == ParseError::UnexpectedField);
	}

	const auto trade_with_bid_qty = splitLine("09:30:00.1,T,S,,1,,,5.00,3,B");
	CHECK(trade_with_bid_qty.has_value());
	if (trade_with_bid_qty) {
		const auto result = fromRow<TradeMessage>(*trade_with_bid_qty);
		CHECK(!result.has_value());
		if (!result) CHECK(result.error() == ParseError::UnexpectedField);
	}

	const auto trade_with_ask_qty = splitLine("09:30:00.1,T,S,,,,1,5.00,3,B");
	CHECK(trade_with_ask_qty.has_value());
	if (trade_with_ask_qty) {
		const auto result = fromRow<TradeMessage>(*trade_with_ask_qty);
		CHECK(!result.has_value());
		if (!result) CHECK(result.error() == ParseError::UnexpectedField);
	}

	// The clean shapes must still pass, or the guards above are just rejecting everything.
	const auto clean_quote = splitLine("09:30:00.1,Q,S,1.00,1,1.01,1,,,");
	CHECK(clean_quote.has_value());
	if (clean_quote) CHECK(fromRow<QuoteMessage>(*clean_quote).has_value());

	const auto clean_trade = splitLine("09:30:00.1,T,S,,,,,5.00,3,B");
	CHECK(clean_trade.has_value());
	if (clean_trade) CHECK(fromRow<TradeMessage>(*clean_trade).has_value());
}

// --- tryParse ---------------------------------------------------------------

void test_try_parse_filters_by_row_code() {
	std::istringstream quotes_in{withHeader(
		"09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,,\n"
		"09:30:00.190,T,SYNTH2,,,,,248.53,65,B\n"
		"09:30:00.215,Q,SYNTH1,101.23,175,101.25,150,,,\n")};

	const auto quotes = tryParse<QuoteMessage>(quotes_in);
	CHECK(quotes.has_value());
	if (quotes) {
		CHECK(quotes->size() == 2);
		if (quotes->size() == 2) {
			CHECK(symbolOf((*quotes)[0].symbol) == "SYNTH3");
			CHECK(symbolOf((*quotes)[1].symbol) == "SYNTH1");
		}
	}

	std::istringstream trades_in{withHeader(
		"09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,,\n"
		"09:30:00.190,T,SYNTH2,,,,,248.53,65,B\n"
		"09:30:02.051,T,SYNTH4,,,,,34.22,225,S\n")};

	const auto trades = tryParse<TradeMessage>(trades_in);
	CHECK(trades.has_value());
	if (trades) {
		CHECK(trades->size() == 2);
		if (trades->size() == 2) {
			CHECK(symbolOf((*trades)[0].symbol) == "SYNTH2");
			CHECK((*trades)[0].aggressor == TradeMessage::AggressorType::Buy);
			CHECK(symbolOf((*trades)[1].symbol) == "SYNTH4");
			CHECK((*trades)[1].aggressor == TradeMessage::AggressorType::Sell);
		}
	}
}

void test_try_parse_skips_comments_and_blank_lines() {
	std::istringstream in{
		"# a comment\n"
		"#\n"
		"\n"
		+ withHeader("09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,,\n")};

	const auto quotes = tryParse<QuoteMessage>(in);
	CHECK(quotes.has_value());
	if (quotes) CHECK(quotes->size() == 1);
}

void test_try_parse_handles_crlf() {
	std::istringstream in{
		"Timestamp,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty,Aggressor\r\n"
		"09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,,\r\n"
		"09:30:00.190,T,SYNTH2,,,,,248.53,65,B\r\n"};

	const auto quotes = tryParse<QuoteMessage>(in);
	CHECK(quotes.has_value());
	if (quotes) CHECK(quotes->size() == 1);
}

void test_try_parse_reports_errors() {
	std::istringstream no_header{"09:30:00.003,Q,S,1.00,1,1.01,1,,,\n"};
	const auto missing = tryParse<QuoteMessage>(no_header);
	CHECK(!missing.has_value());
	if (!missing) CHECK(missing.error() == ParseError::BadHeader);

	std::istringstream empty{""};
	const auto no_rows = tryParse<QuoteMessage>(empty);
	CHECK(!no_rows.has_value());
	if (!no_rows) CHECK(no_rows.error() == ParseError::MissingHeader);

	std::istringstream bad_price{withHeader("09:30:00.003,Q,S,1.2.3,1,1.01,1,,,\n")};
	const auto price_error = tryParse<QuoteMessage>(bad_price);
	CHECK(!price_error.has_value());
	if (!price_error) CHECK(price_error.error() == ParseError::BadPrice);

	// A row in the old nine-column shape is a field-count error, not a silent skip.
	std::istringstream old_layout{withHeader("09:30:00.190,T,SYNTH2,,,,,248.53,65\n")};
	const auto count_error = tryParse<TradeMessage>(old_layout);
	CHECK(!count_error.has_value());
	if (!count_error) CHECK(count_error.error() == ParseError::FieldCount);

	std::istringstream short_row{withHeader("09:30:00.003,Q,S\n")};
	CHECK(!tryParse<QuoteMessage>(short_row).has_value());
}

void test_try_parse_header_only_yields_nothing() {
	std::istringstream in{std::string{header}};
	const auto quotes = tryParse<QuoteMessage>(in);
	CHECK(quotes.has_value());
	if (quotes) CHECK(quotes->empty());

	std::istringstream same{std::string{header}};
	const auto trades = tryParse<TradeMessage>(same);
	CHECK(trades.has_value());
	if (trades) CHECK(trades->empty());
}

} // namespace

int main() {
	std::println("Running csv tests...");

	test_column_names_match_the_layout();
	test_split_line_counts_trailing_empty_fields();
	test_split_line_keeps_the_trade_columns();
	test_split_line_rejects_wrong_field_count();
	test_validate_header();

	test_parse_timestamp();
	test_parse_price_is_exact();
	test_parse_symbol();
	test_parse_aggressor();

	test_quote_row();
	test_trade_row();
	test_trade_row_sell_and_untagged();
	test_trade_row_rejects_bad_aggressor();
	test_row_rejects_fields_from_the_other_type();
	test_row_rejects_each_foreign_column_on_its_own();

	test_try_parse_filters_by_row_code();
	test_try_parse_skips_comments_and_blank_lines();
	test_try_parse_handles_crlf();
	test_try_parse_reports_errors();
	test_try_parse_header_only_yields_nothing();

	if (checks_failed == 0) {
		std::println("All {} checks passed.", checks_run);
		return 0;
	}
	std::println("{} of {} checks FAILED.", checks_failed, checks_run);
	return 1;
}
