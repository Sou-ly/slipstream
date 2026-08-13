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
	"Timestamp,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty\n";

std::string withHeader(std::string_view body) {
	return std::string{header} + std::string{body};
}

std::string symbolOf(const char (&symbol)[12]) {
	std::size_t length = 0;
	while (length < 12 && symbol[length] != '\0') ++length;
	return std::string{symbol, length};
}

// --- Field helpers ----------------------------------------------------------

void test_split_line_counts_trailing_empty_fields() {
	// The bug getline hides: a line ending in ",," still has nine fields.
	const auto fields = splitLine("09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,");
	CHECK(fields.has_value());
	if (!fields) return;
	CHECK(field(*fields, Column::Timestamp) == "09:30:00.003");
	CHECK(field(*fields, Column::Type) == "Q");
	CHECK(field(*fields, Column::AskQty) == "50");
	CHECK(field(*fields, Column::Price).empty());
	CHECK(field(*fields, Column::Qty).empty());
}

void test_split_line_rejects_wrong_field_count() {
	CHECK(!splitLine("a,b,c").has_value());
	CHECK(splitLine("a,b,c").error() == ParseError::FieldCount);
	CHECK(!splitLine("a,b,c,d,e,f,g,h,i,j").has_value());
	CHECK(splitLine("a,b,c,d,e,f,g,h,i,j").error() == ParseError::FieldCount);
}

void test_validate_header() {
	const auto good = splitLine("Timestamp,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty");
	CHECK(good.has_value() && validateHeader(*good).has_value());

	const auto bad = splitLine("Time,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty");
	CHECK(bad.has_value());
	if (bad) CHECK(!validateHeader(*bad).has_value());
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

// --- Row conversion ---------------------------------------------------------

void test_quote_row() {
	const auto fields = splitLine("09:30:00.215,Q,SYNTH1,101.23,175,101.25,150,,");
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

void test_new_order_row() {
	const auto fields = splitLine("09:30:00.190,T,SYNTH2,,,,,248.53,65");
	CHECK(fields.has_value());
	if (!fields) return;

	const auto order = fromRow<NewOrderMessage>(*fields);
	CHECK(order.has_value());
	if (!order) return;

	CHECK(symbolOf(order->symbol) == "SYNTH2");
	CHECK(order->ts_ns == 34'200'190'000'000ull);
	CHECK(order->limit == 2'485'300);
	CHECK(order->qty == 65u);
}

void test_row_rejects_fields_from_the_other_type() {
	// A quote row must not carry Price/Qty, and a trade row must not carry a book.
	const auto quote_with_price = splitLine("09:30:00.1,Q,S,1.00,1,1.01,1,5.00,3");
	CHECK(quote_with_price.has_value());
	if (quote_with_price) {
		const auto result = fromRow<QuoteMessage>(*quote_with_price);
		CHECK(!result.has_value());
		if (!result) CHECK(result.error() == ParseError::UnexpectedField);
	}

	const auto trade_with_book = splitLine("09:30:00.1,T,S,1.00,1,1.01,1,5.00,3");
	CHECK(trade_with_book.has_value());
	if (trade_with_book) CHECK(!fromRow<NewOrderMessage>(*trade_with_book).has_value());
}

// --- tryParse ---------------------------------------------------------------

void test_try_parse_filters_by_row_code() {
	std::istringstream quotes_in{withHeader(
		"09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,\n"
		"09:30:00.190,T,SYNTH2,,,,,248.53,65\n"
		"09:30:00.215,Q,SYNTH1,101.23,175,101.25,150,,\n")};

	const auto quotes = tryParse<QuoteMessage>(quotes_in);
	CHECK(quotes.has_value());
	if (quotes) {
		CHECK(quotes->size() == 2);
		if (quotes->size() == 2) {
			CHECK(symbolOf((*quotes)[0].symbol) == "SYNTH3");
			CHECK(symbolOf((*quotes)[1].symbol) == "SYNTH1");
		}
	}

	std::istringstream orders_in{withHeader(
		"09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,\n"
		"09:30:00.190,T,SYNTH2,,,,,248.53,65\n")};

	const auto orders = tryParse<NewOrderMessage>(orders_in);
	CHECK(orders.has_value());
	if (orders) {
		CHECK(orders->size() == 1);
		if (orders->size() == 1) CHECK(symbolOf((*orders)[0].symbol) == "SYNTH2");
	}
}

void test_try_parse_skips_comments_and_blank_lines() {
	std::istringstream in{
		"# a comment\n"
		"#\n"
		"\n"
		+ withHeader("09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,\n")};

	const auto quotes = tryParse<QuoteMessage>(in);
	CHECK(quotes.has_value());
	if (quotes) CHECK(quotes->size() == 1);
}

void test_try_parse_handles_crlf() {
	std::istringstream in{
		"Timestamp,Type,Symbol,BidPrice,BidQty,AskPrice,AskQty,Price,Qty\r\n"
		"09:30:00.003,Q,SYNTH3,87.37,55,87.49,50,,\r\n"};

	const auto quotes = tryParse<QuoteMessage>(in);
	CHECK(quotes.has_value());
	if (quotes) CHECK(quotes->size() == 1);
}

void test_try_parse_reports_errors() {
	std::istringstream no_header{"09:30:00.003,Q,S,1.00,1,1.01,1,,\n"};
	const auto missing = tryParse<QuoteMessage>(no_header);
	CHECK(!missing.has_value());
	if (!missing) CHECK(missing.error() == ParseError::BadHeader);

	std::istringstream empty{""};
	const auto no_rows = tryParse<QuoteMessage>(empty);
	CHECK(!no_rows.has_value());
	if (!no_rows) CHECK(no_rows.error() == ParseError::MissingHeader);

	std::istringstream bad_price{withHeader("09:30:00.003,Q,S,1.2.3,1,1.01,1,,\n")};
	const auto price_error = tryParse<QuoteMessage>(bad_price);
	CHECK(!price_error.has_value());
	if (!price_error) CHECK(price_error.error() == ParseError::BadPrice);

	std::istringstream short_row{withHeader("09:30:00.003,Q,S\n")};
	CHECK(!tryParse<QuoteMessage>(short_row).has_value());
}

void test_try_parse_header_only_yields_nothing() {
	std::istringstream in{std::string{header}};
	const auto quotes = tryParse<QuoteMessage>(in);
	CHECK(quotes.has_value());
	if (quotes) CHECK(quotes->empty());
}

} // namespace

int main() {
	std::println("Running csv tests...");

	test_split_line_counts_trailing_empty_fields();
	test_split_line_rejects_wrong_field_count();
	test_validate_header();

	test_parse_timestamp();
	test_parse_price_is_exact();
	test_parse_symbol();

	test_quote_row();
	test_new_order_row();
	test_row_rejects_fields_from_the_other_type();

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
