#pragma once

#include "decode.hpp"

#include <utility>
#include <vector>

namespace slipstream {
class FrameReader {
public:
	std::expected<void, DecodeError> read(std::span<const std::byte> chunk);
	std::vector<AnyMessage> take() noexcept;
private:
	void clear() noexcept;
	void compact() noexcept;
	std::vector<std::byte>	buffer_;
	std::vector<AnyMessage> pending_;
	std::size_t read_pos_{};
};

} // namespace slipstream
