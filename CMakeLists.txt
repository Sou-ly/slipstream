cmake_minimum_required(VERSION 3.25)

project(slipstream
    VERSION 0.1.0
    LANGUAGES CXX)

add_executable(slipstream
    src/main.cpp)

# The language standard is a property of this target, so it only ever reaches
# this project's compile commands. Nothing here is installed, exported, or
# written outside the build tree -- other projects on this machine keep
# whatever standard they already ask for.
#
# cxx_std_23 is a hard requirement, not a preference: if the compiler CMake
# picks cannot do C++23, configuration fails here rather than silently falling
# back to an older standard and failing later on a feature that is missing.
target_compile_features(slipstream PRIVATE cxx_std_23)

# -std=c++23 rather than -std=gnu++23, so GNU extensions do not quietly become
# load-bearing and pin the project to one compiler.
set_target_properties(slipstream PROPERTIES CXX_EXTENSIONS OFF)

target_compile_options(slipstream PRIVATE
    -Wall -Wextra -Wpedantic)
