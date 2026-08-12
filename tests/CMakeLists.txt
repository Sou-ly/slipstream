add_executable(test_codec test_codec.cpp)
target_link_libraries(test_codec PRIVATE slipstream::codec)
add_test(NAME codec COMMAND test_codec)
