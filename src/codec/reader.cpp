#include "slipstream/codec/reader.hpp"

namespace slipstream {

std::expected<void, DecodeError> FrameReader::read(std::span<const std::byte> chunk) {
	buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
	
	while (true) {
		auto result = try_decode(std::span(buffer_).subspan(read_pos_));
		if (result) {
			read_pos_ += result->consumed;
			pending_.push_back(result->message);	
			continue;
		} else if (is_fatal(result.error())) {
			return std::unexpected(result.error());
		} else if (result.error() == DecodeError::Incomplete) {
			if (read_pos_ == buffer_.size()) clear(); else compact();
			return {};
		} else { // recoverable
			WireHeader header{};
			std::memcpy(&header, buffer_.data() + read_pos_, sizeof header);
			read_pos_ += sizeof(header) + header.body_len;
			continue;
		}
	}
}

void FrameReader::clear() noexcept {
	// no bytes move
	buffer_.clear();
	read_pos_ = 0;
}

void FrameReader::compact() noexcept {
	// at most 54 bytes move since only the incomplete frame gets cleared
	buffer_.erase(buffer_.begin(), buffer_.begin() + read_pos_);
	read_pos_ = 0;
}

std::vector<AnyMessage> FrameReader::take() noexcept {
	// can also return std::exchange(pending_, {});
	auto out = std::move(pending_);
	pending_.clear(); // moved-from vector is valid but unspecified
	return out;
}

} //namespace slipstream
