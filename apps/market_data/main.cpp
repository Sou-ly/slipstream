#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <slipstream/codec/encode.hpp>
#include <slipstream/replay/csv.hpp>

using namespace slipstream;

namespace {

void printHelpMessage(std::string_view name) {
	std::cerr << "Usage: " << name << " --file <path> [--symbol <sym>] [--speed <x>]\n"
	          << "  --speed 1   real time (default)\n"
	          << "  --speed 0   as fast as possible\n"
	          << "Frames go to stdout, diagnostics to stderr.\n";
}

} // namespace

int main(int argc, char* argv[]) {
	std::string filename;
	std::string symbol;
	double speed = 1.0;

	for (int i = 1; i < argc; ++i) {
		std::string_view arg(argv[i]);
		if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
			filename = argv[++i];
		} else if (arg == "--symbol" && i + 1 < argc) {
			symbol = argv[++i];
			if (symbol.size() > 12) {
				std::cerr << "Symbol must be 12 characters or less." << std::endl;
				return 1;
			}
		} else if (arg == "--speed" && i + 1 < argc) {
			speed = std::stod(argv[++i]);
			// Written this way so NaN is rejected too.
			if (!(speed >= 0.0)) {
				std::cerr << "Speed must be zero or positive." << std::endl;
				return 1;
			}
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

	const auto all_quotes = csv::tryParse<QuoteMessage>(inf);
	if (!all_quotes) {
		std::cerr << "Error parsing " << filename << ": " << describe(all_quotes.error()) << std::endl;
		return 1;
	}

	const auto start = std::chrono::steady_clock::now();
	std::optional<std::uint64_t> first_ts;
	std::size_t sent = 0;
	std::size_t late = 0;
	std::chrono::nanoseconds worst_lateness{};

	for (const auto& quote : *all_quotes) {
		if (!symbol.empty() && symbol_view(quote.symbol) != symbol) continue;

		const std::uint64_t ts = quote.ts_ns;
		if (!first_ts) first_ts = ts;

		if (speed > 0.0) {
			// Deadlines are absolute from `start`, so an overshoot never accumulates.
			const auto offset = std::chrono::nanoseconds{
				static_cast<std::int64_t>(static_cast<double>(ts - *first_ts) / speed)};
			const auto deadline = start + offset;
			std::this_thread::sleep_until(deadline);

			const auto lateness = std::chrono::steady_clock::now() - deadline;
			if (lateness > std::chrono::milliseconds{1}) {
				++late;
				worst_lateness = std::max(worst_lateness,
					std::chrono::duration_cast<std::chrono::nanoseconds>(lateness));
			}
		}

		std::array<std::byte, 256> frame{};
		const auto written = encode(quote, frame);
		if (!written) {
			std::cerr << "Encode failed: " << describe(written.error()) << std::endl;
			return 1;
		}

		std::cout.write(reinterpret_cast<const char*>(frame.data()),
		                static_cast<std::streamsize>(*written));
		std::cout.flush();
		++sent;
	}

	std::cout.flush();
	std::cerr << "sent " << sent << " quotes";
	if (late > 0) {
		std::cerr << ", " << late << " late (worst "
		          << std::chrono::duration_cast<std::chrono::milliseconds>(worst_lateness).count()
		          << " ms)";
	}
	std::cerr << std::endl;
	return 0;
}
