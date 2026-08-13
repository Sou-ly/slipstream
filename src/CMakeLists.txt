# A library so apps and tests share one build, and the headers' asserts actually run.
add_library(slipstream_codec
    codec/decode.cpp)
add_library(slipstream::codec ALIAS slipstream_codec)

# PUBLIC: consumers include <slipstream/codec/...>, so the path propagates.
target_include_directories(slipstream_codec PUBLIC ${PROJECT_SOURCE_DIR}/include)

add_library(slipstream_replay
    replay/csv.cpp)
add_library(slipstream::replay ALIAS slipstream_replay)

target_link_libraries(slipstream_replay PUBLIC slipstream::codec)
