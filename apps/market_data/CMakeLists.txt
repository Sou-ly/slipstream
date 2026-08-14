add_executable(market_data main.cpp)
target_link_libraries(market_data PRIVATE slipstream::replay)
