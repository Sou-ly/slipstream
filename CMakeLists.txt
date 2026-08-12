cmake_minimum_required(VERSION 3.25)

project(slipstream VERSION 0.1.0 LANGUAGES CXX)

# Defaults for every target below.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # fail loudly instead of falling back to C++17
set(CMAKE_CXX_EXTENSIONS OFF)         # -std=c++23, not -std=gnu++23
add_compile_options(-Wall -Wextra -Wpedantic)

# A forgotten case in a switch over MessageType is a bug, not a warning.
add_compile_options(-Werror=switch)

# std::span is unchecked; this aborts on out-of-range access in Debug.
add_compile_definitions($<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)

add_subdirectory(src)
add_subdirectory(apps/market_data)
add_subdirectory(apps/order_entry)

enable_testing()
add_subdirectory(tests)
