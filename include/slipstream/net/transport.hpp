#pragma once

#include <functional>

class IFeedTransport {
public:
	virtual ~IFeedTransport() = default;
	virtual void start(std::function<void(const MarketMessage&)> on_message) = 0;
	virtual void stop() = 0;
};
