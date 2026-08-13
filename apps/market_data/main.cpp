#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <string_view>

#include <slipstream/codec/encode.hpp>

namespace {
enum class HeaderType : std::uint8_t {
	Timestamp	= 0,
	Type		= 1,
	Symbol		= 2,
	BidPrice	= 3,
	BidQty		= 4,
	AskPrice	= 5,
	AskQty		= 6,
	Price		= 7,
	Qty			= 8
};

inline constexpr std::array allHeaderTypes = { HeaderType::Timestamp, HeaderType::Type,
	HeaderType::Symbol, HeaderType::BidPrice, HeaderType::BidQty, HeaderType::AskPrice,
	HeaderType::AskQty, HeaderType::Price, HeaderType::Qty };

inline constexpr std::array allHeaderNames = { "Timestamp", "Type", "Symbol", "BidPrice",
	"BidQty", "AskPrice", "AskQty", "Price", "Qty" };

static_assert(allHeaderNames.size() == allHeaderTypes.size());

using Row = std::array<std::string, allHeaderTypes.size()>;

void printHelpMessage(std::string_view name) {
	std::cout << "Usage: " << name << " --file <filename>" << std::endl;
}
}

int main(int argc, char* argv[]) {
	std::string filename;

	for (int i = 1; i < argc; ++i) {
		std::string_view arg(argv[i]);
		if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
			filename = argv[++i];
		} else if (arg == "-h" || arg == "--help") {
			printHelpMessage(argv[0]);
			return 0;
		}
	}

	if (filename.empty()) {
		printHelpMessage(argv[0]);
		return 1;
	}

	std::ifstream inf(filename);
	if (!inf.is_open()) {
		std::cerr << "Error opening " << filename << std::endl;
		return 1;
	}

	std::string line;
	std::size_t lineNumber = 0;
	bool headerSeen = false;
	std::vector<Row> csv;

	while (std::getline(inf, line)) {
		++lineNumber;

		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.empty() || line[0] == '#') {
			continue;
		}

		Row row;
		std::size_t fields = 0;
		std::size_t start = 0;

		while (true) {
			const std::size_t comma = line.find(',', start);
			const std::size_t end = (comma == std::string::npos) ? line.size() : comma;
			if (fields == row.size()) {
				++fields;
				break;
			}
			row[fields++] = line.substr(start, end - start);
			if (comma == std::string::npos) {
				break;
			}
			start = comma + 1;
		}

		if (fields != row.size()) {
			std::cerr << filename << ":" << lineNumber << ": expected "
			          << row.size() << " fields" << std::endl;
			return 1;
		}

		if (!headerSeen) {
			headerSeen = true;
			for (std::size_t i = 0; i < row.size(); ++i) {
				if (row[i] != allHeaderNames[i]) {
					std::cerr << filename << ":" << lineNumber << ": expected column "
					          << i << " to be " << allHeaderNames[i]
					          << ", got " << row[i] << std::endl;
					return 1;
				}
			}
			continue;
		}

		csv.push_back(std::move(row));
	}

	if (!headerSeen) {
		std::cerr << filename << ": no header row" << std::endl;
		return 1;
	}

	std::cout << "parsed " << csv.size() << " rows from " << filename << std::endl;

	return 0;
}
