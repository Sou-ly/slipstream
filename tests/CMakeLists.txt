add_executable(test_codec test_codec.cpp)
target_link_libraries(test_codec PRIVATE slipstream::codec)
add_test(NAME codec COMMAND test_codec)

add_executable(test_csv test_csv.cpp)
target_link_libraries(test_csv PRIVATE slipstream::replay)
add_test(NAME csv COMMAND test_csv)

add_executable(test_reader test_reader.cpp)
target_link_libraries(test_reader PRIVATE slipstream::codec)
add_test(NAME reader COMMAND test_reader)
